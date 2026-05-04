// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "androidlogcat.h"

#include "QtTaskTree/qtasktree.h"
#include "androidconfigurations.h"
#include "androidconstants.h"
#include "androiddevice.h"
#include "androidtoolmenu.h"
#include "androidtr.h"
#include "androidutils.h"
#include "projectexplorer/devicesupport/devicemanager.h"
#include "utils/action.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>

#include <cstddef>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/runcontrol.h>

#include <utils/commandline.h>
#include <utils/outputformat.h>
#include <utils/qtcprocess.h>

#include <QtTaskTree/QBarrier>
#include <QtTaskTree/QTaskTree>

#include <QHash>
#include <QMenu>
#include <QObject>
#include <QPointer>

#include <memory>

using namespace Utils;
using namespace QtTaskTree;
using namespace ProjectExplorer;

namespace Android::Internal {

static CommandLine adbLogcat(const QString &serialNumber, const QStringList &extra = {})
{
    return {AndroidConfig::adbToolPath(), adbSelector(serialNumber) + QStringList{"logcat"} + extra};
}

struct LogcatEntry
{
    QString line;
    Utils::OutputFormat fmt;
};

class LogcatStream : public QObject
{
    Q_OBJECT
public:
    LogcatStream(Id deviceId, QString serial)
        : m_deviceId(deviceId)
        , m_serial(std::move(serial))
    {}
    Id deviceId() const { return m_deviceId; }
    QString serial() const { return m_serial; }

    void retain();
    void release();

    // Per-tab context. Single tab per stream at the moment; later this would be a list.
    // Moreover, more fields might be added later
    struct TabContext
    {
        QPointer<RunControl> tab;
    };

    void setTab(RunControl *tab) { m_tabContext.tab = tab; }

signals:
    void entryReady(const LogcatEntry &entry);

private:
    const Id m_deviceId;
    const QString m_serial;
    std::unique_ptr<QTaskTree> m_task;
    int m_references;
    TabContext m_tabContext;

private:
    void startTail();
    void stopTail();

