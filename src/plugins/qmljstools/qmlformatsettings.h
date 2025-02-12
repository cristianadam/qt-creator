// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qmljstools_global.h"
#include <utils/commandline.h>
#include <utils/filepath.h>
#include <utils/qtcprocess.h>
#include <utils/temporaryfile.h>
#include <QtCore/qobject.h>
#include <qtsupport/qtversionmanager.h>
namespace QmlJSTools {
class QMLJSTOOLS_EXPORT QmlFormatSettings : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(QmlFormatSettings)
public:
    static QmlFormatSettings &instance();
    Utils::FilePath latestQmlFormatPath() const;
    QVersionNumber latestQmlFormatVersion() const;
    Utils::CommandLine qmlformatCommandLine() const;

    static Utils::FilePath currentQmlFormatIniFile(const Utils::FilePath &path);
    static Utils::FilePath globalQmlFormatIniFile();
    static std::optional<QByteArray> generateQmlFormatIniContent();

signals:
    void versionEvaluated();
private:
    void evaluateLatestQmlFormat();
    QmlFormatSettings();
    Utils::FilePath latestQmlFormat;
    QVersionNumber latestVersion;
};

class QMLJSTOOLS_EXPORT QmlFormatProcess : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(QmlFormatProcess)
public:
    enum class QmlFormatResult : quint8{
        Success,
        Failed
    };
    static QmlFormatProcess &instance();
    ~QmlFormatProcess();
    void setWorkingDirectory(const Utils::FilePath &workingDirectory);
    Utils::FilePath workingDirectory() const;

    void setCommandLine(const Utils::CommandLine &cmd);
    Utils::CommandLine cmd() const;

    QmlFormatResult run();

private:
    QmlFormatProcess();

signals:
    void finished(int exitCode, QProcess::ExitStatus exitStatus);
private:
    Utils::Process *m_process = nullptr;
    Utils::FilePath m_workingDirectory;
    Utils::CommandLine m_cmd;
    Utils::TemporaryFile m_logFile;
};

} // namespace QmlJSTools
