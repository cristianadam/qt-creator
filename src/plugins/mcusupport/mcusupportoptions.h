// Copyright  (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include "mcukitmanager.h"
#include "mcusupport_global.h"
#include "settingshandler.h"

#include <utils/environmentfwd.h>

#include <QObject>
#include <QVersionNumber>

QT_FORWARD_DECLARE_CLASS(QWidget)

namespace Utils {
class FilePath;
class PathChooser;
class InfoLabel;
} // namespace Utils

namespace ProjectExplorer {
class Kit;
class ToolChain;
} // namespace ProjectExplorer

namespace McuSupport {
namespace Internal {

class McuAbstractPackage;

class McuSdkRepository final
{
public:
    Targets mcuTargets;
    Packages packages;
};

class McuSupportOptions final : public QObject
{
    Q_OBJECT

public:
    explicit McuSupportOptions(const SettingsHandler::Ptr &, QObject *parent = nullptr);

    McuPackagePtr qtForMCUsSdkPackage{nullptr};
    McuSdkRepository sdkRepository;

    void setQulDir(const Utils::FilePath &dir);
    Utils::FilePath qulDirFromSettings() const;
    Utils::FilePath qulDocsDir() const;
    static McuKitManager::UpgradeOption askForKitUpgrades();

    void registerQchFiles();
    void registerExamples();

    static const QVersionNumber &minimalQulVersion();
    static bool isLegacyVersion(const QVersionNumber &version);

    void checkUpgradeableKits();
    void populatePackagesAndTargets();

    static bool kitsNeedQtVersion();

    bool automaticKitCreationEnabled() const;
    void setAutomaticKitCreationEnabled(const bool enabled);

private:
    SettingsHandler::Ptr settingsHandler;

    bool m_automaticKitCreation = true;
signals:
    void packagesChanged();
};

} // namespace Internal
} // namespace McuSupport
