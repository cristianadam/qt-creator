// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QLoggingCategory>
#include <utils/fileinprojectfinder.h>

void createDirectoryStructure(const QString &baseDir)
{
    QDir dir(baseDir);

    // Create base directory if it doesn't exist
    if (!dir.exists()) {
        qDebug() << "Creating directory:" << baseDir;
        dir.mkpath(baseDir);
    }

    // Create files and subdirectories as per the tree structure
    QFile file(baseDir + "/App.qml");
    if (!file.exists()) {
        file.open(QIODevice::WriteOnly);
        file.close();
    }

    QFile file2(baseDir + "/Screen01.ui.qml");
    if (!file2.exists()) {
        file2.open(QIODevice::WriteOnly);
        file2.close();
    }

    QFile projectFile(baseDir + "/UntitledProject68.qmlproject");
    if (!projectFile.exists()) {
        projectFile.open(QIODevice::WriteOnly);
        projectFile.close();
    }

    QString subdir = baseDir + "/UntitledProject68";
    dir.mkpath(subdir);

    QFile constantsFile(subdir + "/Constants.qml");
    if (!constantsFile.exists()) {
        constantsFile.open(QIODevice::WriteOnly);
        constantsFile.close();
    }

    QFile eventListModelFile(subdir + "/EventListModel.qml");
    if (!eventListModelFile.exists()) {
        eventListModelFile.open(QIODevice::WriteOnly);
        eventListModelFile.close();
    }

    QFile eventListSimulatorFile(subdir + "/EventListSimulator.qml");
    if (!eventListSimulatorFile.exists()) {
        eventListSimulatorFile.open(QIODevice::WriteOnly);
        eventListSimulatorFile.close();
    }

    QFile qmldirFile(subdir + "/qmldir");
    if (!qmldirFile.exists()) {
        qmldirFile.open(QIODevice::WriteOnly);
        qmldirFile.close();
    }

    QString designerSubdir = subdir + "/designer";
    dir.mkpath(designerSubdir);

    QFile pluginMetainfoFile(designerSubdir + "/plugin.metainfo");
    if (!pluginMetainfoFile.exists()) {
        pluginMetainfoFile.open(QIODevice::WriteOnly);
        pluginMetainfoFile.close();
    }

    qDebug() << "Directory structure created.";
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QLoggingCategory::setFilterRules("qtc.utils.fileinprojectfinder.*=true");

    QString testProjectDirectory = "%1/UntitledProject68";
    //QString testProjectDirectory = "%1/FooTest";
    testProjectDirectory = testProjectDirectory.arg(QCoreApplication::applicationDirPath());
    createDirectoryStructure(testProjectDirectory);

    Utils::FileInProjectFinder finder;
    Utils::FilePath projectDir = Utils::FilePath::fromString(testProjectDirectory);
    finder.setProjectDirectory(projectDir);

    Utils::FilePaths projectFiles = {
        Utils::FilePath::fromString(testProjectDirectory + "/App.qml"),
        Utils::FilePath::fromString(testProjectDirectory + "/Screen01.ui.qml"),
        Utils::FilePath::fromString(testProjectDirectory + "/UntitledProject68/Constants.qml"),
        Utils::FilePath::fromString(testProjectDirectory + "/UntitledProject68/EventListModel.qml"),
        Utils::FilePath::fromString(testProjectDirectory + "/UntitledProject68/EventListSimulator.qml"),
        Utils::FilePath::fromString(testProjectDirectory + "/UntitledProject68/qmldir"),
        Utils::FilePath::fromString(testProjectDirectory + "/UntitledProject68/designer/plugin.metainfo")
    };
    finder.setProjectFiles(projectFiles);

    Q_ASSERT(finder.projectDirectory() == projectDir);
    qDebug() << "Project directory set correctly.";

    // check root directory content
    bool parentFound = finder.findFileOrDirectory(Utils::FilePath::fromString(testProjectDirectory),
                                                  nullptr,
                                                  [](const QStringList &entries, int) {
                                                      qDebug() << "Found directory entries:" << entries;
                                                      Q_ASSERT(entries.contains("App.qml"));
                                                      Q_ASSERT(entries.contains("Screen01.ui.qml"));
                                                      Q_ASSERT(entries.contains("UntitledProject68.qmlproject"));
                                                      Q_ASSERT(entries.contains("designer"));
                                                  });
    Q_ASSERT(parentFound);
    qDebug() << "UntitledProject68 root directoy content found successfully.";



    bool fileFound = finder.findFileOrDirectory(Utils::FilePath::fromString(testProjectDirectory + "/App.qml"),
                                                [](const Utils::FilePath &file, int) {
                                                    qDebug() << "Found file:" << file.toString();
                                                }, nullptr);
    Q_ASSERT(fileFound);
    qDebug() << "App.qml found successfully.";

    // Find a subdirectory with the same name as the parent directory
    bool subdirFound = finder.findFileOrDirectory(Utils::FilePath::fromString(testProjectDirectory + "/UntitledProject68"),
                                                  nullptr,
                                                  [](const QStringList &entries, int) {
                                                      qDebug() << "Found directory entries:" << entries;
                                                      Q_ASSERT(entries.contains("Constants.qml"));
                                                      Q_ASSERT(entries.contains("EventListModel.qml"));
                                                      Q_ASSERT(entries.contains("EventListSimulator.qml"));
                                                      Q_ASSERT(entries.contains("qmldir"));
                                                  });
    Q_ASSERT(subdirFound);
    qDebug() << "UntitledProject68 subdirectory found successfully.";

    // Test case 5: Non-existent file handling
    bool nonExistentFile = finder.findFileOrDirectory(Utils::FilePath::fromString(testProjectDirectory + "/nonexistent.qml"),
                                                      [](const Utils::FilePath &, int) {
                                                          Q_ASSERT(false && "Should not find a non-existent file");
                                                      }, nullptr);
    Q_ASSERT(!nonExistentFile);
    qDebug() << "Non-existent file correctly not found.";
    qDebug() << "All tests passed successfully.";

    return 0;
}
