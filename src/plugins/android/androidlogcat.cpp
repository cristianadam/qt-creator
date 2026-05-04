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

// User-facing device identity: "<displayName> (<serial>)". Used in
// the Tools > Android > Logcat submenu and composed into the tab title.
static QString deviceLabel(const AndroidDeviceConstPtr &device)
{
    return QString("%1 (%2)").arg(device->displayName(), device->serialNumber());
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
    ~LogcatStream() override;
    Id deviceId() const { return m_device->id(); }
    QString serial() const { return m_device->serialNumber(); }

    void startAdbTail();
    void stopAdbTail();

    RunControl *tab() const { return m_tabContext.tab; }
    void attachTab(RunControl *tab);
    void setTabActive(bool active);

private:
    struct TabContext
    {
        QPointer<RunControl> tab;
        bool active = false;
    };

    void onTabDestroyed();

    const AndroidDeviceConstPtr m_device;
    std::unique_ptr<QTaskTree> m_task;
    TabContext m_tabContext;
};

static QHash<Id, LogcatStream *> &streamRegistry()
{
    static QHash<Id, LogcatStream *> map;
    return map;
}

LogcatStream::~LogcatStream()
{
    stopAdbTail();
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
    QObject::connect(tab, &QObject::destroyed, this, [this] { onTabDestroyed(); });
}

// The stream's lifetime follows its tab: once the tab goes away, stop the
// adb tail, leave the registry, and self-destruct.
void LogcatStream::onTabDestroyed()
{
    m_tabContext = {};
    stopAdbTail();
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
        startAdbTail();
    else
        stopAdbTail();
}

void LogcatStream::startAdbTail()
{
    const QString serialNumber = serial();
    const auto onSetup = [this, serialNumber](Process &process) {
        const auto post = [this](const QString &line, Utils::OutputFormat fmt) {
            if (m_tabContext.tab)
                m_tabContext.tab->postMessage(line, fmt, false);
        };
        process.setStdOutLineCallback([post](const QString &line) { post(line, Utils::StdOutFormat); });
        process.setStdErrLineCallback([post](const QString &line) { post(line, Utils::StdErrFormat); });
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
