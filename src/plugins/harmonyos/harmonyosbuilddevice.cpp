// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "harmonyosbuilddevice.h"

#include "harmonyosconstants.h"
#include "harmonyossdk.h"
#include "harmonyossettings.h"
#include "harmonyostr.h"

#include <coreplugin/icore.h>

#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/devicesupport/sshparameters.h>

#include <remote/sshdevicewizard.h>

#include <utils/environment.h>
#include <utils/fileutils.h>
#include <utils/qtcprocess.h>
#include <utils/temporarydirectory.h>

#include <QDialog>
#include <QLoggingCategory>
#include <QHostAddress>
#include <QTcpSocket>

using namespace ProjectExplorer;
using namespace Utils;
using namespace std::chrono_literals;

namespace HarmonyOs::Internal {

static Q_LOGGING_CATEGORY(buildDeviceLog, "qtc.harmonyos.builddevice", QtWarningMsg)

HarmonyOsBuildDevice::HarmonyOsBuildDevice()
{
    setType(Constants::HARMONYOS_BUILD_DEVICE_TYPE);
    setDisplayType(Tr::tr("HarmonyOS Build Device"));
    setDefaultDisplayName(Tr::tr("HarmonyOS Build Device"));

    // The toolchains there are ordinary Linux ones, and everything that looks for
    // them expects a Linux device.
    setOsType(OsTypeLinux);

    SshParameters sshParams = sshParameters();
    sshParams.setPort(Constants::HARMONYOS_SSH_PORT);
    setDefaultSshParameters(sshParams);
}

// A binary reaching the device unsigned is refused by its code signing, and one
// signed twice is refused as well, so an already signed binary is left alone.
Result<QByteArray> HarmonyOsBuildDevice::prepareExecutableForUpload(const QByteArray &binary) const
{
    const FilePath signTool = Sdk::binarySignTool(settings().sdkLocation());
    if (signTool.isEmpty()) {
        // Uploading it unsigned would leave a bridge that cannot run, which the
        // caller cannot tell from one that works.
        return ResultError(Tr::tr("No HarmonyOS SDK is configured to sign the bridge with. "
                                  "Set it up in Preferences > SDKs > HarmonyOS."));
    }

    TemporaryDirectory directory("qtc-harmonyos-sign");
    if (!directory.isValid())
        return ResultError(directory.errorString());
    const FilePath unsignedFile = directory.filePath("unsigned");
    const FilePath signedFile = directory.filePath("signed");
    if (const Result<qint64> written = unsignedFile.writeFileContents(binary); !written)
        return ResultError(written.error());

    struct SignToolRun
    {
        bool succeeded = false;
        QString output;
    };
    const auto runSignTool = [signTool](const QStringList &arguments) {
        Process process;
        process.setCommand({signTool, arguments});
        process.runBlocking(30s);
        return SignToolRun{process.result() == ProcessResult::FinishedWithSuccess,
                           process.allOutput()};
    };

    // The marker decides, not the exit status: the tool may well report a missing
    // signature as a failure. But a run that neither found the marker nor
    // succeeded says nothing, and reading that as "already signed" would upload
    // the unsigned binary this function exists to prevent.
    const SignToolRun state = runSignTool({"display-sign", "-inFile", unsignedFile.nativePath()});
    if (!state.output.contains("code signature is not found")) {
        if (!state.succeeded) {
            return ResultError(Tr::tr("Could not tell whether the bridge is signed: %1")
                                   .arg(state.output.trimmed()));
        }
        return binary;
    }

    const SignToolRun signing = runSignTool({"sign", "-selfSign", "1",
                                             "-inFile", unsignedFile.nativePath(),
                                             "-outFile", signedFile.nativePath()});
    if (!signedFile.exists())
        return ResultError(Tr::tr("Failed to sign the bridge: %1").arg(signing.output.trimmed()));
    return signedFile.fileContents();
}

#ifdef Q_OS_OHOS

// What the platform installs from an application's native package is the only thing it may
// execute, and nothing searches there. Appended rather than prepended: it is where a tool
// is found when nothing else provides it, not a way to override what does.
static void addNativePackageToPath()
{
    const FilePath bin = FilePath::fromString(Constants::HARMONYOS_NATIVE_PACKAGE_BIN);
    if (Environment::systemEnvironment().pathListValue("PATH").contains(bin))
        return;
    Environment::modifySystemEnvironment(
        {{"PATH", bin.path(), EnvironmentItem::Append}});
}

// Qt Creator running on a HarmonyOS device is one process boundary away from a toolchain.
// Nothing in its own sandbox may execute a compiler - the platform refuses to execute any
// file that did not arrive in an installed package - but the terminal application's SSH
// server on loopback answers, and its environment holds a native clang and cmake. That
// connection is how anything gets built here, so the device for it is worth finding rather
// than asking for: everything about it is known except whether something is listening.
static bool somethingSpeaksSshOnLoopback()
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, Constants::HARMONYOS_SSH_PORT);
    if (!socket.waitForConnected(1000) || !socket.waitForReadyRead(2000))
        return false;
    return socket.readAll().startsWith("SSH-");
}

