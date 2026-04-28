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

#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/runcontrol.h>

#include <utils/commandline.h>
#include <utils/outputformat.h>
#include <utils/qtcprocess.h>

#include <QtCore/qchar.h>

#include <QtTaskTree/QBarrier>
#include <QtTaskTree/QTaskTree>
#include <QtTaskTree/qtasktree.h>

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

static const QRegularExpression regExpLogcat(
    "^"                     // line start
    "(?:\\x1b\\[[0-9;]*m)?" // optional ANSI color
    "([VDIWEF])"            // log level
    "/"                     // level/tag separator
    "([^(]*)"               // tag
    "\\(\\s*(\\d+)\\s*\\)"  // PID
);

static QString logcatTitle(const QString &label)
{
    return Tr::tr("Logcat (%1)").arg(label);
}

static QString deviceLabel(const AndroidDeviceConstPtr &device)
{
    return QString("%1 - %2").arg(device->displayName(), device->serialNumber());
}

static AndroidDeviceConstPtr asReadyAndroidDevice(const IDeviceConstPtr &device)
{
    if (!device || device->type() != Constants::ANDROID_DEVICE_TYPE
        || device->deviceState() != IDevice::DeviceReadyToUse)
        return {};
    return std::dynamic_pointer_cast<const AndroidDevice>(device);
}

//LogcatStream

class LogcatStream : public QObject
{
public:
    LogcatStream(AndroidDeviceConstPtr device)
        : m_device(std::move(device))
    {}
    ~LogcatStream() override { stopAdbTail(); }
    Id deviceId() const { return m_device->id(); }
    QString serial() const { return m_device->serialNumber(); }
    QString tabLabel() const;

    void startAdbTail();
    void stopAdbTail();

    RunControl *tab() const { return m_tabContext.tab; }
    void setTab(RunControl *tab);

private:
    struct TabContext
    {
        QPointer<RunControl> tab;
    };

    void parseLine(const QString &raw);

    const AndroidDeviceConstPtr m_device;
    std::unique_ptr<QTaskTree> m_task;
    TabContext m_tabContext;
};

static QHash<Id, LogcatStream *> &streamRegistry()
{
    static QHash<Id, LogcatStream *> map;
    return map;
}

QString LogcatStream::tabLabel() const
{
    return deviceLabel(m_device);
}

void LogcatStream::setTab(RunControl *tab)
{
    m_tabContext = {};
    m_tabContext.tab = tab;
    if (!tab) {
        const Id id = deviceId();
        stopAdbTail();
        streamRegistry().remove(id);
        return;
    }
    QObject::connect(tab, &QObject::destroyed, this, [this] {
        setTab(nullptr);
        deleteLater();
    });
}

void LogcatStream::startAdbTail()
{
    const QString serialNumber = serial();
    const auto onSetup = [this, serialNumber](Process &process) {
        process.setStdOutLineCallback([this](const QString &line) { parseLine(line); });
        process.setStdErrLineCallback([this](const QString &line) {
            if (m_tabContext.tab)
                m_tabContext.tab->postMessage(line, Utils::StdErrFormat, false);
        });
        // -T 1 starts the tail at the current head, skipping the device's
        // existing ring buffer (live tail only), no destructive 'logcat -c'.
        process.setCommand(adbLogcat(serialNumber, {"-T", "1", "-v", "color", "-v", "brief"}));
    };
    m_task = std::make_unique<QTaskTree>(Group{Forever{ProcessTask(onSetup) || successItem}});
    m_task->start();
}

void LogcatStream::stopAdbTail()
{
    if (m_task)
        m_task.reset();
}

void LogcatStream::parseLine(const QString &raw)
{
    if (!m_tabContext.tab)
        return;
    const auto match = regExpLogcat.match(raw);
    const auto level = match.hasMatch() ? match.captured(1).at(0) : QChar();

    auto line = raw;
    if (match.hasMatch()) {
        //strip pid so the rendered line is "level/tag: message" for now
        const auto from = line.indexOf(QLatin1Char('('));
        const auto to = line.indexOf(QLatin1Char(')'), from);
        if (from >= 0 && to > from)
            line.remove(from, to - from + 1);
    }
    const auto isError = level == QLatin1Char('W') || level == QLatin1Char('E')
                         || level == QLatin1Char('F');
    m_tabContext.tab->postMessage(line, isError ? Utils::StdErrFormat : Utils::StdOutFormat, false);
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
    runControl->setDisplayName(logcatTitle(logcatStream->tabLabel()));

    logcatStream->setTab(runControl);
    logcatStream->startAdbTail();

    QPointer<RunControl> rcPtr = runControl;
    rcPtr->setRunRecipe(QBarrierTask([](QBarrier &) {}).withCancel([rcPtr] {
        return makeObjectSignal(rcPtr.data(), &RunControl::canceled);
    }));
    rcPtr->start();
    return rcPtr;
}

static RunControl *ensureVisibleTab(const AndroidDeviceConstPtr &device)
{
    auto *stream = ensureStream(device);
    if (!stream)
        return nullptr;
    return openLogcatTabForStream(stream);
}

//Menu wiring

// Tools > Android > Logcat is a submenu listing every ready Android device;
// rebuilt on demand so connected/disconnected transitions show up immediately.
static void populateLogcatSubmenu(QMenu *menu)
{
    qDeleteAll(menu->actions());
    DeviceManager::forEachDevice([menu](const IDeviceConstPtr &device) {
        const auto androidDev = asReadyAndroidDevice(device);
        if (!androidDev)
            return;
        menu->addAction(deviceLabel(androidDev), menu, [androidDev] { ensureVisibleTab(androidDev); });
    });
    if (menu->isEmpty()) {
        QAction *const placeholder = menu->addAction(Tr::tr("No Android device connected"));
        placeholder->setEnabled(false);
    }
}

//Public API

void initAndroidLogcat()
{
    ActionContainer *const logcatMenu = ActionManager::createMenu("Android.Tools.Logcat");
    logcatMenu->menu()->setTitle(Tr::tr("Logcat"));
    logcatMenu->setOnAllDisabledBehavior(ActionContainer::Show);

    ActionContainer *const parent = ActionManager::actionContainer(Constants::ANDROID_TOOLS_MENU_ID);
    if (!parent)
        return;
    parent->addMenu(logcatMenu);
    QObject::connect(parent->menu(), &QMenu::aboutToShow, parent->menu(), [logcatMenu] {
        populateLogcatSubmenu(logcatMenu->menu());
    });
}

} // namespace Android::Internal
