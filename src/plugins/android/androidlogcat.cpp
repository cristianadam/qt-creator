// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "androidlogcat.h"

#include "androidconfigurations.h"
#include "androiddevice.h"
#include "androidutils.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/modemanager.h>
#include <coreplugin/outputpane.h>

#include <projectexplorer/appoutputpane.h>
#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/runcontrol.h>

#include <utils/aspects.h>
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

struct LogcatEntry
{
    QString line;
    qint32 pid = -1;
    QString packageName; // resolved from the pid when the entry is buffered
    bool bypassFilter = false;

    static LogcatEntry fromLine(const QString &raw);
    QString displayText() const;
};

// The '-v threadtime -v year' line layout; the tag runs up to the first ": ".
static const QRegularExpression regExpLogcat(
    "(?:\\x1b\\[[0-9;]*m)?"                                                // optional ANSI color
    "(?<timestamp>(?:\\d{4}-)?\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d\\.\\d+) +"
    "(?<pid>\\d+) +"
    "(?<tid>\\d+) +"
    "(?<level>[VDIWEF]) "
    "(?<tag>.*?) *: ");

// Keep the line as received (adb's -v color paints it); parse the pid for
// the pid-tid and package-name columns.
LogcatEntry LogcatEntry::fromLine(const QString &raw)
{
    LogcatEntry entry{.line = raw};
    const auto match = regExpLogcat.match(raw);
    if (match.hasMatch())
        entry.pid = match.captured("pid").toInt();
    return entry;
}

QString LogcatEntry::displayText() const
{
    if (bypassFilter)
        return line;
    const auto match = regExpLogcat.match(line);
    if (!match.hasMatch())
        return line;
    const auto &settings = logcatSettings();
    QString result = line.left(match.capturedStart("timestamp"));
    if (settings.showTimestamp())
        result += match.captured("timestamp") + QLatin1Char(' ');
    if (settings.showPid()) {
        result += match.captured("pid") + QLatin1Char('-') + match.captured("tid")
                  + QLatin1Char(' ');
    }
    if (settings.showTag())
        result += match.captured("tag") + QLatin1Char(' ');
    if (settings.showPackage() && !packageName.isEmpty())
        result += packageName + QLatin1Char(' ');
    result += match.captured("level") + QLatin1String("  ");
    result += line.mid(match.capturedEnd());
    return result;
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
    void refreshProcessNames();

    RunControl *tab() const { return m_tabContext.tab; }
    void attachTab(RunControl *tab);
    void setStreaming(bool streaming);

private:
    struct TabContext
    {
        QPointer<RunControl> tab;
        bool streaming = false;
        QList<LogcatEntry> buffer;
        QHash<qint32, QString> processNames;
        LogcatFilter filter;

        void appendEntry(LogcatEntry entry);
        void fillPackageNames();
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

// Fill the package-name column: pids resolve through "adb shell ps", run
// once per newly seen pid (the placeholder inserted by the caller keeps a
// pid that ps does not know from asking again).
void LogcatStream::refreshProcessNames()
{
    if (m_device->deviceState() != IDevice::DeviceReadyToUse)
        return;
    const auto ps = new Process(this);
    ps->setCommand(adbCommand({"shell", "ps", "-A", "-o", "PID,NAME"}));
    QObject::connect(ps, &Process::done, this, [this, ps] {
        const QStringList psLines = ps->cleanedStdOut().split('\n', Qt::SkipEmptyParts);
        bool changed = false;
        for (const auto &psLine : psLines) {
            const auto fields = psLine.simplified().split(QChar::Space);
            auto ok = false;
            const auto pid = fields.size() == 2 ? fields.first().toInt(&ok) : 0;
            if (ok && m_tabContext.processNames.value(pid) != fields.last()) {
                m_tabContext.processNames.insert(pid, fields.last());
                changed = true;
            }
        }
        if (changed) {
            m_tabContext.fillPackageNames();
            m_tabContext.renderFromBuffer();
        }
        ps->deleteLater();
    });
    ps->start();
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
            const LogcatEntry entry = LogcatEntry::fromLine(line);
            if (entry.pid > 0 && !m_tabContext.processNames.contains(entry.pid)) {
                m_tabContext.processNames.insert(entry.pid, QString()); // ask ps once per pid
                refreshProcessNames();
            }
            m_tabContext.appendEntry(entry);
        });
        process.setStdErrLineCallback(
            [this](const QString &line) { postMessage(line); });
        // -T 1 starts the tail at the current head, skipping the device's existing
        // ring buffer (live tail only).
        process.setCommand(
            adbCommand({"logcat", "-T", "1", "-v", "color", "-v", "threadtime", "-v", "year"}));
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

void LogcatStream::TabContext::appendEntry(LogcatEntry entry)
{
    entry.packageName = processNames.value(entry.pid);
    buffer.append(entry);

    while (buffer.size() > logcatSettings().bufferedLines())
        buffer.removeFirst();

    if (tab && filter.accepts(entry))
        tab->postMessage(entry.displayText(), Utils::StdOutFormat, false);
}

// A pid can outrun ps: entries buffered before its answer get their name here.
void LogcatStream::TabContext::fillPackageNames()
{
    for (LogcatEntry &entry : buffer) {
        if (entry.packageName.isEmpty())
            entry.packageName = processNames.value(entry.pid);
    }
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
            tab->postMessage(entry.displayText(), Utils::StdOutFormat, false);
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
