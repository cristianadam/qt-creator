// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmlformatsettings.h"
#include "utils/filepath.h"

#include <QtCore/qtemporarydir.h>
#include <qmljs/qmljsmodelmanagerinterface.h>
#include <QStandardPaths>

using namespace QtSupport;
using namespace Utils;
using namespace ProjectExplorer;
namespace QmlJSTools {

using namespace std::chrono_literals;
constexpr std::chrono::seconds qmlformatTimeout = 30s;

// TODO: Borrowed from qmllsclientsettings.cpp
// Unify them.
void QmlFormatSettings::evaluateLatestQmlFormat()
{
    if (!QtVersionManager::isLoaded())
        return;
    const QtVersions versions = QtVersionManager::versions();
    FilePath latestQmakeFilePath;
    int latestUniqueId = std::numeric_limits<int>::min();
    for (QtVersion *v : versions) {
        QVersionNumber vNow = v->qtVersion();
        FilePath qmlformatNow = QmlJS::ModelManagerInterface::qmlformatBinPath(v->hostBinPath(), vNow);
        if (!qmlformatNow.isExecutableFile())
            continue;
        if (latestVersion > vNow)
            continue;
        FilePath qmakeNow = v->qmakeFilePath();
        int uniqueIdNow = v->uniqueId();
        if (latestVersion == vNow) {
            if (latestQmakeFilePath > qmakeNow)
                continue;
            if (latestQmakeFilePath == qmakeNow && latestUniqueId >= v->uniqueId())
                continue;
        }
        latestVersion = vNow;
        latestQmlFormat = qmlformatNow;
        latestQmakeFilePath = qmakeNow;
        latestUniqueId = uniqueIdNow;
    }

    emit versionEvaluated();
}

QmlFormatSettings::QmlFormatSettings()
{
    connect(QtVersionManager::instance(), &QtVersionManager::qtVersionsLoaded,
            this, &QmlFormatSettings::evaluateLatestQmlFormat);
}

QmlFormatSettings& QmlFormatSettings::instance()
{
    static QmlFormatSettings instance;
    return instance;
}

Utils::FilePath QmlFormatSettings::globalQmlFormatIniFile()
{
    return Utils::FilePath::fromString(
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/qmlformat.ini") ;
}

Utils::FilePath QmlFormatSettings::currentQmlFormatIniFile(const Utils::FilePath &path)
{
    QDir dir = path.isDir() ? path.toUrlishString() : path.parentDir().toUrlishString();
    const QLatin1String settingsFileName(".qmlformat.ini");
    while (dir.exists() && dir.isReadable()) {
        const QString iniFile = dir.absoluteFilePath(settingsFileName);
        if (QFileInfo::exists(iniFile)) {
            return Utils::FilePath::fromString(iniFile);
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return globalQmlFormatIniFile();
}

Utils::CommandLine QmlFormatSettings::qmlformatCommandLine() const
{
    Utils::CommandLine cmd(latestQmlFormat);
    return cmd;
}

Utils::FilePath QmlFormatSettings::latestQmlFormatPath() const
{
    return latestQmlFormat;
}

QVersionNumber QmlFormatSettings::latestQmlFormatVersion() const
{
    return latestVersion;
}

std::optional<QByteArray> QmlFormatSettings::generateQmlFormatIniContent()
{
    QTemporaryDir tempDir;
    const Utils::FilePath qmlformatIniFile = Utils::FilePath::fromString(
        tempDir.path() + "/.qmlformat.ini");
    Utils::CommandLine cmd = QmlFormatSettings::instance().qmlformatCommandLine();
    cmd.addArg("--write-defaults");
    QmlFormatProcess &qmlformat = QmlFormatProcess::instance();
    qmlformat.setWorkingDirectory(Utils::FilePath::fromString(tempDir.path()));
    qmlformat.setCommandLine(cmd);
    const QmlFormatProcess::QmlFormatResult result = qmlformat.run();
    if (result != QmlFormatProcess::QmlFormatResult::Success) {
        qWarning() << "Failed to generate QmlFormat ini file:" << qmlformat.cmd().executable();
        return std::nullopt;
    }

    return qmlformatIniFile.exists() ? std::optional<QByteArray>(*qmlformatIniFile.fileContents())
                                     : std::nullopt;
}

QmlFormatProcess::QmlFormatProcess()
    : m_logFile("qmlformat.qtc.log")
{
    m_logFile.setAutoRemove(false);
    m_logFile.open();

    if (m_process) {
        QTC_CHECK(!m_process->isRunning());
        delete m_process;
    }
    m_process = new Process;
    m_process->setProcessMode(ProcessMode::Writer);
}

QmlFormatProcess::~QmlFormatProcess()
{
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished();
        delete m_process;
    }
}

QmlFormatProcess::QmlFormatResult QmlFormatProcess::run()
{
    if (!m_process)
        return QmlFormatResult::Failed;

    if (m_cmd.executable().isEmpty()) {
        qWarning() << "No executable set for QmlFormatProcess";
        return QmlFormatResult::Failed;
    }
    m_process->setCommand(m_cmd);
    m_process->setWorkingDirectory(m_workingDirectory);
    m_process->runBlocking(qmlformatTimeout);
    const auto result = m_process->result();
    if (result != ProcessResult::FinishedWithSuccess) {
        qWarning() << "QmlFormatProcess failed:" << m_process->exitMessage();
        m_logFile.write(m_process->rawStdErr());
        return QmlFormatResult::Failed;
    }
    return QmlFormatResult::Success;
}

void QmlFormatProcess::setWorkingDirectory(const Utils::FilePath &workingDirectory)
{
    m_workingDirectory = workingDirectory;
}

Utils::FilePath QmlFormatProcess::workingDirectory() const
{
    return m_workingDirectory;
}

void QmlFormatProcess::setCommandLine(const Utils::CommandLine &cmd)
{
    m_cmd = cmd;
}

Utils::CommandLine QmlFormatProcess::cmd() const
{
    return m_cmd;
}

QmlFormatProcess& QmlFormatProcess::instance()
{
    static QmlFormatProcess instance;
    return instance;
}

} // namespace QmlJSTools
