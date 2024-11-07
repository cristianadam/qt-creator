// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "runmanager.h"

#include <projectexplorer/kitmanager.h>
#include <projectexplorer/projectexplorer.h>
#include <qmldesigner/qmldesignerplugin.h>

#include <resourcegeneratorproxy.h>

namespace QmlDesigner {

namespace {
// helper type for the visitor
template<class... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};

} // namespace

RunManager::RunManager(DeviceShare::DeviceManager &deviceManager)
    : QObject()
    , m_deviceManager(deviceManager)
{
    // Connect DeviceManager with internal target generation
    connect(&m_deviceManager, &DeviceShare::DeviceManager::deviceAdded, this, &RunManager::udpateTargets);
    connect(&m_deviceManager,
            &DeviceShare::DeviceManager::deviceRemoved,
            this,
            &RunManager::udpateTargets);
    connect(&m_deviceManager,
            &DeviceShare::DeviceManager::deviceActivated,
            this,
            &RunManager::udpateTargets);
    connect(&m_deviceManager,
            &DeviceShare::DeviceManager::deviceDeactivated,
            this,
            &RunManager::udpateTargets);
    connect(&m_deviceManager,
            &DeviceShare::DeviceManager::deviceAliasChanged,
            this,
            &RunManager::udpateTargets);

    // TODO If device going offline is currently running force stop

    // Connect Android/Device run/stop project signals
    connect(&m_deviceManager,
            &DeviceShare::DeviceManager::projectStarted,
            this,
            [this](const DeviceShare::DeviceInfo &info) {
                qDebug() << Q_FUNC_INFO << "Project started." << info;

                m_runningTargets.append(info.deviceId());

                m_state = TargetState::Running;
                emit stateChanged();
            });
    connect(&m_deviceManager,
            &DeviceShare::DeviceManager::projectStopped,
            this,
            [this](const DeviceShare::DeviceInfo &info) {
                qDebug() << Q_FUNC_INFO << "Project stopped." << info;

                auto findRunningTarget = [&](const auto &runningTarget) {
                    return std::visit(overloaded{[](const QPointer<ProjectExplorer::RunControl>) {
                                                     return false;
                                                 },
                                                 [&](const QString &arg) {
                                                     return arg == info.deviceId();
                                                 }},
                                      runningTarget);
                };

                const auto [first, last] = std::ranges::remove_if(m_runningTargets, findRunningTarget);
                m_runningTargets.erase(first, last);

                if (!m_runningTargets.isEmpty())
                    return;

                m_state = TargetState::NotRunning;
                emit stateChanged();
            });

    // Connect Creator run/stop project signals
    auto projectExplorerPlugin = ProjectExplorer::ProjectExplorerPlugin::instance();
    connect(projectExplorerPlugin,
            &ProjectExplorer::ProjectExplorerPlugin::runControlStarted,
            this,
            [this](ProjectExplorer::RunControl *runControl) {
                qDebug() << Q_FUNC_INFO << "Run Control started.";

                m_runningTargets.append(QPointer(runControl));

                m_state = TargetState::Running;
                emit stateChanged();
            });
    connect(projectExplorerPlugin,
            &ProjectExplorer::ProjectExplorerPlugin::runControlStoped,
            this,
            [this](ProjectExplorer::RunControl *runControl) {
                qDebug() << Q_FUNC_INFO << "Run Control stoped.";

                auto findRunningTarget = [&](const auto &runningTarget) {
                    return std::visit(overloaded{[&](const QPointer<ProjectExplorer::RunControl> arg) {
                                                     return arg.get() == runControl;
                                                 },
                                                 [](const QString &) { return false; }},
                                      runningTarget);
                };

                const auto [first, last] = std::ranges::remove_if(m_runningTargets, findRunningTarget);
                m_runningTargets.erase(first, last);

                if (!m_runningTargets.isEmpty())
                    return;

                m_state = TargetState::NotRunning;
                emit stateChanged();
            });

    udpateTargets();
}

void RunManager::udpateTargets()
{
    m_targets.clear();

    m_targets.append(NormalTarget());
    m_targets.append(LivePreviewTarget());

    for (const auto &device : m_deviceManager.devices()) {
        if (device->deviceSettings().active())
            m_targets.append(AndroidTarget(device->deviceInfo().deviceId()));
    }

    emit targetsChanged();

    bool currentTargetReset = false;
    if (m_currentTargetId.isValid())
        currentTargetReset = selectRunTarget(m_currentTargetId);

    if (!currentTargetReset) // default run target
        selectRunTarget(NormalTarget().id());
}

const QList<Target> RunManager::targets() const
{
    return m_targets;
}

