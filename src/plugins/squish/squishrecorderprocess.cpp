// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "squishrecorderprocess.h"

#include "scripthelper.h"

#include <coreplugin/documentmanager.h>

#include <utils/qtcassert.h>
#include <utils/temporaryfile.h>

#include <QLoggingCategory>

static Q_LOGGING_CATEGORY(recLog, "qtc.squish.recorder", QtWarningMsg)

namespace Squish::Internal {

SquishRecorderProcess::SquishRecorderProcess(QObject *parent)
    : SquishRunnerProcessBase{parent}
    , m_suiteConf{Utils::FilePath{}}
{
    m_process.setProcessMode(Utils::ProcessMode::Writer);
}

void SquishRecorderProcess::start(const Utils::FilePath &runnerFilePath,
                                  const Utils::Environment &environment)
{
    QTC_ASSERT(m_process.state() == QProcess::NotRunning, return);
    QTC_ASSERT(m_autId != -1, return);
    QTC_ASSERT(m_serverPort != -1, return);
    QTC_ASSERT(!m_suiteConf.suiteName().isEmpty(), return);
    QTC_ASSERT(!m_testName.isEmpty(), return);

    // prevent crashes on fast re-use
    m_process.close();

    QStringList args;
    if (!m_serverHost.isEmpty())
        args << "--host" << m_serverHost;
    args << "--port" << QString::number(m_serverPort);
    args << "--debugLog" << "alpw"; // TODO make this configurable?
    args << "--record";
    args << "--suitedir" << m_suiteConf.suitePath().toUserOutput();

    Utils::TemporaryFile tmp("squishsnippetfile-XXXXXX"); // quick and dirty
    tmp.open();
    m_snippetFile = Utils::FilePath::fromUserInput(tmp.fileName());
    args << "--outfile" << m_snippetFile.toUserOutput();
    tmp.close();

    args << "--lang" << m_suiteConf.langParameter();
    args << "--useWaitFor" << "--recordStart";
    if (m_suiteConf.objectMapStyle() == "script")
        args << "--useScriptedObjectMap";
    args << "--autid" << QString::number(m_autId);

    m_process.setCommand({runnerFilePath, args});
    qCDebug(recLog) << "Recorder starting:" << m_process.commandLine().toUserOutput();
    m_process.setEnvironment(environment);
    m_process.start();
}

void SquishRecorderProcess::stopRecorder(bool discardResult)
{
    QTC_ASSERT(m_process.isRunning(), return);
    const QString cmd = QLatin1String(discardResult ? "exit" : "endrecord");
    qCDebug(recLog) << QString("Stopping recorder (%1)").arg(cmd);
    m_process.write(cmd + '\n');
}

void SquishRecorderProcess::onDone()
{
    qCDebug(recLog) << "Recorder finished:" << m_process.exitCode();
//    if (m_runnerProcess.isRunning()) {
//        if (m_closeRunnerOnEndRecord) {
//            //terminateRunner();
//            m_runnerProcess.write("exit\n"); // why doesn't work anymore?
//        }
//    } else {
//        m_request = ServerStopRequested;
//        qCInfo(LOG) << "Stop Server from recorder";
//        stopSquishServer();
//    }
    emit stateChanged(Stopped);

    if (!m_snippetFile.exists()) {
        qCInfo(recLog) << m_snippetFile.toUserOutput() << "does not exist";
        return;
    }
    qCInfo(recLog).noquote() << "\nSnippetFile content:\n--------------------\n"
                          << m_snippetFile.fileContents().value_or(QByteArray())
                          << "--------------------";

    const ScriptHelper helper(m_suiteConf.language());
    const Utils::FilePath testFile = m_suiteConf.suitePath()
            .pathAppended(m_testName + "/test" + m_suiteConf.scriptExtension());
    Core::DocumentManager::expectFileChange(testFile);
    bool result = helper.writeScriptFile(testFile, m_snippetFile,
                                         m_suiteConf.aut(), m_suiteConf.arguments());
    qCInfo(recLog) << "Wrote recorded test case" << testFile.toUserOutput() << " " << result;
    m_snippetFile.removeFile();
    m_snippetFile.clear();
    // reset rest for re-use
    m_suiteConf = SuiteConf{Utils::FilePath{}};
    m_testName.clear();
    m_serverHost.clear();
    m_serverPort = -1;
    m_autId = -1;
}

} // namespace Squish::Internal
