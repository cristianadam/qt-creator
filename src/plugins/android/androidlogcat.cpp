// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "androidlogcat.h"

#include "androidconfigurations.h"
#include "androiddevice.h"
#include "androidutils.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/modemanager.h>
#include <coreplugin/outputpane.h>

#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/runcontrol.h>

#include <utils/commandline.h>
#include <utils/outputformat.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>

#include <QtTaskTree/QBarrier>
#include <QtTaskTree/QTaskTree>

#include <QChar>
#include <QHash>
#include <QMenu>
#include <QObject>
#include <QPointer>

using namespace Utils;
using namespace Core;
using namespace QtTaskTree;
using namespace ProjectExplorer;

namespace Android::Internal {

// Cap on the per-device entry history kept for re-rendering on filter changes.
static constexpr int maxBufferedLines = 1000;

struct LogcatEntry
{
    QString line;
    bool bypassFilter = false;

    static LogcatEntry fromLine(const QString &raw);
};

// Keep the line as received; coloring comes from adb's -v color.
LogcatEntry LogcatEntry::fromLine(const QString &raw)
{
    return {.line = raw};
}

class LogcatFilter
{
public:
    void setFromText(const QString &text);
    bool accepts(const LogcatEntry &entry) const;

    QString filterText() const { return m_filterText; }
    bool isActive() const { return !m_predicates.isEmpty(); }

    using FilterPredicate = std::function<bool(const LogcatEntry &)>;

private:
    QList<FilterPredicate> m_predicates;
    QString m_filterText;
};

void LogcatFilter::setFromText(const QString &text)
{
    m_filterText = text;
    m_predicates.clear();
}

bool LogcatFilter::accepts(const LogcatEntry &entry) const
{
    if (entry.bypassFilter)
        return true;
    for (const FilterPredicate &filterPredicate : m_predicates)
        if (!filterPredicate(entry))
            return false;
    return true;
}

class LogcatStream : public QObject
{
public:
    LogcatStream(AndroidDevice::ConstPtr device);
    ~LogcatStream() override;

    void start();
    void stop();

    RunControl *tab() const { return m_tabContext.tab; }
    void attachTab(RunControl *tab);
    void setStreaming(bool streaming);

private:
    struct TabContext
    {
        QPointer<RunControl> tab;
        bool streaming = false;
        QList<LogcatEntry> buffer;
        LogcatFilter filter;

        void appendEntry(const LogcatEntry &entry);
        void applyFilter() const;
        void renderFromBuffer() const;

        QString windowFilterText() const
        {
            return filter.isActive() ? QString() : filter.filterText();
        }
    };

    void onTabDestroyed();

    void postMessage(const QString &msg);

    void onDisconnected();
    void onConnected();

    void onOutputFilterTextChanged(const QString &text);

    const AndroidDevice::ConstPtr m_device;
    std::unique_ptr<QTaskTree> m_task;
    TabContext m_tabContext;

