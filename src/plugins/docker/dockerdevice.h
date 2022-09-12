// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "dockerdevicedata.h"
#include "dockersettings.h"

#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/devicesupport/idevicefactory.h>
#include <coreplugin/documentmanager.h>

#include <utils/aspects.h>

#include <QMutex>
#include <QThread>

namespace Docker::Internal {

class DockerDevice : public ProjectExplorer::IDevice
{
public:
    using Ptr = QSharedPointer<DockerDevice>;
    using ConstPtr = QSharedPointer<const DockerDevice>;

    explicit DockerDevice(DockerSettings *settings, const DockerDeviceData &data);
    ~DockerDevice();

    void shutdown();

    static Ptr create(DockerSettings *settings, const DockerDeviceData &data) { return Ptr(new DockerDevice(settings, data)); }

    ProjectExplorer::IDeviceWidget *createWidget() override;
    QList<ProjectExplorer::Task> validate() const override;

    Utils::ProcessInterface *createProcessInterface() const override;

    bool canAutoDetectPorts() const override;
    ProjectExplorer::PortsGatheringMethod portsGatheringMethod() const override;
    bool canCreateProcessModel() const override { return false; }
    ProjectExplorer::DeviceProcessList *createProcessListModel(QObject *parent) const override;
    bool hasDeviceTester() const override { return false; }
    ProjectExplorer::DeviceTester *createDeviceTester() const override;
    ProjectExplorer::DeviceProcessSignalOperation::Ptr signalOperation() const override;
    ProjectExplorer::DeviceEnvironmentFetcher::Ptr environmentFetcher() const override;

    Utils::FilePath mapToGlobalPath(const Utils::FilePath &pathOnDevice) const override;
    QString mapToDevicePath(const Utils::FilePath &globalPath) const override;

    Utils::FilePath rootPath() const override;

    bool handlesFile(const Utils::FilePath &filePath) const override;
    bool isExecutableFile(const Utils::FilePath &filePath) const override;
    bool isReadableFile(const Utils::FilePath &filePath) const override;
    bool isWritableFile(const Utils::FilePath &filePath) const override;
    bool isReadableDirectory(const Utils::FilePath &filePath) const override;
    bool isWritableDirectory(const Utils::FilePath &filePath) const override;
    bool isFile(const Utils::FilePath &filePath) const override;
    bool isDirectory(const Utils::FilePath &filePath) const override;
    bool createDirectory(const Utils::FilePath &filePath) const override;
    bool exists(const Utils::FilePath &filePath) const override;
    bool ensureExistingFile(const Utils::FilePath &filePath) const override;
    bool removeFile(const Utils::FilePath &filePath) const override;
    bool removeRecursively(const Utils::FilePath &filePath) const override;
    bool copyFile(const Utils::FilePath &filePath, const Utils::FilePath &target) const override;
    bool renameFile(const Utils::FilePath &filePath, const Utils::FilePath &target) const override;
    Utils::FilePath symLinkTarget(const Utils::FilePath &filePath) const override;
    void iterateDirectory(const Utils::FilePath &filePath,
                          const std::function<bool(const Utils::FilePath &)> &callBack,
                          const Utils::FileFilter &filter) const override;
    QByteArray fileContents(const Utils::FilePath &filePath, qint64 limit, qint64 offset) const override;
    bool writeFileContents(const Utils::FilePath &filePath, const QByteArray &data) const override;
    QDateTime lastModified(const Utils::FilePath &filePath) const override;
    qint64 fileSize(const Utils::FilePath &filePath) const override;
    QFileDevice::Permissions permissions(const Utils::FilePath &filePath) const override;
    bool setPermissions(const Utils::FilePath &filePath, QFileDevice::Permissions permissions) const override;

    bool ensureReachable(const Utils::FilePath &other) const override;

    Utils::Environment systemEnvironment() const override;

    const DockerDeviceData data() const;
    DockerDeviceData data();

    void updateContainerAccess() const;
    void setMounts(const QStringList &mounts) const;

    Utils::CommandLine withDockerExecCmd(const Utils::CommandLine& cmd, bool interactive = false) const;

protected:
    void fromMap(const QVariantMap &map) final;
    QVariantMap toMap() const final;

private:
    void iterateWithFind(const Utils::FilePath &filePath,
                         const std::function<bool(const Utils::FilePath &)> &callBack,
                         const Utils::FileFilter &filter) const;

    void aboutToBeRemoved() const final;

    class DockerDevicePrivate *d = nullptr;
    QThread m_privateThread;

    friend class DockerDeviceSetupWizard;
    friend class DockerDeviceWidget;
};


} // Docker::Internal

Q_DECLARE_METATYPE(Docker::Internal::DockerDeviceData)
