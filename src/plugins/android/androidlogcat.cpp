// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "androidlogcat.h"

#include "androidconfigurations.h"
#include "androidconstants.h"
#include "androiddevice.h"
#include "androidtr.h"
#include "androidutils.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/modemanager.h>
#include <coreplugin/outputpane.h>
#include <coreplugin/outputwindow.h>

#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/devicesupport/idevice.h>
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

#include <memory>

using namespace Utils;
using namespace Core;
using namespace QtTaskTree;
using namespace ProjectExplorer;

namespace Android::Internal {

using AndroidDeviceConstPtr = std::shared_ptr<const AndroidDevice>;

//Helpers

static CommandLine adbLogcat(const QString &serialNumber, const QStringList &extra = {})
{
    return {AndroidConfig::adbToolPath(), adbSelector(serialNumber) + QStringList{"logcat"} + extra};
}
// Cap on the per-device entry history kept for re-rendering on filter changes.
// 10k entries of typical line length occupy a few megabytes.(10K entries per device = 2~3MB of memory)
static constexpr int maxBufferedLines = 10000;

struct LogcatEntry
{
    QString line; // raw 'adb logcat -v brief' line, ANSI color kept
    Utils::OutputFormat fmt;
    qint32 pid = -1;
    bool bypassFilter = false;
};

static const QRegularExpression regExpLogcat(
    "(?:\\x1b\\[[0-9;]*m)?"   // optional ANSI color
    "([VDIWEF])"             // 1: log level
    "(/[^(]*)"               // 2: /tag
    "(\\(\\s*(\\d+)\\s*\\))"  // 3: (pid)   4: pid digits
);

// Keep the line as received; only extract the PID so the filter can match it.
static LogcatEntry parseLogcat(const QString &raw)
{
    LogcatEntry entry{.line = raw};
    QChar level;
    const auto match = regExpLogcat.match(raw);
    if (match.hasMatch()) {
        level = match.captured(1).at(0);
        entry.pid = match.captured(4).toInt();
    }
    const bool isError = level == QLatin1Char('W') || level == QLatin1Char('E')
                         || level == QLatin1Char('F');
    entry.fmt = isError ? Utils::StdErrFormat : Utils::StdOutFormat;
    return entry;
}

// User-facing device identity: "<displayName> (<serial>)". Used in
// the Tools > Android > Logcat submenu and composed into the tab title.
static QString deviceLabel(const AndroidDeviceConstPtr &device)
{
    return QString("%1 (%2)").arg(device->displayName(), device->serialNumber());
}

static QString banner(const QString &label, const QString &state)
{
    return QString("**** %1 - %2 ****").arg(label, state);
}

static AndroidDeviceConstPtr asReadyAndroidDevice(const IDeviceConstPtr &device)
{
    if (!device || device->type() != Constants::ANDROID_DEVICE_TYPE
        || device->deviceState() != IDevice::DeviceReadyToUse)
        return {};
    return std::dynamic_pointer_cast<const AndroidDevice>(device);
}

//LogcatFilter
class LogcatFilter
{
public:
    void setFromText(const QString &text);
    bool accepts(const LogcatEntry &entry) const;

    QString cachedText() const { return m_cachedText; }
    bool isActive() const { return !m_predicates.isEmpty(); }