    CommandLine adbCommand(const QStringList &args) const
    {
        return {AndroidConfig::adbToolPath(),
                adbSelector(m_device->serialNumber()) + args};
    }
};

static QHash<Id, LogcatStream *> &streamRegistry()
{
    static QHash<Id, LogcatStream *> map;
    return map;
}

LogcatStream::LogcatStream(AndroidDevice::ConstPtr device)
    : m_device(std::move(device))
{
    DeviceManager *dm = DeviceManager::instance();
    QObject::connect(dm, &DeviceManager::deviceRemoved, this, [this](Id removedId) {
        if (removedId == m_device->id())
            onDisconnected();
    });
    QObject::connect(dm, &DeviceManager::deviceUpdated, this, [this](Id id) {
        if (id != m_device->id())
            return;
        const auto state = m_device->deviceState();
        if (state == IDevice::DeviceDisconnected)
            onDisconnected();
        else if (state == IDevice::DeviceReadyToUse)
            onConnected();
    });
}

LogcatStream::~LogcatStream()
{
    stop();
    auto &reg = streamRegistry();
    if (reg.value(m_device->id()) == this)
        reg.remove(m_device->id());
}

void LogcatStream::attachTab(RunControl *tab)
{
    QTC_ASSERT(tab, return);
    m_tabContext = {};
    m_tabContext.tab = tab;
    tab->setDisplayName(m_device->displayNameWithSerial());
    // adb tails only while the tab's output is visible (current tab + pane shown)
    QObject::connect(tab, &RunControl::outputVisibilityChanged,
                     this, &LogcatStream::setStreaming);
    QObject::connect(tab, &RunControl::outputFilterChanged, this, [this](const QString &text) {
        onOutputFilterTextChanged(text);
    });
    QObject::connect(tab, &QObject::destroyed, this, [this] { onTabDestroyed(); });
    setStreaming(tab->isOutputVisible());
}

void LogcatStream::onTabDestroyed()
{
    m_tabContext = {};
    stop();
    streamRegistry().remove(m_device->id());
    deleteLater();
}

void LogcatStream::setStreaming(bool streaming)
{
    if (!m_tabContext.tab)
        return;
    if (streaming == m_tabContext.streaming)
        return;
    m_tabContext.streaming = streaming;
    if (streaming)
        start();
    else
        stop();
}

void LogcatStream::start()
{
    if (m_task)
        return;
    if (m_device->deviceState() != IDevice::DeviceReadyToUse)
        return;
    const auto onSetup = [this](Process &process) {
        process.setStdOutLineCallback([this](const QString &line) {
            // A flaky device/connection can make adb dump a corrupt binary chunk
            // (NUL-padded log records) that renders as boxes, so drop any that does.
            if (line.contains(QChar(u'\0')))
                return;
            m_tabContext.appendEntry(LogcatEntry::fromLine(line));
        });
        process.setStdErrLineCallback(
            [this](const QString &line) { postMessage(line); });
        // -T 1 starts the tail at the current head, skipping the device's existing
        // ring buffer (live tail only).
        process.setCommand(adbCommand({"logcat", "-T", "1", "-v", "color", "-v", "brief"}));
    };
    m_task = std::make_unique<QTaskTree>(Group{Forever{ProcessTask(onSetup) || successItem}});
    m_task->start();
}

void LogcatStream::stop()
{
    m_task.reset();
}

static QString banner(const QString &label, const QString &state)
{
    return QString("**** %1 - %2 ****\n").arg(label, state);
}

void LogcatStream::onDisconnected()
{
    if (!m_task)
        return;
    postMessage(banner(m_device->displayNameWithSerial(), QLatin1String("disconnected")));
    stop();
}

void LogcatStream::onConnected()
{
    if (m_tabContext.tab && m_tabContext.streaming)
        start();
}

void LogcatStream::postMessage(const QString &msg)
{
    m_tabContext.appendEntry({.line = msg, .bypassFilter = true});
}

void LogcatStream::TabContext::appendEntry(const LogcatEntry &entry)
{
    buffer.append(entry);
    if (buffer.size() > maxBufferedLines)
        buffer.removeFirst();
    if (tab && filter.accepts(entry))
        tab->postMessage(entry.line, Utils::StdOutFormat, false);
}

void LogcatStream::TabContext::applyFilter() const
{
    if (!tab)
        return;
    tab->setOutputFilterText(filter.filterText());
    tab->setOutputContentFilter(windowFilterText());
}

void LogcatStream::TabContext::renderFromBuffer() const
{
    if (!tab)
        return;
    applyFilter();
    tab->clearOutput();
    for (const LogcatEntry &entry : buffer) {
        if (filter.accepts(entry))
            tab->postMessage(entry.line, Utils::StdOutFormat, false);
    }
}

void LogcatStream::onOutputFilterTextChanged(const QString &text)
{
    m_tabContext.filter.setFromText(text);
    if (m_tabContext.filter.isActive())
        m_tabContext.renderFromBuffer();
    else
        m_tabContext.applyFilter();
}

static LogcatStream *ensureStream(const AndroidDevice::ConstPtr &device)
{
    if (!device || device->serialNumber().isEmpty())
        return nullptr;
    const auto id = device->id();
    auto &reg = streamRegistry();
    if (auto *stream = reg.value(id))
        return stream;
    auto *stream = new LogcatStream(device);
    reg.insert(id, stream);
    return stream;
}

static RunControl *openLogcatTabForStream(LogcatStream *logcatStream)
{
    if (!logcatStream)
        return nullptr;
    if (RunControl *existing = logcatStream->tab())
        return existing;
    auto *runControl = new RunControl(ProjectExplorer::Constants::NORMAL_RUN_MODE);
    runControl->setPromptToStop([](bool *) { return true; });
    runControl->setRunControlsEnabled(false);
    logcatStream->attachTab(runControl);

    runControl->setRunRecipe(QBarrierTask([](QBarrier &) {}).withCancel([runControl] {
        return makeObjectSignal(runControl, &RunControl::canceled);
    }));
    runControl->start();
    return runControl;
}

void showLogcatTab(const AndroidDevice::ConstPtr &device)
{
    auto *stream = ensureStream(device);
    if (!stream)
        return;
    RunControl *const tab = openLogcatTabForStream(stream);
    if (!tab || tab->isOutputVisible())
        return;
    if (!OutputPanePlaceHolder::getCurrent())
        ModeManager::activateMode(Core::Constants::MODE_EDIT);
    tab->showOutputPane();
}

} // namespace Android::Internal