void RunManager::toggleCurrentTarget()
{
    if (!m_runningTargets.isEmpty()) {
        for (auto runningTarget : m_runningTargets) {
            std::visit(overloaded{[](const QPointer<ProjectExplorer::RunControl> arg) {
                                      //QTC_ASSERT
                                      if (!arg.isNull())
                                          arg.get()->initiateStop();
                                  },
                                  [](const QString arg) {
                                      qDebug() << "toggleCurrentTarget" << arg;
                                      if (!arg.isEmpty())
                                          QmlDesignerPlugin::deviceManager().stopRunningProject(arg);
                                  }},
                       runningTarget);
        }
        return;
    }

    auto target = runTarget(m_currentTargetId);

    if (!target)
        return;

    bool enabled = std::visit([&](const auto &arg) { return arg.enabled(); }, *target);

    if (!enabled) {
        qDebug() << "Can't start run target" << m_currentTargetId << "not enabled.";
        return;
    }

    std::visit([&](const auto &arg) { arg.run(); }, *target);

    m_state = TargetState::Starting;
    emit stateChanged();
}

int RunManager::currentTargetIndex() const
{
    return runTargetIndex(m_currentTargetId);
}

bool RunManager::selectRunTarget(Utils::Id id)
{
    if (m_currentTargetId == id)
        return true;

    int index = runTargetIndex(id);

    // TODO What if targetName not available
    if (index == -1) {
        qDebug() << Q_FUNC_INFO << "Couldn't find run target" << id;
        return false;
    }

    m_currentTargetId = id;
    emit runTargetChanged();

    return true;
}

bool RunManager::selectRunTarget(const QString &targetName)
{
    return selectRunTarget(Utils::Id::fromString(targetName));
}

std::optional<Target> RunManager::runTarget(Utils::Id targetId) const
{
    auto find_id = [&](const auto &target) {
        return std::visit([&](const auto &arg) { return arg.id() == targetId; }, target);
    };

    auto result = std::ranges::find_if(m_targets, find_id);

    if (result != m_targets.end())
        return *result;

    qDebug() << "Couldn't find run target" << targetId;

    return {};
}

int RunManager::runTargetIndex(Utils::Id targetId) const
{
    auto find_id = [&](const auto &target) {
        return std::visit([&](const auto &arg) { return arg.id() == targetId; }, target);
    };

    auto result = std::ranges::find_if(m_targets, find_id);

    if (result != m_targets.end())
        return std::distance(m_targets.begin(), result);

    return -1;
}

QString NormalTarget::name() const
{
    return "Default";
}

Utils::Id NormalTarget::id() const
{
    return ProjectExplorer::Constants::NORMAL_RUN_MODE;
}

bool NormalTarget::enabled() const
{
    return bool(ProjectExplorer::ProjectExplorerPlugin::canRunStartupProject(id()));
}

void NormalTarget::run() const
{
    ProjectExplorer::ProjectExplorerPlugin::runStartupProject(id());
}

QString LivePreviewTarget::name() const
{
    return "Live Preview";
}

Utils::Id LivePreviewTarget::id() const
{
    return ProjectExplorer::Constants::QML_PREVIEW_RUN_MODE;
}

bool LivePreviewTarget::enabled() const
{
    return bool(ProjectExplorer::ProjectExplorerPlugin::canRunStartupProject(id()));
}

void LivePreviewTarget::run() const
{
    ProjectExplorer::ProjectExplorerPlugin::runStartupProject(id());
}

AndroidTarget::AndroidTarget(const QString &deviceId)
    : m_deviceId(deviceId)
{}

QString AndroidTarget::name() const
{
    if (auto devcieSettings = QmlDesignerPlugin::deviceManager().deviceSettings(m_deviceId))
        return devcieSettings->alias();

    return {};
}

Utils::Id AndroidTarget::id() const
{
    if (auto deviceInfo = QmlDesignerPlugin::deviceManager().deviceInfo(m_deviceId))
        return Utils::Id::fromString(deviceInfo->deviceId());

    return {};
}

bool AndroidTarget::enabled() const
{
    return QmlDesignerPlugin::deviceManager().deviceIsConnected(m_deviceId).value_or(false);
}

void AndroidTarget::run() const
{
    auto qmlrcPath = DesignViewer::ResourceGeneratorProxy().createResourceFileSync();
    QmlDesignerPlugin::deviceManager().sendProjectFile(m_deviceId, qmlrcPath);
}

/*
QVariant Model::data(const QModelIndex &index, int role) const
{
    if (role == TextRole)
        return std::visit([](const auto &arg) { return arg.name(); }, m_targets[index.row()]);

    if (role == ValueRole)
        return std::visit(overloaded{[](const NormalTarget arg) { return; },
                                     [](const LivePreviewTarget arg) { return; },
                                     [](const AndroidTarget arg) { return; }},
                          m_targets[index.row()]);
}
*/

} // namespace QmlDesigner