    void parseLine(const QString &raw, bool onlyError);
};
static QHash<Id, LogcatStream *> &streamRegistry()
{
    static QHash<Id, LogcatStream *> map;
    return map;
}

void LogcatStream::retain()
{
    if (m_references++ == 0)
        startTail();
}

void LogcatStream::release()
{
    if (--m_references > 0)
        return;
    stopTail();
    streamRegistry().remove(m_deviceId);
    deleteLater();
}

void LogcatStream::startTail()
{
    const auto serial = m_serial;
    QPointer<LogcatStream> streamPtr = this;

    const auto onClearSetup = [serial](Process &process) {
        process.setCommand(adbLogcat(serial, {"-c"}));
    };

    const auto onSetup = [serial, streamPtr](Process &process) {
        process.setStdOutLineCallback([streamPtr](const QString &line) {
            if (streamPtr)
                streamPtr->parseLine(line, false);
        });
        process.setStdErrLineCallback([streamPtr](const QString &line) {
            if (streamPtr)
                streamPtr->parseLine(line, true);
        });
        process.setCommand(adbLogcat(serial, {"-v", "color", "-v", "brief"}));
    };
    m_task = std::make_unique<QTaskTree>(
        Group{ProcessTask(onClearSetup) || successItem, Forever{ProcessTask(onSetup) || successItem}});
    m_task->start();
}
void LogcatStream::stopTail()
{
    m_task.reset();
}

static const QRegularExpression regExpLogcat(
    "^"                     // line start
    "(?:\\x1b\\[[0-9;]*m)?" // optional ANSI color
    "([VDIWEF])"            // log level
    "/"                     // level/tag separator
    "[^(]*"                 // tag
    "\\(\\s*(\\d+)\\s*\\)"  // PID
);

void LogcatStream::parseLine(const QString &raw, bool onlyError)
{
    const auto m = regExpLogcat.match(raw);
    const auto level = m.hasMatch() ? m.captured(1).at(0) : QChar();
    const auto isFatal = level == QLatin1Char('F');

    auto line = raw;
    if (m.hasMatch()) {
        //strip pid so the rendered line is "level/tag: message" for now
        const auto from = line.indexOf(QLatin1Char('('));
        const auto to = line.indexOf(QLatin1Char(')'), from);
        if (from >= 0 && to > from)
            line.remove(from, to - from + 1);
    }
    const auto isError = onlyError || level == QLatin1Char('W') || level == QLatin1Char('E')
                         || isFatal;
    emit entryReady({line, isError ? Utils::StdErrFormat : Utils::StdOutFormat});
}

static bool isAndroidDeviceReady(const IDeviceConstPtr &device)
{
    return device && device->type() == Constants::ANDROID_DEVICE_TYPE
           && device->deviceState() == IDevice::DeviceReadyToUse;
}

static LogcatStream *ensureStream(const IDeviceConstPtr &device)
{
    if (!isAndroidDeviceReady(device))
        return nullptr;
    const auto androidDev = std::dynamic_pointer_cast<const AndroidDevice>(device);
    const auto serial = androidDev->serialNumber();
    if (serial.isEmpty())
        return nullptr;
    const auto id = device->id();
    auto &reg = streamRegistry();
    if (auto *stream = reg.value(id))
        return stream;
    auto *stream = new LogcatStream(id, serial);
    reg.insert(id, stream);
    return stream;
}

static QString logcatTitle(const QString &serial)
{
    return Tr::tr("Logcat (%1)").arg(serial);
}

static RunControl *openLogcatTabForStream(LogcatStream *logcatStream)
{
    if (!logcatStream)
        return nullptr;

    auto *runControl = new RunControl(ProjectExplorer::Constants::NORMAL_RUN_MODE);
    runControl->setDisplayName(logcatTitle(logcatStream->serial()));

    logcatStream->retain();
    logcatStream->setTab(runControl);

    QPointer<RunControl> rcPtr = runControl;
    QObject::connect(
        logcatStream, &LogcatStream::entryReady, runControl, [rcPtr](const LogcatEntry &entry) {
            if (rcPtr)
                rcPtr->postMessage(entry.line, entry.fmt, false);
        });

    QPointer<LogcatStream> streamPtr = logcatStream;
    QObject::connect(runControl, &QObject::destroyed, [streamPtr] {
        if (!streamPtr)
            return;
        streamPtr->setTab(nullptr);
        streamPtr->release();
    });
    // add to keep recipe stays "running" indefinitely?
    rcPtr->setRunRecipe(QBarrierTask([](QBarrier &) {}).withCancel([rcPtr] {
        return makeObjectSignal(rcPtr.data(), &RunControl::canceled);
    }));
    rcPtr->start();
    return rcPtr;
}

static RunControl *ensureVisibleTab(const IDeviceConstPtr &device)
{
    auto *stream = ensureStream(device);
    if (!stream)
        return nullptr;
    return openLogcatTabForStream(stream);
}

static bool hasAtLeastAndroidDeviceReady()
{
    bool ready = false;
    DeviceManager::forEachDevice([&ready](const IDeviceConstPtr &device) {
        if (!ready && isAndroidDeviceReady(device))
            ready = true;
    });
    return ready;
}

static void openLogcatTab()
{
    DeviceManager::forEachDevice([](const IDeviceConstPtr &device) {
        if (isAndroidDeviceReady(device))
            ensureVisibleTab(device);
    });
}

void initAndroidLogcat()
{
    // Tools > Android > Logcat opens a tab for every ready Android device
    Utils::Action *action = nullptr;
    Core::ActionBuilder(Core::ActionManager::instance(), "Android.Tools.Logcat")
        .setText(Tr::tr("Logcat"))
        .bindContextAction(&action)
        .addOnTriggered(&openLogcatTab)
        .addToContainer(ANDROID_TOOLS_MENU_ID);

    QPointer<QAction> actionPtr = action;
    if (Core::ActionContainer *container = Core::ActionManager::actionContainer(
            ANDROID_TOOLS_MENU_ID)) {
        QObject::connect(container->menu(), &QMenu::aboutToShow, container->menu(), [actionPtr] {
            if (!actionPtr)
                return;
            const bool hasDevice = hasAtLeastAndroidDeviceReady();
            actionPtr->setEnabled(hasDevice);
            actionPtr->setToolTip(
                hasDevice ? Tr::tr("Open the device's Logcat tab")
                          : Tr::tr("No connected Android devices"));
        });
    }
}

} // namespace Android::Internal

#include "androidlogcat.moc"