    using FilterPredicate = std::function<bool(const LogcatEntry &)>;

private:
    QList<FilterPredicate> m_predicates;
    QString m_cachedText;
};

void LogcatFilter::setFromText(const QString &text)
{
    m_cachedText = text;
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

//LogcatStream
class LogcatStream : public QObject
{
public:
    LogcatStream(AndroidDeviceConstPtr device);
    ~LogcatStream() override;
    Id deviceId() const { return m_device->id(); }
    QString serial() const { return m_device->serialNumber(); }

    void start();
    void stop();

    RunControl *tab() const { return m_tabContext.tab; }
    void attachTab(RunControl *tab);
    void setTabActive(bool active);

private:
    struct TabContext
    {
        QPointer<RunControl> tab;
        bool active = false;
        QList<LogcatEntry> buffer;
        LogcatFilter filter;

        void appendEntry(const LogcatEntry &entry);
        void applyFilter() const;
        void renderFromBuffer() const;

        QString windowFilterText() const
        {
            return filter.isActive() ? QString() : filter.cachedText();
        }
    };
    enum class Lifecycle { Stop, Start };

    void startAdbTail();
    void stopAdbTail();

    void onTabDestroyed();

    void postMessage(const QString &msg, Utils::OutputFormat fmt);

    void onDisconnected();
    void onConnected();

    void onOutputFilterTextChanged(const QString &text);

    const AndroidDeviceConstPtr m_device;
    std::unique_ptr<QTaskTree> m_task;
    TabContext m_tabContext;
    Lifecycle m_lifecycle = Lifecycle::Stop;
};

static QHash<Id, LogcatStream *> &streamRegistry()
{
    static QHash<Id, LogcatStream *> map;
    return map;
}

LogcatStream::LogcatStream(AndroidDeviceConstPtr device)
    : m_device(std::move(device))
{
    DeviceManager *dm = DeviceManager::instance();
    QObject::connect(dm, &DeviceManager::deviceRemoved, this, [this](Id removedId) {
        if (removedId == deviceId())
            onDisconnected();
    });
    QObject::connect(dm, &DeviceManager::deviceUpdated, this, [this](Id id) {
        if (id != deviceId())
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
    if (reg.value(deviceId()) == this)
        reg.remove(deviceId());
}

void LogcatStream::attachTab(RunControl *tab)
{
    QTC_ASSERT(tab, return);
    m_tabContext = {};
    m_tabContext.tab = tab;
    tab->setDisplayName(deviceLabel(m_device));
    // adb tails only while the tab is the currently selected one
    QObject::connect(tab, &RunControl::tabActiveChanged, this, [this](bool active) {
        setTabActive(active);
    });

    QObject::connect(tab, &RunControl::outputFilterChanged, this, [this](const QString &text) {
        onOutputFilterTextChanged(text);
    });

    QObject::connect(tab, &QObject::destroyed, this, [this] { onTabDestroyed(); });
}

// The stream's lifetime follows its tab: once the tab goes away, stop the
// adb tail, leave the registry, and self-destruct.
void LogcatStream::onTabDestroyed()
{
    m_tabContext = {};
    stop();
    streamRegistry().remove(deviceId());
    deleteLater();
}

void LogcatStream::setTabActive(bool active)
{
    if (!m_tabContext.tab)
        return;
    if (active == m_tabContext.active)
        return;
    m_tabContext.active = active;
    if (active)
        start();
    else
        stop();
}

void LogcatStream::start()
{
    if (m_lifecycle == Lifecycle::Start)
        return;
    if (m_device->deviceState() != IDevice::DeviceReadyToUse)
        return;
    startAdbTail();
    m_lifecycle = Lifecycle::Start;
}

void LogcatStream::stop()
{
    if (m_lifecycle == Lifecycle::Stop)
        return;
    stopAdbTail();
    m_lifecycle = Lifecycle::Stop;
}

void LogcatStream::onDisconnected()
{
    if (m_lifecycle == Lifecycle::Stop)
        return;
    postMessage(banner(deviceLabel(m_device), QLatin1String("disconnected")), Utils::NormalMessageFormat);
    stop();
}

void LogcatStream::onConnected()
{
    if (m_tabContext.tab && m_tabContext.active)
        start();
}

void LogcatStream::postMessage(const QString &msg, Utils::OutputFormat fmt)
{
    m_tabContext.appendEntry({.line = msg, .fmt = fmt, .bypassFilter = true});
}

void LogcatStream::TabContext::appendEntry(const LogcatEntry &entry)
{
    buffer.append(entry);
    if (buffer.size() > maxBufferedLines)
        buffer.removeFirst();
    if (tab && filter.accepts(entry))
        tab->postMessage(entry.line, entry.fmt, false);
}

void LogcatStream::TabContext::applyFilter() const
{
    if (!tab)
        return;
    tab->setOutputFilterText(filter.cachedText());
    if (OutputWindow *const w = tab->outputWindow())
        w->updateFilterProperties(windowFilterText(), Qt::CaseInsensitive, false, false, 0, 0);
}

void LogcatStream::TabContext::renderFromBuffer() const
{
    applyFilter();
    OutputWindow *const w = tab ? tab->outputWindow() : nullptr;
    if (!w)
        return;
    w->clear();
    for (const LogcatEntry &entry : buffer) {
        if (filter.accepts(entry))
            tab->postMessage(entry.line, entry.fmt, false);
    }
}

void LogcatStream::onOutputFilterTextChanged(const QString &text)
{
    m_tabContext.filter.setFromText(text);
    // Keyword-only changes filter in place via the OutputWindow; replay the
    // buffer only when the entry predicates change.
    if (m_tabContext.filter.isActive())
        m_tabContext.renderFromBuffer();
    else
        m_tabContext.applyFilter();
}

void LogcatStream::startAdbTail()
{
    const QString serialNumber = serial();
    const auto onSetup = [this, serialNumber](Process &process) {
        process.setStdOutLineCallback([this](const QString &line) {
            m_tabContext.appendEntry(parseLogcat(line));
        });
        // Buffered with bypassFilter so adb's own errors survive re-renders.
        process.setStdErrLineCallback([this](const QString &line) {
            postMessage(line, Utils::StdErrFormat);
        });
        // -T 1 starts the tail at the current head, skipping the device's existing ring buffer (live tail only)
        process.setCommand(adbLogcat(serialNumber, {"-T", "1", "-v", "color", "-v", "brief"}));
    };
    m_task = std::make_unique<QTaskTree>(Group{Forever{ProcessTask(onSetup) || successItem}});
    m_task->start();
}

void LogcatStream::stopAdbTail()
{
    m_task.reset();
}

static LogcatStream *ensureStream(const AndroidDeviceConstPtr &device)
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

//Tab plumbing

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

static RunControl *ensureVisibleTab(const AndroidDeviceConstPtr &device)
{
    auto *stream = ensureStream(device);
    if (!stream)
        return nullptr;
    RunControl *const tab = openLogcatTabForStream(stream);
    if (tab) {
        // showOutputPane() does not switch modes; from a mode without an output
        // area (e.g. Preferences) jump to Edit first so the tab is revealed.
        if (!OutputPanePlaceHolder::getCurrent())
            ModeManager::activateMode(Core::Constants::MODE_EDIT);
        tab->showOutputPane();
    }
    return tab;
}

//Menu wiring

// Tools > Android > Logcat is a submenu listing every ready Android device.
// rebuilt on demand so connected/disconnected transitions show up immediately.
static void populateLogcatSubmenu(QMenu *menu)
{
    qDeleteAll(menu->actions());
    DeviceManager::forEachDevice([menu](const IDeviceConstPtr &device) {
        const auto androidDev = asReadyAndroidDevice(device);
        if (!androidDev)
            return;
        menu->addAction(deviceLabel(androidDev), menu, [androidDev] {ensureVisibleTab(androidDev);});
    });
    if (menu->isEmpty()) {
        QAction *const placeholder = menu->addAction(Tr::tr("No Android device connected"));
        placeholder->setEnabled(false);
    }
}

//Public API

void initAndroidLogcat()
{
    ActionContainer *const parent = ActionManager::actionContainer(Constants::ANDROID_TOOLS_MENU_ID);
    if (!parent)
        return;
    ActionContainer *const logcatMenu = ActionManager::createMenu("Android.Tools.Logcat");
    logcatMenu->menu()->setTitle(Tr::tr("Logcat"));
    logcatMenu->setOnAllDisabledBehavior(ActionContainer::Show);

    parent->addMenu(logcatMenu);
    QObject::connect(parent->menu(), &QMenu::aboutToShow, parent->menu(), [logcatMenu] {
        populateLogcatSubmenu(logcatMenu->menu());
    });
}

} // namespace Android::Internal