// The server is reached with a key, and ssh has a place for those: the one it looks in
// by default, so nothing has to be configured and the file is where anyone would look for
// it. ssh-keygen travels in the native package.
static FilePath privateKey()
{
    const FilePath key = FileUtils::homePath().pathAppended(".ssh/id_ed25519");
    if (key.exists())
        return key;
    // Not checked for existence first: what the platform installs there are symlinks into
    // a directory the application may execute from but not stat, so every such check says
    // the tool is not there while running it works.
    const FilePath keygen = FilePath::fromString(Constants::HARMONYOS_NATIVE_PACKAGE_BIN)
                                .pathAppended("ssh-keygen");
    if (const Result<> created = key.parentDir().ensureWritableDir(); !created)
        return {};

    Process process;
    process.setCommand({keygen, {"-t", "ed25519", "-q", "-N", "", "-f", key.path()}});
    process.runBlocking(30s);
    if (!key.exists())
        return {};
    return key;
}

static const char loopbackDeviceId[] = "HarmonyOs.BuildDevice.Loopback";

// Completes what is there rather than only creating what is not: a device from an earlier
// run whose key never got written would otherwise stay unusable forever.
static void detectLoopbackBuildDevice()
{
    IDevice::Ptr device;
    for (int i = 0, n = DeviceManager::deviceCount(); i < n; ++i) {
        const IDevice::Ptr known = DeviceManager::deviceAt(i);
        if (!known || known->type() != Constants::HARMONYOS_BUILD_DEVICE_TYPE)
            continue;
        if (known->id() != Utils::Id(loopbackDeviceId))
            return;                     // somebody configured their own, leave it alone
        device = known;
    }
    if (!device && !somethingSpeaksSshOnLoopback())
        return;

    const bool isNew = !device;
    if (isNew)
        device = HarmonyOsBuildDevice::create();

    SshParameters parameters = device->sshParameters();
    if (!parameters.privateKeyFile().isEmpty() && parameters.privateKeyFile().exists())
        return;                         // set up already

    parameters.setHost("127.0.0.1");
    parameters.setPort(Constants::HARMONYOS_SSH_PORT);
    // That server takes any name and runs the session as the application it belongs to,
    // so there is nothing to ask the user for.
    parameters.setUserName("device");
    const FilePath key = privateKey();
    if (key.isEmpty())
        return;
    parameters.setPrivateKeyFile(key);
    parameters.setAuthenticationType(SshParameters::AuthenticationTypeSpecificKey);
    DeviceRef(device).setSshParameters(parameters);

    if (isNew) {
        device->setupId(IDevice::AutoDetected, loopbackDeviceId);
        device->setDisplayName(Tr::tr("HarmonyOS Build Device (this device)"));
        DeviceManager::addDevice(device);
    }
    // The server has to be told to accept this key, which is not this plugin's to do
    // silently: it lives in another application's configuration.
    qCWarning(buildDeviceLog) << "Build device on loopback ready to authorise; add"
                              << key.stringAppended(".pub").path()
                              << "to ~/.ssh/authorized_keys on this device.";
}

#endif // Q_OS_OHOS

HarmonyOsBuildDeviceFactory::HarmonyOsBuildDeviceFactory()
    : IDeviceFactory(Constants::HARMONYOS_BUILD_DEVICE_TYPE)
{
    setDisplayName(Tr::tr("HarmonyOS Build Device"));
    setQuickCreationAllowed(true);
    setConstructionFunction([] { return HarmonyOsBuildDevice::create(); });
    setCreator([]() -> IDevice::Ptr {
        const HarmonyOsBuildDevice::Ptr device = HarmonyOsBuildDevice::create();
        Remote::SshDeviceWizard wizard(Tr::tr("New HarmonyOS Build Device Configuration Setup"),
                                       IDevice::Ptr(device));
        if (wizard.exec() != QDialog::Accepted)
            return {};
        return device;
    });
}

void setupHarmonyOsBuildDevice()
{
    static HarmonyOsBuildDeviceFactory theHarmonyOsBuildDeviceFactory;
#ifdef Q_OS_OHOS
    addNativePackageToPath();
    // Adding a device before the saved ones are restored loses it: the restore replaces
    // the list. By the time this plugin is initialized they may or may not be there yet.
    if (DeviceManager::isLoaded()) {
        detectLoopbackBuildDevice();
    } else {
        QObject::connect(DeviceManager::instance(), &DeviceManager::devicesLoaded,
                         DeviceManager::instance(), &detectLoopbackBuildDevice,
                         Qt::SingleShotConnection);
    }
#endif
}

} // namespace HarmonyOs::Internal
