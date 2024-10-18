#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QLoggingCategory>
#include <utils/fileinprojectfinder.h>

// Helper function to create directories if they don't exist
void createDirIfNotExist(const QString &path)
{
    QDir dir(path);
    if (!dir.exists()) {
        qDebug() << "Creating directory:" << path;
        dir.mkpath(path);
    }
}

// Helper function to create a file if it doesn't exist
void createFileIfNotExist(const QString &filePath)
{
    QFile file(filePath);
    if (!file.exists()) {
        qDebug() << "Creating file:" << filePath;
        file.open(QIODevice::WriteOnly);
        file.close();
    }
}

void createDirectoryStructure(const QString &baseDir)
{
    createDirIfNotExist(baseDir);

    // Create the necessary files in the root of the base directory
    createFileIfNotExist(baseDir + "/App.qml");
    createFileIfNotExist(baseDir + "/Screen01.ui.qml");
    createFileIfNotExist(baseDir + "/UntitledProject68.qmlproject");

    // Create subdirectories and files
    QString subdir = baseDir + "/UntitledProject68";
    createDirIfNotExist(subdir);
    createFileIfNotExist(subdir + "/Constants.qml");
    createFileIfNotExist(subdir + "/EventListModel.qml");
    createFileIfNotExist(subdir + "/EventListSimulator.qml");
    createFileIfNotExist(subdir + "/qmldir");

    QString designerSubdir = subdir + "/designer";
    createDirIfNotExist(designerSubdir);
    createFileIfNotExist(designerSubdir + "/plugin.metainfo");

    qDebug() << "Initial directory structure created.";
}

void createComplexDirectoryStructure(const QString &baseDir)
{
    // Create nested directories
    QString nameA_nameB = baseDir + "/nameA/nameB";
    QString nameA_nameB_nameA = nameA_nameB + "/nameA";
    QString nameA_nameB_nameA_nameB = nameA_nameB_nameA + "/nameB";

    createDirIfNotExist(nameA_nameB_nameA_nameB);

    // Create unique files in each directory
    createFileIfNotExist(nameA_nameB + "/file_nameA_nameB");
    createFileIfNotExist(nameA_nameB_nameA + "/file_nameA_nameB_nameA");
    createFileIfNotExist(nameA_nameB_nameA_nameB + "/file_nameA_nameB_nameA_nameB");

    qDebug() << "Complex directory structure created.";
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QLoggingCategory::setFilterRules("qtc.utils.fileinprojectfinder.*=true");

    QString testProjectDirectory = "%1/UntitledProject68";
    QString complexDirectory = "%1/ComplexTest";
    testProjectDirectory = testProjectDirectory.arg(QCoreApplication::applicationDirPath());
    complexDirectory = complexDirectory.arg(QCoreApplication::applicationDirPath());

    // Create both directory structures
    createDirectoryStructure(testProjectDirectory);
    createComplexDirectoryStructure(complexDirectory);

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

    // Test case: Check if the full project directory is set correctly
    Q_ASSERT(finder.projectDirectory() == projectDir);
    qDebug() << "Project directory set correctly: " << projectDir;

    // Check root directory content
    bool parentFound = finder.findFileOrDirectory(Utils::FilePath::fromString(testProjectDirectory),
                                                  nullptr,
                                                  [](const QStringList &entries, int) {
                                                      qDebug() << "Found directory entries:" << entries;
                                                      Q_ASSERT(entries.contains("App.qml"));
                                                      Q_ASSERT(entries.contains("Screen01.ui.qml"));
                                                      Q_ASSERT(entries.contains("UntitledProject68.qmlproject"));
                                                  });
    Q_ASSERT(parentFound);
    qDebug() << "UntitledProject68 root directory content found successfully.";

    // Test case: Check complex directory structure (nameA/nameB/nameA/nameB)
    Utils::FileInProjectFinder complexFinder;
    complexFinder.setProjectDirectory(Utils::FilePath::fromString(complexDirectory));

    bool fileFound = complexFinder.findFileOrDirectory(Utils::FilePath::fromString(complexDirectory + "/nameA/nameB/file_nameA_nameB"),
                                                       [](const Utils::FilePath &file, int) {
                                                           qDebug() << "Found file:" << file.toString();
                                                           Q_ASSERT(file.toString().endsWith("file_nameA_nameB"));
                                                       }, nullptr);
    Q_ASSERT(fileFound);

    bool nestedFileFound = complexFinder.findFileOrDirectory(Utils::FilePath::fromString(complexDirectory + "/nameA/nameB/nameA/nameB/file_nameA_nameB_nameA_nameB"),
                                                             [](const Utils::FilePath &file, int) {
                                                                 qDebug() << "Found file:" << file.toString();
                                                                 Q_ASSERT(file.toString().endsWith("file_nameA_nameB_nameA_nameB"));
                                                             }, nullptr);
    Q_ASSERT(nestedFileFound);
    qDebug() << "Complex directory structure checked successfully.";

    return 0;
}
