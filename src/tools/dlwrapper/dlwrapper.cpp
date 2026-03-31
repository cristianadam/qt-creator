// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <utils/commandline.h>
#include <utils/filestreamer.h>
#include <utils/temporaryfile.h>
#include <utils/unarchiver.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QSocketNotifier>

#include <QtTaskTree/qtasktree.h>
#include <QtTaskTree/qprocesstask.h>

using namespace QtTaskTree;
using namespace Utils;

Q_LOGGING_CATEGORY(dlWrapper, "qtc.dlwrapper", QtWarningMsg)

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    a.setApplicationName("Qt Creator Download Wrapper");
    a.setOrganizationName("Qt");

    QCommandLineParser commandLineParser;
    commandLineParser.setApplicationDescription("Downloads and executes a command from an archive.");
    commandLineParser.addHelpOption();
    commandLineParser.setOptionsAfterPositionalArgumentsMode(
        QCommandLineParser::ParseAsPositionalArguments);
    commandLineParser.addOption(
        QCommandLineOption("env", "Environment Variable values to set", "env"));
    commandLineParser.addOption(QCommandLineOption(
        "download", "The archive to download the command to run from.", "download"));
    commandLineParser
        .addPositionalArgument("executable", "The executable relative to the extracted archive root");

    commandLineParser.process(a);

    const FilePath tempDir = FilePath::fromUserInput(QDir::tempPath());
    const FilePath archiveUrl = FilePath::fromUserInput(commandLineParser.value("download"));
    CommandLine cmdLine{
        FilePath::fromUserInput(commandLineParser.positionalArguments().value(0)),
        commandLineParser.positionalArguments().mid(1)};

    QStringList envVars = commandLineParser.values("env");
    if (!envVars.isEmpty())
        qCDebug(dlWrapper) << "Environment variables to set:" << envVars;

    if (archiveUrl.isEmpty() || cmdLine.isEmpty()) {
        qCWarning(dlWrapper).noquote() << commandLineParser.helpText();
        return 1;
    }

    QTaskTree taskTree;
    Storage<std::unique_ptr<TemporaryFilePath>> tempArchive;
    Storage<std::unique_ptr<TemporaryFilePath>> tempExtractedDir;

    const auto setupDownload = [tempDir, archiveUrl, tempArchive](FileStreamer &task) {
        task.setSource(archiveUrl);

        auto tmpFileResult = TemporaryFilePath::create(tempDir / archiveUrl.fileName());
        if (!tmpFileResult) {
            qCWarning(dlWrapper) << "Failed to create temporary file:" << tmpFileResult.error();
            return SetupResult::StopWithError;
        }
        *tempArchive = std::move(*tmpFileResult);

        qCDebug(dlWrapper) << "Downloading" << archiveUrl << "to temporary file"
                           << (*tempArchive)->filePath();

        task.setDestination((*tempArchive)->filePath());
        task.setStreamMode(StreamMode::Transfer);
        return SetupResult::Continue;
    };

    const auto setupUnarchive = [tempDir, archiveUrl, tempArchive, tempExtractedDir](
                                    Unarchiver &task) {
        task.setArchive((*tempArchive)->filePath());

        auto tmpDirResult = TemporaryFilePath::create(tempDir / archiveUrl.baseName(), true);
        if (!tmpDirResult) {
            qCWarning(dlWrapper) << "Failed to create temporary directory:" << tmpDirResult.error();
            return SetupResult::StopWithError;
        }
        *tempExtractedDir = std::move(*tmpDirResult);
        task.setDestination((*tempExtractedDir)->filePath());
        qCDebug(dlWrapper) << "Extracting archive to temporary directory"
                           << (*tempExtractedDir)->filePath();
        return SetupResult::Continue;
    };

    const auto setupProcess = [&](QProcess &process) {
        CommandLine c = cmdLine;
        c.setExecutable((*tempExtractedDir)->filePath() / cmdLine.executable().toUrlishString());
        process.setProgram(c.executable().toFSPathString());
        process.setArguments(c.splitArguments());
        process.setWorkingDirectory((*tempExtractedDir)->filePath().toFSPathString());
        process.setInputChannelMode(QProcess::ForwardedInputChannel);
        process.setProcessChannelMode(QProcess::ForwardedChannels);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const QString &envVar : envVars) {
            const int eqIdx = envVar.indexOf('=');
            if (eqIdx > 0) {
                const QString name = envVar.left(eqIdx);
                const QString value = envVar.mid(eqIdx + 1);
                env.insert(name, value);
                qCDebug(dlWrapper) << "Setting environment variable:" << name << "=" << value;
            } else {
                qCWarning(dlWrapper)
                    << "Invalid environment variable format (expected NAME=VALUE):" << envVar;
            }
        }
        process.setProcessEnvironment(env);
        qCDebug(dlWrapper) << "Executing command" << c.toUserOutput() << "in directory"
                           << (*tempExtractedDir)->filePath();
    };

    const auto processDone = [&](const QProcess &process, DoneWith doneWith) {
        if (doneWith != DoneWith::Success)
            qCWarning(dlWrapper).noquote() << process.errorString();
    };

    // clang-format off
    Group recipe {
        tempArchive,
        tempExtractedDir,
        FileStreamerTask(setupDownload),
        UnarchiverTask(setupUnarchive),
        QProcessTask(setupProcess, processDone),
        onGroupDone([&](DoneWith doneWith){
            a.exit(doneWith == DoneWith::Success ? 0 : 1);
        })
    };
    // clang-format on

    taskTree.setRecipe(recipe);
    taskTree.start();

    return a.exec();
}
