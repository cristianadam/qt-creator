// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppmodelmanager_test.h"

#include "baseeditordocumentprocessor.h"
#include "builtineditordocumentparser.h"
#include "cppcodemodelqueries.h"
#include "cpplocatordata.h"
#ifdef QTC_WITH_CXX_FRONTEND
#include "cxxfrontendindexcache.h"
#include "cxxfrontendmodel.h"
#endif
#include "cpptoolstestcase.h"
#include "editordocumenthandle.h"
#include "modelmanagertesthelper.h"
#include "projectinfo.h"

#include <coreplugin/documentmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <texteditor/texteditor.h>
#include <coreplugin/fileutils.h>

#include <cplusplus/LookupContext.h>

#include <projectexplorer/kitmanager.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projectnodes.h>

#include <utils/environment.h>
#include <utils/hostosinfo.h>
#include <utils/qtcassert.h>

#include <QDebug>
#include <QElapsedTimer>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent>

#include <numeric>
#include <QScopeGuard>
#include <QTest>

#include <memory>

#define VERIFY_DOCUMENT_REVISION(document, expectedRevision) \
    QVERIFY(document); \
    QCOMPARE(document->revision(), expectedRevision);

using namespace ProjectExplorer;
using namespace Utils;

using CPlusPlus::Document;

// FIXME: Clean up the namespaces
using CppEditor::Tests::ModelManagerTestHelper;
using CppEditor::Tests::ProjectOpenerAndCloser;
using CppEditor::Tests::SourceFilesRefreshGuard;
using CppEditor::Tests::TemporaryCopiedDir;
using CppEditor::Tests::TemporaryDir;
using CppEditor::Tests::TestCase;
using CppEditor::Internal::Tests::VerifyCleanCppModelManager;

Q_DECLARE_METATYPE(CppEditor::ProjectFile)

namespace CppEditor::Internal {
namespace {

inline QString _(const QByteArray &ba) { return QString::fromLatin1(ba, ba.size()); }

static FilePath testDataDir(const QString &subdir)
{
    return FilePath::fromUserInput(SRCDIR "/../../../tests/cppmodelmanager/" + subdir);
}

FilePaths toAbsolutePaths(const QStringList &relativePathList,
                          const TemporaryCopiedDir &temporaryDir)
{
    FilePaths result;
    for (const QString &file : relativePathList)
        result << temporaryDir.absolutePath(file);
    return result;
}

// TODO: When possible, use this helper class in all tests
class ProjectCreator
{
public:
    explicit ProjectCreator(ModelManagerTestHelper *modelManagerTestHelper)
        : modelManagerTestHelper(modelManagerTestHelper)
    {}

    /// 'files' is expected to be a list of file names that reside in 'dir'.
    void create(const QString &name, const QString &dir, const QStringList &files)
    {
        const FilePath projectDir = testDataDir(dir);
        for (const QString &file : files)
            projectFiles << projectDir / file;

        RawProjectPart rpp;
        rpp.setQtVersion(Utils::QtMajorVersion::Qt5);
        const ProjectFiles rppFiles = Utils::transform<ProjectFiles>(projectFiles,
                [](const FilePath &file) {
            return ProjectFile(file, ProjectFile::classify(file));
        });
        const auto project = modelManagerTestHelper->createProject(
                    name, Utils::FilePath::fromString(dir).pathAppended(name + ".pro"));

        const auto part = ProjectPart::create(project->projectFilePath(), rpp, {}, rppFiles);
        projectInfo = ProjectInfo::create(ProjectUpdateInfo(project, KitInfo(nullptr), {}, {}),
                                          {part});
    }

    ModelManagerTestHelper *modelManagerTestHelper;
    ProjectInfo::ConstPtr projectInfo;
    FilePaths projectFiles;
};

/// Changes a file on the disk and restores its original contents on destruction
class FileChangerAndRestorer
{
public:
    explicit FileChangerAndRestorer(const Utils::FilePath &filePath)
        : m_filePath(filePath)
    {
    }

    ~FileChangerAndRestorer()
    {
        restoreContents();
    }

    /// Saves the contents also internally so it can be restored on destruction
    Result<QByteArray> readContents()
    {
        const Result<QByteArray> result = m_filePath.fileContents();
        if (result)
            m_originalFileContents = *result;
        return result;
    }

    bool writeContents(const QByteArray &contents) const
    {
        return TestCase::writeFile(m_filePath, contents);
    }

private:
    void restoreContents() const
    {
        TestCase::writeFile(m_filePath, m_originalFileContents);
    }

    QByteArray m_originalFileContents;
    const Utils::FilePath m_filePath;
};

} // anonymous namespace

static ProjectPart::ConstPtr projectPartOfEditorDocument(const FilePath &filePath)
{
    auto *editorDocument = CppModelManager::cppEditorDocument(filePath);
    QTC_ASSERT(editorDocument, return ProjectPart::ConstPtr());
    return editorDocument->processor()->parser()->projectPartInfo().projectPart;
}

/// Check: The preprocessor cleans include and framework paths.
void ModelManagerTest::testPathsAreClean()
{
    ModelManagerTestHelper helper;

    const FilePath dataDir = testDataDir("testdata");

    const auto project = helper.createProject(_("test_modelmanager_paths_are_clean"),
                                              Utils::FilePath::fromString("blubb.pro"));
    RawProjectPart rpp;
    rpp.setQtVersion(Utils::QtMajorVersion::Qt5);
    rpp.setMacros({ProjectExplorer::Macro("OH_BEHAVE", "-1")});
    rpp.setHeaderPaths({HeaderPath::makeUser(dataDir / "include"),
                        HeaderPath::makeFramework(dataDir / "frameworks")});
    const auto part = ProjectPart::create(project->projectFilePath(), rpp);
    const auto pi = ProjectInfo::create(ProjectUpdateInfo(project, KitInfo(nullptr), {}, {}),
                                        {part});

    CppModelManager::updateProjectInfo(pi);

    ProjectExplorer::HeaderPaths headerPaths = CppModelManager::headerPaths();
    QCOMPARE(headerPaths.size(), 2);
    QVERIFY(headerPaths.contains(HeaderPath::makeUser(dataDir / "include")));
    QVERIFY(headerPaths.contains(HeaderPath::makeFramework(dataDir / "frameworks")));
}

/// Check: How a file is to be read is what its project part says, and it is
/// said whether or not anything has parsed the file. A consumer outside this
/// plugin -- the debugger, lexing an expression off the line it has stopped
/// on -- used to take this off the document an indexing pass had left behind.
void ModelManagerTest::testLanguageFeaturesWithoutAParse()
{
    ModelManagerTestHelper helper;

    // A file the part lists and that is not on disk, so that nothing can
    // ever parse it: updateProjectInfo() does start an indexing pass, and
    // the question here is precisely what is answered without one.
    const FilePath source = testDataDir("testdata") / "sources/nothing_wrote_this.cpp";
    QVERIFY(!source.exists());
    const auto project = helper.createProject(_("test_modelmanager_language_features"),
                                              Utils::FilePath::fromString("blubb.pro"));
    RawProjectPart rpp;
    rpp.setQtVersion(Utils::QtMajorVersion::Qt5);
    // Which is what tells the part's answer from the all-features default.
    rpp.setMacros({ProjectExplorer::Macro("QT_NO_KEYWORDS", "1")});
    const auto part = ProjectPart::create(project->projectFilePath(), rpp, {},
                                          {ProjectFile(source, ProjectFile::CXXSource)});
    CppModelManager::updateProjectInfo(
        ProjectInfo::create(ProjectUpdateInfo(project, KitInfo(nullptr), {}, {}), {part}));

    QVERIFY(!CppModelManager::snapshot().document(source));

    const CPlusPlus::LanguageFeatures features = CppModelManager::languageFeatures(source);
    QVERIFY(!features.qtKeywordsEnabled);
    QVERIFY(features.qtEnabled);

    // And the defaults where no project part reaches the file at all, since
    // a lexer has to be given something.
    const FilePath stranger = testDataDir("testdata") / "sources/nothing_lists_this.cpp";
    QCOMPARE(CppModelManager::languageFeatures(stranger),
             CPlusPlus::LanguageFeatures::defaultFeatures());
}

/// Check: Frameworks headers are resolved.
void ModelManagerTest::testFrameworkHeaders()
{
    if (Utils::HostOsInfo::isWindowsHost())
        QSKIP("Can't resolve framework soft links on Windows.");

    ModelManagerTestHelper helper;

    const FilePath dataDir = testDataDir("testdata");

    const auto project = helper.createProject(_("test_modelmanager_framework_headers"),
                                              Utils::FilePath::fromString("blubb.pro"));
    RawProjectPart rpp;
    rpp.setQtVersion(Utils::QtMajorVersion::Qt5);
    rpp.setMacros({{"OH_BEHAVE", "-1"}});
    rpp.setHeaderPaths({HeaderPath::makeUser(dataDir / "include"),
                        HeaderPath::makeFramework(dataDir / "frameworks")});
    const FilePath source =
            dataDir / "sources/test_modelmanager_framework_headers.cpp";
    const auto part = ProjectPart::create(project->projectFilePath(), rpp, {},
                                          {ProjectFile(source, ProjectFile::CXXSource)});
    const auto pi = ProjectInfo::create(ProjectUpdateInfo(project, KitInfo(nullptr), {}, {}),
                                        {part});

    CppModelManager::updateProjectInfo(pi).waitForFinished();
    QCoreApplication::processEvents();

    QVERIFY(CppModelManager::snapshot().contains(source));
    Document::Ptr doc = CppModelManager::document(source);
    QVERIFY(!doc.isNull());
    CPlusPlus::Namespace *ns = doc->globalNamespace();
    QVERIFY(ns);
    QVERIFY(ns->memberCount() > 0);
    for (unsigned i = 0, ei = ns->memberCount(); i < ei; ++i) {
        CPlusPlus::Symbol *s = ns->memberAt(i);
        QVERIFY(s);
        QVERIFY(s->name());
        const CPlusPlus::Identifier *id = s->name()->asNameId();
        QVERIFY(id);
        QByteArray chars = id->chars();
        QVERIFY(chars.startsWith("success"));
    }
}

/// QTCREATORBUG-9056
/// Check: If the project configuration changes, all project files and their
///        includes have to be reparsed.
void ModelManagerTest::testRefreshAlsoIncludesOfProjectFiles()
{
    ModelManagerTestHelper helper;

    const FilePath dataDir = testDataDir("testdata");

    const FilePath testCpp = dataDir / "sources/test_modelmanager_refresh.cpp";
    const FilePath testHeader = dataDir / "sources/test_modelmanager_refresh.h";

    const auto project
            = helper.createProject(_("test_modelmanager_refresh_also_includes_of_project_files"),
                                   Utils::FilePath::fromString("blubb.pro"));
    RawProjectPart rpp;
    rpp.setQtVersion(Utils::QtMajorVersion::Qt5);
    rpp.setMacros({{"OH_BEHAVE", "-1"}});
    rpp.setHeaderPaths({HeaderPath::makeUser(dataDir / "include")});
    auto part = ProjectPart::create(project->projectFilePath(), rpp, {},
                                    {ProjectFile(testCpp, ProjectFile::CXXSource)});
    auto pi = ProjectInfo::create(ProjectUpdateInfo(project, KitInfo(nullptr), {}, {}), {part});

    QSet<FilePath> refreshedFiles = helper.updateProjectInfo(pi);
    QCOMPARE(refreshedFiles.size(), 1);
    QVERIFY(refreshedFiles.contains(testCpp));
    CPlusPlus::Snapshot snapshot = CppModelManager::snapshot();
    QVERIFY(snapshot.contains(testHeader));
    QVERIFY(snapshot.contains(testCpp));

    Document::Ptr headerDocumentBefore = snapshot.document(testHeader);
    const QList<CPlusPlus::Macro> macrosInHeaderBefore = headerDocumentBefore->definedMacros();
    QCOMPARE(macrosInHeaderBefore.size(), 1);
    QVERIFY(macrosInHeaderBefore.first().name() == "test_modelmanager_refresh_h");

    // Introduce a define that will enable another define once the document is reparsed.
    rpp.setMacros({{"TEST_DEFINE", "1"}});
    part = ProjectPart::create(project->projectFilePath(), rpp, {},
                               {ProjectFile(testCpp, ProjectFile::CXXSource)});
    pi = ProjectInfo::create(ProjectUpdateInfo(project, KitInfo(nullptr), {}, {}), {part});

    refreshedFiles = helper.updateProjectInfo(pi);

    QCOMPARE(refreshedFiles.size(), 1);
    QVERIFY(refreshedFiles.contains(testCpp));
    snapshot = CppModelManager::snapshot();
    QVERIFY(snapshot.contains(testHeader));
    QVERIFY(snapshot.contains(testCpp));

    Document::Ptr headerDocumentAfter = snapshot.document(testHeader);
    const QList<CPlusPlus::Macro> macrosInHeaderAfter = headerDocumentAfter->definedMacros();
    QCOMPARE(macrosInHeaderAfter.size(), 2);
    QVERIFY(macrosInHeaderAfter.at(0).name() == "test_modelmanager_refresh_h");
    QVERIFY(macrosInHeaderAfter.at(1).name() == "TEST_DEFINE_DEFINED");
}

/// QTCREATORBUG-9205
/// Check: When reparsing the same files again, no errors occur
///        (The CppSourceProcessor's already seen files are properly cleared!).
void ModelManagerTest::testRefreshSeveralTimes()
{
    ModelManagerTestHelper helper;

    const FilePath dataDir = testDataDir("testdata_refresh");

    const FilePath testHeader1 = dataDir / "defines.h";
    const FilePath testHeader2 = dataDir / "header.h";
    const FilePath testCpp = dataDir / "source.cpp";

    const auto project = helper.createProject(_("test_modelmanager_refresh_several_times"),
                                              Utils::FilePath::fromString("blubb.pro"));
    RawProjectPart rpp;
    rpp.setQtVersion(Utils::QtMajorVersion::Qt5);
    const ProjectFiles files = {
        ProjectFile(testHeader1, ProjectFile::CXXHeader),
        ProjectFile(testHeader2, ProjectFile::CXXHeader),
        ProjectFile(testCpp, ProjectFile::CXXSource)
    };
    const auto part = ProjectPart::create(project->projectFilePath(), rpp, {}, files);
    auto pi = ProjectInfo::create(ProjectUpdateInfo(project, KitInfo(nullptr), {}, {}), {part});
    CppModelManager::updateProjectInfo(pi);

    CPlusPlus::Snapshot snapshot;
    QSet<FilePath> refreshedFiles;
    Document::Ptr document;

    ProjectExplorer::Macros macros = {{"FIRST_DEFINE"}};
    for (int i = 0; i < 2; ++i) {
        // Simulate project configuration change by having different defines each time.
        macros += {"ANOTHER_DEFINE"};
        rpp.setMacros(macros);
        const auto part = ProjectPart::create(project->projectFilePath(), rpp, {}, files);
        pi = ProjectInfo::create({project, KitInfo(nullptr), {}, {}}, {part});

        refreshedFiles = helper.updateProjectInfo(pi);
        QCOMPARE(refreshedFiles.size(), 3);

        QVERIFY(refreshedFiles.contains(testHeader1));
        QVERIFY(refreshedFiles.contains(testHeader2));
        QVERIFY(refreshedFiles.contains(testCpp));

        snapshot = CppModelManager::snapshot();
        QVERIFY(snapshot.contains(testHeader1));
        QVERIFY(snapshot.contains(testHeader2));
        QVERIFY(snapshot.contains(testCpp));

        // No diagnostic messages expected
        document = snapshot.document(testHeader1);
        QVERIFY(document->diagnosticMessages().isEmpty());

        document = snapshot.document(testHeader2);
        QVERIFY(document->diagnosticMessages().isEmpty());

        document = snapshot.document(testCpp);
        QVERIFY(document->diagnosticMessages().isEmpty());
    }
}

/// QTCREATORBUG-9581
/// Check: If nothing has changes, nothing should be reindexed.
void ModelManagerTest::testRefreshTestForChanges()
{
    ModelManagerTestHelper helper;

    const FilePath dataDir = testDataDir("testdata_refresh");
    const FilePath testCpp = dataDir / "source.cpp";

    const auto project = helper.createProject(_("test_modelmanager_refresh_2"),
                                              Utils::FilePath::fromString("blubb.pro"));
    RawProjectPart rpp;
    rpp.setQtVersion(Utils::QtMajorVersion::Qt5);
    const auto part = ProjectPart::create(project->projectFilePath(), rpp, {},
                                          {ProjectFile(testCpp, ProjectFile::CXXSource)});
    const auto pi = ProjectInfo::create({project, KitInfo(nullptr), {}, {}}, {part});

    // Reindexing triggers a reparsing thread
    helper.resetRefreshedSourceFiles();
    QFuture<void> firstFuture = CppModelManager::updateProjectInfo(pi);
    QVERIFY(firstFuture.isStarted() || firstFuture.isRunning());
    firstFuture.waitForFinished();
    const QSet<FilePath> refreshedFiles = helper.waitForRefreshedSourceFiles();
    QCOMPARE(refreshedFiles.size(), 1);
    QVERIFY(refreshedFiles.contains(testCpp));

    // No reindexing since nothing has changed
    QFuture<void> subsequentFuture = CppModelManager::updateProjectInfo(pi);
    QVERIFY(subsequentFuture.isCanceled());
    QVERIFY(subsequentFuture.isFinished());
}

/// Check: (1) Added project files are recognized and parsed.
/// Check: (2) Removed project files are recognized and purged from the snapshot.
void ModelManagerTest::testRefreshAddedAndPurgeRemoved()
{
    ModelManagerTestHelper helper;

    const FilePath dataDir = testDataDir("testdata_refresh");

    const FilePath testHeader1 = dataDir / "header.h";
    const FilePath testHeader2 = dataDir / "defines.h";
    const FilePath testCpp = dataDir / "source.cpp";

    const auto project = helper.createProject(_("test_modelmanager_refresh_3"),
                                              Utils::FilePath::fromString("blubb.pro"));
    RawProjectPart rpp;
    rpp.setQtVersion(Utils::QtMajorVersion::Qt5);
    const auto part = ProjectPart::create(project->projectFilePath(), rpp, {},
            {{testCpp, ProjectFile::CXXSource},
             {testHeader1, ProjectFile::CXXHeader}});
    auto pi = ProjectInfo::create({project, KitInfo(nullptr), {}, {}}, {part});

    CPlusPlus::Snapshot snapshot;
    QSet<FilePath> refreshedFiles;

    refreshedFiles = helper.updateProjectInfo(pi);

    QCOMPARE(refreshedFiles.size(), 2);
    QVERIFY(refreshedFiles.contains(testHeader1));
    QVERIFY(refreshedFiles.contains(testCpp));

    snapshot = CppModelManager::snapshot();
    QVERIFY(snapshot.contains(testHeader1));
    QVERIFY(snapshot.contains(testCpp));

    // Now add testHeader2 and remove testHeader1
    const auto newPart = ProjectPart::create(project->projectFilePath(), rpp, {},
            {{testCpp, ProjectFile::CXXSource},
             {testHeader2, ProjectFile::CXXHeader}});
    pi = ProjectInfo::create({project, KitInfo(nullptr), {}, {}}, {newPart});

    refreshedFiles = helper.updateProjectInfo(pi);

    // Only the added project file was reparsed
    QCOMPARE(refreshedFiles.size(), 1);
    QVERIFY(refreshedFiles.contains(testHeader2));

    snapshot = CppModelManager::snapshot();
    QVERIFY(snapshot.contains(testHeader2));
    QVERIFY(snapshot.contains(testCpp));
    // The removed project file is not anymore in the snapshot
    QVERIFY(!snapshot.contains(testHeader1));
}

/// Check: Timestamp modified files are reparsed if project files are added or removed
///        while the project configuration stays the same
void ModelManagerTest::testRefreshTimeStampModifiedIfSourcefilesChange()
{
    QFETCH(QString, fileToChange);
    QFETCH(QStringList, initialProjectFiles);
    QFETCH(QStringList, finalProjectFiles);

    TemporaryCopiedDir temporaryDir(testDataDir("testdata_refresh2").path());
    const FilePath filePath = temporaryDir.absolutePath(fileToChange);
    const FilePaths initialProjectFilePaths = toAbsolutePaths(initialProjectFiles, temporaryDir);
    const FilePaths finalProjectFilePaths = toAbsolutePaths(finalProjectFiles, temporaryDir);

    ModelManagerTestHelper helper;

    const auto project = helper.createProject(_("test_modelmanager_refresh_timeStampModified"),
                                              FilePath::fromString("blubb.pro"));
    RawProjectPart rpp;
    rpp.setQtVersion(Utils::QtMajorVersion::Qt5);
    auto files = Utils::transform<ProjectFiles>(initialProjectFilePaths, [](const FilePath &f) {
        return ProjectFile(f, ProjectFile::CXXSource);
    });
    auto part = ProjectPart::create(project->projectFilePath(), rpp, {}, files);
    auto pi = ProjectInfo::create({project, KitInfo(nullptr), {}, {}}, {part});

    Document::Ptr document;
    CPlusPlus::Snapshot snapshot;
    QSet<FilePath> refreshedFiles;

    refreshedFiles = helper.updateProjectInfo(pi);

    QCOMPARE(refreshedFiles.size(), initialProjectFilePaths.size());
    snapshot = CppModelManager::snapshot();
    for (const FilePath &file : initialProjectFilePaths) {
        QVERIFY(refreshedFiles.contains(file));
        QVERIFY(snapshot.contains(file));
    }

    document = snapshot.document(filePath);
    const QDateTime lastModifiedBefore = document->lastModified();
    QCOMPARE(document->globalSymbolCount(), 1);
    QCOMPARE(document->globalSymbolAt(0)->name()->identifier()->chars(), "someGlobal");

    // Modify the file
    QTest::qSleep(1000); // Make sure the timestamp is different
    FileChangerAndRestorer fileChangerAndRestorer(filePath);
    const Result<QByteArray> originalContents = fileChangerAndRestorer.readContents();
    QVERIFY(originalContents);
    const QByteArray newFileContentes = originalContents.value_or(QByteArray())
            + "\nint addedOtherGlobal;";
    QVERIFY(fileChangerAndRestorer.writeContents(newFileContentes));

    // Add or remove source file. The configuration stays the same.
    files = Utils::transform<ProjectFiles>(finalProjectFilePaths, [](const FilePath &f) {
        return ProjectFile(f, ProjectFile::CXXSource);
    });
    part = ProjectPart::create(project->projectFilePath(), rpp, {}, files);
    pi = ProjectInfo::create({project, KitInfo(nullptr), {}, {}}, {part});

    refreshedFiles = helper.updateProjectInfo(pi);

    QCOMPARE(refreshedFiles.size(), finalProjectFilePaths.size());
    snapshot = CppModelManager::snapshot();
    for (const FilePath &file : finalProjectFilePaths) {
        QVERIFY(refreshedFiles.contains(file));
        QVERIFY(snapshot.contains(file));
    }
    document = snapshot.document(filePath);
    const QDateTime lastModifiedAfter = document->lastModified();
    QVERIFY(lastModifiedAfter > lastModifiedBefore);
    QCOMPARE(document->globalSymbolCount(), 2);
    QCOMPARE(document->globalSymbolAt(0)->name()->identifier()->chars(), "someGlobal");
    QCOMPARE(document->globalSymbolAt(1)->name()->identifier()->chars(), "addedOtherGlobal");
}

void ModelManagerTest::testRefreshTimeStampModifiedIfSourcefilesChange_data()
{
    QTest::addColumn<QString>("fileToChange");
    QTest::addColumn<QStringList>("initialProjectFiles");
    QTest::addColumn<QStringList>("finalProjectFiles");

    const QString testCpp = QLatin1String("source.cpp");
    const QString testCpp2 = QLatin1String("source2.cpp");

    const QString fileToChange = testCpp;
    const QStringList projectFiles1 = {testCpp};
    const QStringList projectFiles2 = {testCpp, testCpp2};

    // Add a file
    QTest::newRow("case: add project file") << fileToChange << projectFiles1 << projectFiles2;

    // Remove a file
    QTest::newRow("case: remove project file") << fileToChange << projectFiles2 << projectFiles1;
}

/// Check: If a second project is opened, the code model is still aware of
///        files of the first project.
void ModelManagerTest::testSnapshotAfterTwoProjects()
{
    QSet<FilePath> refreshedFiles;
    ModelManagerTestHelper helper;
    ProjectCreator project1(&helper);
    ProjectCreator project2(&helper);

    // Project 1
    project1.create(_("test_modelmanager_snapshot_after_two_projects.1"),
                    _("testdata_project1"),
                    {"foo.h", "foo.cpp",  "main.cpp"});

    refreshedFiles = helper.updateProjectInfo(project1.projectInfo);
    QCOMPARE(refreshedFiles, Utils::toSet(project1.projectFiles));
    const int snapshotSizeAfterProject1 = CppModelManager::snapshot().size();

    for (const FilePath &file : std::as_const(project1.projectFiles))
        QVERIFY(CppModelManager::snapshot().contains(file));

    // Project 2
    project2.create(_("test_modelmanager_snapshot_after_two_projects.2"),
                    _("testdata_project2"),
                    {"bar.h", "bar.cpp",  "main.cpp"});

    refreshedFiles = helper.updateProjectInfo(project2.projectInfo);
    QCOMPARE(refreshedFiles, Utils::toSet(project2.projectFiles));

    const int snapshotSizeAfterProject2 = CppModelManager::snapshot().size();
    QVERIFY(snapshotSizeAfterProject2 > snapshotSizeAfterProject1);
    QVERIFY(snapshotSizeAfterProject2 >= snapshotSizeAfterProject1 + project2.projectFiles.size());

    for (const FilePath &file : std::as_const(project1.projectFiles))
        QVERIFY(CppModelManager::snapshot().contains(file));
    for (const FilePath &file : std::as_const(project2.projectFiles))
        QVERIFY(CppModelManager::snapshot().contains(file));
}

/// Check: (1) For a project with a *.ui file a GeneratedFileSupport object
///            is added for the ui_* file.
/// Check: (2) The CppSourceProcessor can successfully resolve the ui_* file
///            though it might not be actually generated in the build dir.
///

void ModelManagerTest::testExtraeditorsupportUiFiles()
{
    VerifyCleanCppModelManager verify;

    TemporaryCopiedDir temporaryDir(testDataDir("testdata_guiproject1").path());
    QVERIFY(temporaryDir.isValid());
    const FilePath projectFile = temporaryDir.absolutePath("testdata_guiproject1.pro");

    ProjectOpenerAndCloser projects;
    QVERIFY(projects.open(projectFile));

    // Check working copy.
    // A GeneratedFileSupport object should have been added for the ui_* file.
    WorkingCopy workingCopy = CppModelManager::workingCopy();

    QCOMPARE(workingCopy.size(), 2); // CppModelManager::configurationFileName() and "ui_*.h"

    QStringList fileNamesInWorkinCopy;
    const WorkingCopy::Table &elements = workingCopy.elements();
    for (auto it = elements.cbegin(), end = elements.cend(); it != end; ++it)
        fileNamesInWorkinCopy << it.key().fileName();

    fileNamesInWorkinCopy.sort();
    const QString expectedUiHeaderFileName = _("ui_mainwindow.h");
    QCOMPARE(fileNamesInWorkinCopy.at(0), CppModelManager::configurationFileName().toUrlishString());
    QCOMPARE(fileNamesInWorkinCopy.at(1), expectedUiHeaderFileName);

    // Check CppSourceProcessor / includes.
    // The CppSourceProcessor is expected to find the ui_* file in the working copy.
    const FilePath fileIncludingTheUiFile = temporaryDir.absolutePath("mainwindow.cpp");
    while (!CppModelManager::snapshot().document(fileIncludingTheUiFile))
        QCoreApplication::processEvents();

    const CPlusPlus::Snapshot snapshot = CppModelManager::snapshot();
    const Document::Ptr document = snapshot.document(fileIncludingTheUiFile);
    QVERIFY(document);
    const FilePaths includedFiles = document->includedFiles();
    QCOMPARE(includedFiles.size(), 2);
    QCOMPARE(includedFiles.at(0).fileName(), _("mainwindow.h"));
    QCOMPARE(includedFiles.at(1).fileName(), _("ui_mainwindow.h"));
}

/// QTCREATORBUG-9828: Locator shows symbols of closed files
/// Check: The garbage collector should be run if the last CppEditor is closed.
void ModelManagerTest::testGcIfLastCppeditorClosed()
{
    ModelManagerTestHelper helper;

    const FilePath file = testDataDir("testdata_guiproject1/main.cpp");

    helper.resetRefreshedSourceFiles();

    // Open a file in the editor
    QCOMPARE(Core::DocumentModel::openedDocuments().size(), 0);
    Core::IEditor *editor = Core::EditorManager::openEditor(file);
    QVERIFY(editor);
    QCOMPARE(Core::DocumentModel::openedDocuments().size(), 1);
    QVERIFY(CppModelManager::isCppEditor(editor));
    QVERIFY(CppModelManager::workingCopy().get(file));

    // Wait until the file is refreshed
    helper.waitForRefreshedSourceFiles();

    // Close file/editor
    Core::EditorManager::closeDocuments({editor->document()}, /*askAboutModifiedEditors=*/false);
    helper.waitForFinishedGc();

    // Check: File is removed from the snapshpt
    QVERIFY(!CppModelManager::workingCopy().get(file));
    QVERIFY(!CppModelManager::snapshot().contains(file));
}

/// Check: Files that are open in the editor are not garbage collected.
void ModelManagerTest::testDontGcOpenedFiles()
{
    ModelManagerTestHelper helper;

    const FilePath file = testDataDir("testdata_guiproject1/main.cpp");

    helper.resetRefreshedSourceFiles();

    // Open a file in the editor
    QCOMPARE(Core::DocumentModel::openedDocuments().size(), 0);
    Core::IEditor *editor = Core::EditorManager::openEditor(file);
    QVERIFY(editor);
    QCOMPARE(Core::DocumentModel::openedDocuments().size(), 1);
    QVERIFY(CppModelManager::isCppEditor(editor));

    // Wait until the file is refreshed and check whether it is in the working copy
    helper.waitForRefreshedSourceFiles();

    QVERIFY(CppModelManager::workingCopy().get(file));

    // Run the garbage collector
    CppModelManager::GC();

    // Check: File is still there
    QVERIFY(CppModelManager::workingCopy().get(file));
    QVERIFY(CppModelManager::snapshot().contains(file));

    // Close editor
    Core::EditorManager::closeDocuments({editor->document()});
    helper.waitForFinishedGc();
    QVERIFY(CppModelManager::snapshot().isEmpty());
}

namespace {
struct EditorCloser {
    Core::IEditor *editor;
    explicit EditorCloser(Core::IEditor *editor): editor(editor) {}
    ~EditorCloser()
    {
        if (editor)
            QVERIFY(TestCase::closeEditorWithoutGarbageCollectorInvocation(editor));
    }
};

QString nameOfFirstDeclaration(const Document::Ptr &doc)
{
    if (doc && doc->globalNamespace()) {
        if (CPlusPlus::Symbol *s = doc->globalSymbolAt(0)) {
            if (CPlusPlus::Declaration *decl = s->asDeclaration()) {
                if (const CPlusPlus::Name *name = decl->name()) {
                    if (const CPlusPlus::Identifier *identifier = name->identifier())
                        return QString::fromUtf8(identifier->chars(), identifier->size());
                }
            }
        }
    }
    return QString();
}
}

void ModelManagerTest::testDefinesPerProject()
{
    ModelManagerTestHelper helper;

    const FilePath dataDir = testDataDir("testdata_defines");
    const FilePath main1File = dataDir / "main1.cpp";
    const FilePath main2File = dataDir / "main2.cpp";
    const FilePath header = dataDir / "header.h";

    const auto project = helper.createProject(_("test_modelmanager_defines_per_project"),
                                              Utils::FilePath::fromString("blubb.pro"));

    RawProjectPart rpp1;
    rpp1.setProjectFileLocation("project1.projectfile");
    rpp1.setQtVersion(Utils::QtMajorVersion::None);
    rpp1.setMacros({{"SUB1"}});
    rpp1.setHeaderPaths({HeaderPath::makeUser(dataDir / "include")});
    const auto part1 = ProjectPart::create(project->projectFilePath(), rpp1, {},
            {{main1File, ProjectFile::CXXSource}, {header, ProjectFile::CXXHeader}});

    RawProjectPart rpp2;
    rpp2.setProjectFileLocation("project1.projectfile");
    rpp2.setQtVersion(Utils::QtMajorVersion::None);
    rpp2.setMacros({{"SUB2"}});
    rpp2.setHeaderPaths({HeaderPath::makeUser(dataDir / "include")});
    const auto part2 = ProjectPart::create(project->projectFilePath(), rpp2, {},
            {{main2File, ProjectFile::CXXSource}, {header, ProjectFile::CXXHeader}});

    const auto pi = ProjectInfo::create({project, KitInfo(nullptr), {}, {}}, {part1, part2});
    helper.updateProjectInfo(pi);
    QCOMPARE(CppModelManager::snapshot().size(), 4);

    // Open a file in the editor
    QCOMPARE(Core::DocumentModel::openedDocuments().size(), 0);

    struct Data {
        QString firstDeclarationName;
        FilePath filePath;
    } d[] = {
        {_("one"), main1File},
        {_("two"), main2File}
    };

    for (auto &i : d) {
        const QString firstDeclarationName = i.firstDeclarationName;

        Core::IEditor *editor = Core::EditorManager::openEditor(i.filePath);
        EditorCloser closer(editor);
        QVERIFY(editor);
        QCOMPARE(Core::DocumentModel::openedDocuments().size(), 1);
        QVERIFY(CppModelManager::isCppEditor(editor));

        Document::Ptr doc = CppModelManager::document(i.filePath);
        QCOMPARE(nameOfFirstDeclaration(doc), firstDeclarationName);
    }
}

/// Check: A header that CppSourceProcessor::sourceNeeded() finds already present in the snapshot
/// must not be reused as-is unless none of the macros it actually queried while being parsed
/// (Document::isValidForCurrentEnvironment(), based on macroUses()/undefinedMacroUses()) would
/// resolve differently now. Without that check, when two sibling source files of the same
/// indexing batch textually include the same header under different preprocessor conditions,
/// whichever one is processed first would silently "freeze" the header's content for the rest of
/// the batch, and CppModelManager::GC() could neither detect nor fix it, since the
/// wrongly-inherited includes stay "reachable" forever.
void ModelManagerTest::testStaleHeaderReuseAcrossConfigs_QTCREATORBUG_18800()
{
    ModelManagerTestHelper helper;

    const FilePath dataDir = testDataDir("testdata_stale_header_reuse");
    const FilePath sharedHeader = dataDir / "shared.h";
    const FilePath onlyWithFlag = dataDir / "onlywithflag.h";
    const FilePath configACpp = dataDir / "configA.cpp";
    const FilePath configBCpp = dataDir / "configB.cpp";

    // Both files are part of the same project and get indexed in the same
    // batch, sharing one CppSourceProcessor. configA.cpp defines FLAG itself
    // before including the shared header, so it legitimately pulls in
    // onlywithflag.h. configB.cpp does not, and is sorted after configA.cpp,
    // so it is processed once shared.h is already sitting in the (shared,
    // in-batch) snapshot with configA.cpp's FLAG-flavored content.
    const auto project = helper.createProject(_("test_modelmanager_stale_header_reuse"),
                                               FilePath::fromString("blubb.pro"));
    RawProjectPart rpp;
    rpp.setQtVersion(Utils::QtMajorVersion::None);
    const auto part = ProjectPart::create(project->projectFilePath(), rpp, {},
            {{configACpp, ProjectFile::CXXSource},
             {configBCpp, ProjectFile::CXXSource},
             {sharedHeader, ProjectFile::CXXHeader}});
    const auto pi = ProjectInfo::create({project, KitInfo(nullptr), {}, {}}, {part});
    helper.updateProjectInfo(pi);

    QVERIFY(CppModelManager::snapshot().contains(onlyWithFlag));

    // Since configB.cpp never defines FLAG, its translation unit must not
    // (transitively) depend on onlywithflag.h.
    const CPlusPlus::Snapshot snapshot = CppModelManager::snapshot();
    const QSet<FilePath> reachableFromB = snapshot.allIncludesForDocument(configBCpp);
    QVERIFY2(!reachableFromB.contains(onlyWithFlag),
             "shared.h was reused unchanged from configA.cpp instead of being "
             "reparsed for configB.cpp, which never defines FLAG");
}

/// Check: Document::isValidForCurrentEnvironment()'s own-closure exclusion (see
/// its doc comment in CppDocument.cpp) is an approximation, not a proof: it does
/// not recursively verify that a transitively-included definer's own content is
/// itself free of dependencies on the *outer* translation unit. Here, outer.h
/// uses NESTED_FLAG, which is defined by definer.h - a file outer.h itself
/// #includes, so its definer is (wrongly, in this case) treated as
/// self-contained. But definer.h's own value for NESTED_FLAG actually depends
/// on FLAG, a macro from a sibling file external to outer.h's own closure. This
/// is a known, accepted limitation, not something Document::
/// isValidForCurrentEnvironment() is expected to catch; contrast
/// testStaleHeaderReuseAcrossConfigs_QTCREATORBUG_18800(), the direct case that
/// fix *does* cover.
void ModelManagerTest::testStaleHeaderReuseViaNestedDependency()
{
    ModelManagerTestHelper helper;

    const FilePath dataDir = testDataDir("testdata_stale_header_reuse_nested");
    const FilePath outerHeader = dataDir / "outer.h";
    const FilePath onlyWithNestedFlag = dataDir / "onlywithnestedflag.h";
    const FilePath configACpp = dataDir / "configA.cpp";
    const FilePath configBCpp = dataDir / "configB.cpp";

    // Both files are part of the same project and get indexed in the same
    // batch. configA.cpp defines FLAG before including outer.h, so
    // NESTED_FLAG (defined by definer.h, based on FLAG) legitimately pulls in
    // onlywithnestedflag.h. configB.cpp does not define FLAG.
    const auto project = helper.createProject(
                _("test_modelmanager_stale_header_reuse_nested"),
                FilePath::fromString("blubb.pro"));
    RawProjectPart rpp;
    rpp.setQtVersion(Utils::QtMajorVersion::None);
    const auto part = ProjectPart::create(project->projectFilePath(), rpp, {},
            {{configACpp, ProjectFile::CXXSource},
             {configBCpp, ProjectFile::CXXSource},
             {outerHeader, ProjectFile::CXXHeader}});
    const auto pi = ProjectInfo::create({project, KitInfo(nullptr), {}, {}}, {part});
    helper.updateProjectInfo(pi);

    // Exactly one of the two should (transitively) depend on
    // onlywithnestedflag.h: configA.cpp (which defines FLAG) should,
    // configB.cpp (which doesn't) shouldn't - regardless of which of the two
    // happened to be processed first in the batch (index() has no defined
    // order; params.sourceFiles is a QSet). The known limitation makes both
    // configs silently agree instead, whichever way that race goes: outer.h
    // ends up frozen at whichever NESTED_FLAG value was established first,
    // since NESTED_FLAG's definer (definer.h) is part of outer.h's own
    // #include closure, so its actual dependency on FLAG - external to that
    // closure - gets wrongly ignored for the second config either way.
    const CPlusPlus::Snapshot snapshot = CppModelManager::snapshot();
    const bool reachableFromA =
            snapshot.allIncludesForDocument(configACpp).contains(onlyWithNestedFlag);
    const bool reachableFromB =
            snapshot.allIncludesForDocument(configBCpp).contains(onlyWithNestedFlag);
    QEXPECT_FAIL("", "Known limitation: Document::isValidForCurrentEnvironment()'s "
                 "own-closure exclusion does not verify that a transitively-included "
                 "definer's own content is free of dependencies on the outer "
                 "translation unit", Abort);
    QVERIFY2(reachableFromA != reachableFromB,
             "configA.cpp and configB.cpp ended up agreeing on whether "
             "onlywithnestedflag.h is reachable, because NESTED_FLAG's definer "
             "(definer.h) is part of outer.h's own #include closure and so its "
             "dependency on FLAG, external to that closure, was wrongly ignored");
}

/// Check: a header (h.h) reachable both directly by #include and via a
/// sibling document's (x.h's) own recorded #include - a "diamond" - can end
/// up with its own include guard already active in the environment purely
/// via CppSourceProcessor::mergeEnvironment(), which recurses through x.h's
/// resolvedIncludes() without going through sourceNeeded()/m_included at
/// all. If the direct #include then finds h.h's cached document invalid for
/// an unrelated reason (here: FLAG) and falls through to a real reparse,
/// that reparse hits h.h's own "#ifndef H_H_INCLUDED" with the guard already
/// defined, skips the entire body, and yields an empty document - which
/// must not silently replace the real one (with its unconditional
/// "alwaysHere" declaration) in the snapshot.
///
/// Both configA.cpp and configB.cpp #include "x.h" before "h.h", so the bug
/// reproduces regardless of which of the two is processed first in the
/// batch (index() has no defined order; params.sourceFiles is a QSet):
/// whichever is processed second reaches x.h already cached (from the
/// first), merges h.h's include guard via x.h, and then finds its own direct
/// #include of h.h invalid (FLAG differs) and reparses into that
/// already-guarded environment.
void ModelManagerTest::testStaleHeaderReuseViaDiamondIncludeGuard()
{
    ModelManagerTestHelper helper;

    const FilePath dataDir = testDataDir("testdata_stale_header_reuse_diamond");
    const FilePath hHeader = dataDir / "h.h";
    const FilePath configACpp = dataDir / "configA.cpp";
    const FilePath configBCpp = dataDir / "configB.cpp";

    const auto project = helper.createProject(
                _("test_modelmanager_stale_header_reuse_diamond"),
                FilePath::fromString("blubb.pro"));
    RawProjectPart rpp;
    rpp.setQtVersion(Utils::QtMajorVersion::None);
    const auto part = ProjectPart::create(project->projectFilePath(), rpp, {},
            {{configACpp, ProjectFile::CXXSource},
             {configBCpp, ProjectFile::CXXSource}});
    const auto pi = ProjectInfo::create({project, KitInfo(nullptr), {}, {}}, {part});
    helper.updateProjectInfo(pi);

    // alwaysHere is declared unconditionally in h.h, outside of any FLAG
    // check - it must survive regardless of what happens with FLAG or with
    // h.h's own include guard.
    const Document::Ptr hDoc = CppModelManager::snapshot().document(hHeader);
    QVERIFY(hDoc);
    QVERIFY2(hDoc->globalSymbolCount() > 0,
             "h.h was reparsed while its own include guard was already "
             "active (via mergeEnvironment() recursing into it through "
             "x.h's own #include, which bypasses m_included), so the "
             "reparse skipped the entire guarded body and yielded an empty "
             "document, silently replacing the real one in the snapshot");
}

void ModelManagerTest::testPrecompiledHeaders()
{
    ModelManagerTestHelper helper;

    const FilePath dataDir = testDataDir("testdata_defines");
    const FilePath main1File = dataDir / "main1.cpp";
    const FilePath main2File = dataDir / "main2.cpp";
    const FilePath header = dataDir / "header.h";
    const FilePath pch1File = dataDir / "pch1.h";
    const FilePath pch2File = dataDir / "pch2.h";

    const auto project = helper.createProject(_("test_modelmanager_defines_per_project_pch"),
                                              Utils::FilePath::fromString("blubb.pro"));

    RawProjectPart rpp1;
    rpp1.setProjectFileLocation("project1.projectfile");
    rpp1.setQtVersion(Utils::QtMajorVersion::None);
    rpp1.setPreCompiledHeaders({pch1File});
    rpp1.setHeaderPaths({HeaderPath::makeUser(dataDir / "include")});
    const auto part1 = ProjectPart::create(project->projectFilePath(), rpp1, {},
            {{main1File, ProjectFile::CXXSource}, {header, ProjectFile::CXXHeader}});

    RawProjectPart rpp2;
    rpp2.setProjectFileLocation("project2.projectfile");
    rpp2.setQtVersion(Utils::QtMajorVersion::None);
    rpp2.setPreCompiledHeaders({pch2File});
    rpp2.setHeaderPaths({HeaderPath::makeUser(dataDir / "include")});
    const auto part2 = ProjectPart::create(project->projectFilePath(), rpp2, {},
            {{main2File, ProjectFile::CXXSource}, {header, ProjectFile::CXXHeader}});

    const auto pi = ProjectInfo::create({project, KitInfo(nullptr), {}, {}}, {part1, part2});

    helper.updateProjectInfo(pi);
    QCOMPARE(CppModelManager::snapshot().size(), 4);

    // Open a file in the editor
    QCOMPARE(Core::DocumentModel::openedDocuments().size(), 0);

    struct Data {
        QString firstDeclarationName;
        QString firstClassInPchFile;
        FilePath filePath;
    } d[] = {
        {_("one"), _("ClassInPch1"), main1File},
        {_("two"), _("ClassInPch2"), main2File}
    };
    for (auto &i : d) {
        const QString firstDeclarationName = i.firstDeclarationName;
        const QByteArray firstClassInPchFile = i.firstClassInPchFile.toUtf8();
        const FilePath filePath = i.filePath;

        Core::IEditor *editor = Core::EditorManager::openEditor(filePath);
        EditorCloser closer(editor);
        QVERIFY(editor);
        QCOMPARE(Core::DocumentModel::openedDocuments().size(), 1);
        QVERIFY(CppModelManager::isCppEditor(editor));

        auto parser = BuiltinEditorDocumentParser::get(filePath);
        QVERIFY(parser);
        BaseEditorDocumentParser::Configuration config = parser->configuration();
        config.setUsePrecompiledHeaders(true);
        parser->setConfiguration(config);
        parser->update({CppModelManager::workingCopy(), nullptr,Utils::Language::Cxx, false});

        // Check if defines from pch are considered
        Document::Ptr document = CppModelManager::document(filePath);
        QCOMPARE(nameOfFirstDeclaration(document), firstDeclarationName);

        // Check if declarations from pch are considered
        CPlusPlus::LookupContext context(document, parser->snapshot());
        const CPlusPlus::Identifier *identifier
            = document->control()->identifier(firstClassInPchFile.data());
        const QList<CPlusPlus::LookupItem> results = context.lookup(identifier,
                                                                    document->globalNamespace());
        QVERIFY(!results.isEmpty());
        QVERIFY(results.first().declaration()->type()->asClassType());
    }
}

void ModelManagerTest::testDefinesPerEditor()
{
    ModelManagerTestHelper helper;

    const FilePath dataDir = testDataDir(_("testdata_defines"));
    const FilePath main1File = dataDir / "main1.cpp";
    const FilePath main2File = dataDir / "main2.cpp";
    const FilePath header = dataDir / "header.h";

    const auto project = helper.createProject(_("test_modelmanager_defines_per_editor"),
                                              Utils::FilePath::fromString("blubb.pro"));

    RawProjectPart rpp1;
    rpp1.setQtVersion(Utils::QtMajorVersion::None);
    rpp1.setHeaderPaths({HeaderPath::makeUser(dataDir / "include")});
    const auto part1 = ProjectPart::create(project->projectFilePath(), rpp1, {},
            {{main1File, ProjectFile::CXXSource}, {header, ProjectFile::CXXHeader}});

    RawProjectPart rpp2;
    rpp2.setQtVersion(Utils::QtMajorVersion::None);
    rpp2.setHeaderPaths({HeaderPath::makeUser(dataDir / "include")});
    const auto part2 = ProjectPart::create(project->projectFilePath(), rpp2, {},
            {{main2File, ProjectFile::CXXSource}, {header, ProjectFile::CXXHeader}});

    const auto pi = ProjectInfo::create({project, KitInfo(nullptr), {}, {}}, {part1, part2});
    helper.updateProjectInfo(pi);

    QCOMPARE(CppModelManager::snapshot().size(), 4);

    // Open a file in the editor
    QCOMPARE(Core::DocumentModel::openedDocuments().size(), 0);

    struct Data {
        QString editorDefines;
        QString firstDeclarationName;
    } d[] = {
        {_("#define SUB1\n"), _("one")},
        {_("#define SUB2\n"), _("two")}
    };
    for (auto &i : d) {
        const QString editorDefines = i.editorDefines;
        const QString firstDeclarationName = i.firstDeclarationName;

        Core::IEditor *editor = Core::EditorManager::openEditor(main1File);
        EditorCloser closer(editor);
        QVERIFY(editor);
        QCOMPARE(Core::DocumentModel::openedDocuments().size(), 1);
        QVERIFY(CppModelManager::isCppEditor(editor));

        const FilePath filePath = editor->document()->filePath();
        const auto parser = BaseEditorDocumentParser::get(filePath);
        BaseEditorDocumentParser::Configuration config = parser->configuration();
        config.setEditorDefines(editorDefines.toUtf8());
        parser->setConfiguration(config);
        parser->update({CppModelManager::workingCopy(), nullptr, Utils::Language::Cxx, false});

        Document::Ptr doc = CppModelManager::document(main1File);
        QCOMPARE(nameOfFirstDeclaration(doc), firstDeclarationName);
    }
}

void ModelManagerTest::testUpdateEditorsAfterProjectUpdate()
{
    ModelManagerTestHelper helper;

    const FilePath dataDir = testDataDir("testdata_defines");
    const FilePath fileA = dataDir / "main1.cpp"; // content not relevant
    const FilePath fileB = dataDir / "main2.cpp"; // content not relevant

    // Open file A in editor
    Core::IEditor *editorA = Core::EditorManager::openEditor(fileA);
    QVERIFY(editorA);
    EditorCloser closerA(editorA);
    QCOMPARE(Core::DocumentModel::openedDocuments().size(), 1);
    QVERIFY(TestCase::waitForProcessedEditorDocument(fileA));
    ProjectPart::ConstPtr documentAProjectPart = projectPartOfEditorDocument(fileA);
    QVERIFY(!documentAProjectPart->hasProject());

    // Open file B in editor
    Core::IEditor *editorB = Core::EditorManager::openEditor(fileB);
    QVERIFY(editorB);
    EditorCloser closerB(editorB);
    QCOMPARE(Core::DocumentModel::openedDocuments().size(), 2);
    QVERIFY(TestCase::waitForProcessedEditorDocument(fileB));
    ProjectPart::ConstPtr documentBProjectPart = projectPartOfEditorDocument(fileB);
    QVERIFY(!documentBProjectPart->hasProject());

    // Switch back to document A
    Core::EditorManager::activateEditor(editorA);

    // Open/update related project
    const auto project
            = helper.createProject(_("test_modelmanager_updateEditorsAfterProjectUpdate"),
                                   Utils::FilePath::fromString("blubb.pro"));
    RawProjectPart rpp;
    rpp.setQtVersion(Utils::QtMajorVersion::None);
    const auto part = ProjectPart::create(project->projectFilePath(), rpp, {},
        {{fileA, ProjectFile::CXXSource}, {fileB, ProjectFile::CXXSource}});
    const auto pi = ProjectInfo::create({project, KitInfo(nullptr), {}, {}}, {part});
    helper.updateProjectInfo(pi);

    // ... and check for updated editor document A
    QVERIFY(TestCase::waitForProcessedEditorDocument(fileA));
    documentAProjectPart = projectPartOfEditorDocument(fileA);
    QCOMPARE(documentAProjectPart->topLevelProject, pi->projectFilePath());

    // Switch back to document B and check if that's updated, too
    Core::EditorManager::activateEditor(editorB);
    QVERIFY(TestCase::waitForProcessedEditorDocument(fileB));
    documentBProjectPart = projectPartOfEditorDocument(fileB);
    QCOMPARE(documentBProjectPart->topLevelProject, pi->projectFilePath());
}

void ModelManagerTest::testRenameIncludes_data()
{
    QTest::addColumn<QString>("oldRelPath");
    QTest::addColumn<QString>("newRelPath");
    QTest::addColumn<bool>("successExpected");

    QTest::addRow("rename in place 1")
        << "subdir1/header1.h" << "subdir1/header1_renamed.h" << true;
    QTest::addRow("rename in place 2")
        << "subdir2/header2.h" << "subdir2/header2_renamed.h" << true;
    QTest::addRow("rename in place 3") << "header.h" << "header_renamed.h" << true;
    QTest::addRow("move up") << "subdir1/header1.h" << "header1_moved.h" << true;
    QTest::addRow("move up (breaks build)") << "subdir2/header2.h" << "header2_moved.h" << false;
    QTest::addRow("move down") << "header.h" << "subdir1/header_moved.h" << true;
    QTest::addRow("move across") << "subdir1/header1.h" << "subdir2/header1_moved.h" << true;
    QTest::addRow("move across (breaks build)")
        << "subdir2/header2.h" << "subdir1/header2_moved.h" << false;
}

void ModelManagerTest::testRenameIncludes()
{
    // Set up project.
    TemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    const FilePath sourceDir = testDataDir("testdata_renameheaders");
    const FilePath srcFilePath = FilePath::fromString(sourceDir.path());
    const FilePath projectDir = tmpDir.filePath().pathAppended(srcFilePath.fileName());
    const auto copyResult = srcFilePath.copyRecursively(projectDir);
    if (!copyResult)
        qDebug() << copyResult.error();
    QVERIFY(copyResult);
    Kit * const kit  = Utils::findOr(KitManager::kits(), nullptr, [](const Kit *k) {
        return k->isValid() && !k->hasWarning() && k->value("QtSupport.QtInformation").isValid();
    });
    if (!kit)
        QSKIP("The test requires at least one valid kit with a valid Qt");
    const FilePath projectFile = projectDir.pathAppended(projectDir.fileName() + ".pro");
    SourceFilesRefreshGuard refreshGuard;
    ProjectOpenerAndCloser projectMgr;
    const ProjectInfo::ConstPtr projectInfo = projectMgr.open(projectFile, kit);
    QVERIFY(projectInfo);
    QVERIFY(refreshGuard.wait());

    // Verify initial code model state.
    const auto makeAbs = [&](const QStringList &relPaths) {
        return Utils::transform<QSet<FilePath>>(relPaths, [&](const QString &relPath) {
            return projectDir.pathAppended(relPath);
        });
    };
    const QSet<FilePath> allSources = makeAbs({"main.cpp", "subdir1/file1.cpp", "subdir2/file2.cpp"});
    const QSet<FilePath> allHeaders = makeAbs({"header.h", "subdir1/header1.h", "subdir2/header2.h"});
    QCOMPARE(projectInfo->sourceFiles(), allSources + allHeaders);
    CPlusPlus::Snapshot snapshot = CppModelManager::snapshot();
    for (const FilePath &srcFile : allSources) {
        QCOMPARE(snapshot.allIncludesForDocument(srcFile), allHeaders);
    }

    // Rename the header.
    QFETCH(QString, oldRelPath);
    QFETCH(QString, newRelPath);
    QFETCH(bool, successExpected);
    const FilePath oldHeader = projectDir.pathAppended(oldRelPath);
    const FilePath newHeader = projectDir.pathAppended(newRelPath);
    refreshGuard.expect(3);
    QVERIFY(ProjectExplorerPlugin::renameFile(oldHeader, newHeader));

    // Verify new code model state.
    QVERIFY(refreshGuard.wait());
    QSet<FilePath> incompleteNewHeadersSet = allHeaders;
    incompleteNewHeadersSet.remove(oldHeader);
    QSet<FilePath> completeNewHeadersSet = incompleteNewHeadersSet;
    completeNewHeadersSet << newHeader;

    snapshot = CppModelManager::snapshot();
    for (const FilePath &srcFile : allSources) {
        const QSet<FilePath> &expectedHeaders = srcFile.fileName() == "main.cpp" && !successExpected
            ? incompleteNewHeadersSet : completeNewHeadersSet;
        QCOMPARE(snapshot.allIncludesForDocument(srcFile), expectedHeaders);
    }
}

void ModelManagerTest::testMoveIncludingSources_data()
{
    QTest::addColumn<QString>("oldRelPath");
    QTest::addColumn<QString>("newRelPath");

    QTest::addRow("move up") << "subdir1/file1.cpp" << "file1_moved.cpp";
    QTest::addRow("move down") << "main.cpp" << "subdir1/main.cpp";
    QTest::addRow("move across") << "subdir1/file1.cpp" << "subdir2/file1_moved.cpp";
}

void ModelManagerTest::testMoveIncludingSources()
{
    QFETCH(QString, oldRelPath);
    QFETCH(QString, newRelPath);

    // Set up project.
    TemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    const FilePath sourceDir = testDataDir("testdata_renameheaders");
    const FilePath srcFilePath = FilePath::fromString(sourceDir.path());
    const FilePath projectDir = tmpDir.filePath().pathAppended(srcFilePath.fileName());
    const auto copyResult = srcFilePath.copyRecursively(projectDir);
    if (!copyResult)
        qDebug() << copyResult.error();
    QVERIFY(copyResult);
    Kit * const kit  = Utils::findOr(KitManager::kits(), nullptr, [](const Kit *k) {
        return k->isValid() && !k->hasWarning() && k->value("QtSupport.QtInformation").isValid();
    });
    if (!kit)
        QSKIP("The test requires at least one valid kit with a valid Qt");
    SourceFilesRefreshGuard refreshGuard;
    const FilePath projectFile = projectDir.pathAppended(projectDir.fileName() + ".pro");
    ProjectOpenerAndCloser projectMgr;
    QVERIFY(projectMgr.open(projectFile, kit));
    QVERIFY(refreshGuard.wait());

    // Verify initial code model state.
    const auto makeAbs = [&](const QStringList &relPaths) {
        return Utils::transform<QSet<FilePath>>(relPaths, [&](const QString &relPath) {
            return projectDir.pathAppended(relPath);
        });
    };
    const FilePath oldSource = projectDir.pathAppended(oldRelPath);
    QVERIFY(oldSource.exists());
    const QSet<FilePath> includedHeaders = makeAbs(
        {"header.h", "subdir1/header1.h", "subdir2/header2.h"});
    QCOMPARE(CppModelManager::snapshot().allIncludesForDocument(oldSource), includedHeaders);

    // Rename the source file.
    refreshGuard.expect(1);
    const FilePath newSource = projectDir.pathAppended(newRelPath);
    QVERIFY(ProjectExplorerPlugin::renameFile(oldSource, newSource, projectMgr.projects().first()));

    // Verify new code model state.
    QVERIFY(refreshGuard.wait());
    QCOMPARE(CppModelManager::snapshot().allIncludesForDocument(newSource), includedHeaders);
}

void ModelManagerTest::testRenameIncludesInEditor()
{
    struct ModelManagerGCHelper {
        ~ModelManagerGCHelper() { CppModelManager::GC(); }
    } GCHelper;
    Q_UNUSED(GCHelper) // do not warn about being unused

    TemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    const QDir workingDir(tmpDir.path());
    const QStringList fileNames = {"baz.h", "baz2.h", "baz3.h", "foo.h", "foo.cpp", "main.cpp"};
    const FilePath headerWithPragmaOnce = FilePath::fromString(workingDir.filePath("foo.h"));
    const FilePath renamedHeaderWithPragmaOnce = FilePath::fromString(workingDir.filePath("bar.h"));
    const QString headerWithNormalGuard(workingDir.filePath(_("baz.h")));
    const QString renamedHeaderWithNormalGuard(workingDir.filePath(_("foobar2000.h")));
    const QString headerWithUnderscoredGuard(workingDir.filePath(_("baz2.h")));
    const QString renamedHeaderWithUnderscoredGuard(workingDir.filePath(_("foobar4000.h")));
    const QString headerWithMalformedGuard(workingDir.filePath(_("baz3.h")));
    const QString renamedHeaderWithMalformedGuard(workingDir.filePath(_("foobar5000.h")));
    const FilePath mainFile = FilePath::fromString(workingDir.filePath("main.cpp"));
    const FilePath testDir = testDataDir("testdata_project1");

    ModelManagerTestHelper helper;
    helper.resetRefreshedSourceFiles();

    // Copy test files to a temporary directory
    QSet<FilePath> sourceFiles;
    for (const QString &fileName : fileNames) {
        const FilePath filePath = FilePath::fromString(workingDir.filePath(fileName));
        QVERIFY(testDir.pathAppended(fileName).copyFile(filePath));
        // Saving source file names for the model manager update,
        // so we can update just the relevant files.
        if (ProjectFile::classify(filePath) == ProjectFile::CXXSource)
            sourceFiles.insert(filePath);
    }

    // Update the c++ model manager and check for the old includes
    CppModelManager::updateSourceFiles(sourceFiles).waitForFinished();
    QCoreApplication::processEvents();
    CPlusPlus::Snapshot snapshot = CppModelManager::snapshot();
    for (const FilePath &sourceFile : std::as_const(sourceFiles)) {
        QCOMPARE(snapshot.allIncludesForDocument(sourceFile),
                 QSet<FilePath>{headerWithPragmaOnce});
    }

    // Open a file in the editor
    QCOMPARE(Core::DocumentModel::openedDocuments().size(), 0);
    Core::IEditor *editor = Core::EditorManager::openEditor(mainFile);
    QVERIFY(editor);
    EditorCloser editorCloser(editor);
    const QScopeGuard cleanup([] { Core::DocumentManager::saveAllModifiedDocumentsSilently(); });
    QCOMPARE(Core::DocumentModel::openedDocuments().size(), 1);
    QVERIFY(CppModelManager::isCppEditor(editor));
    QVERIFY(CppModelManager::workingCopy().get(mainFile));

    // Test the renaming of a header file where a pragma once guard is present
    QVERIFY(ProjectExplorerPlugin::renameFile(headerWithPragmaOnce, renamedHeaderWithPragmaOnce));

    // Test the renaming the header with include guard:
    // The contents should match the foobar2000.h in the testdata_project2 project
    QVERIFY(ProjectExplorerPlugin::renameFile(FilePath::fromString(headerWithNormalGuard),
                                              FilePath::fromString(renamedHeaderWithNormalGuard)));

    const FilePath testDir2 = testDataDir("testdata_project2");
    QFile foobar2000Header(testDir2.pathAppended("foobar2000.h").path());
    QVERIFY(foobar2000Header.open(QFile::ReadOnly | QFile::Text));
    const auto foobar2000HeaderContents = foobar2000Header.readAll();
    foobar2000Header.close();

    QFile renamedHeader(renamedHeaderWithNormalGuard);
    QVERIFY(renamedHeader.open(QFile::ReadOnly | QFile::Text));
    auto renamedHeaderContents = renamedHeader.readAll();
    renamedHeader.close();
    QCOMPARE(renamedHeaderContents, foobar2000HeaderContents);

    // Test the renaming the header with underscore pre/suffixed include guard:
    // The contents should match the foobar2000.h in the testdata_project2 project
    QVERIFY(
        Core::FileUtils::renameFile(Utils::FilePath::fromString(headerWithUnderscoredGuard),
                                    Utils::FilePath::fromString(renamedHeaderWithUnderscoredGuard),
                                    Core::HandleIncludeGuards::Yes));

    QFile foobar4000Header(testDir2.pathAppended("foobar4000.h").path());
    QVERIFY(foobar4000Header.open(QFile::ReadOnly | QFile::Text));
    const auto foobar4000HeaderContents = foobar4000Header.readAll();
    foobar4000Header.close();

    renamedHeader.setFileName(renamedHeaderWithUnderscoredGuard);
    QVERIFY(renamedHeader.open(QFile::ReadOnly | QFile::Text));
    renamedHeaderContents = renamedHeader.readAll();
    renamedHeader.close();
    QCOMPARE(renamedHeaderContents, foobar4000HeaderContents);

    // test the renaming of a header with a malformed guard to verify we do not make
    // accidental refactors
    renamedHeader.setFileName(headerWithMalformedGuard);
    QVERIFY(renamedHeader.open(QFile::ReadOnly | QFile::Text));
    auto originalMalformedGuardContents = renamedHeader.readAll();
    renamedHeader.close();

    QVERIFY(Core::FileUtils::renameFile(Utils::FilePath::fromString(headerWithMalformedGuard),
                                        Utils::FilePath::fromString(renamedHeaderWithMalformedGuard),
                                        Core::HandleIncludeGuards::Yes));

    renamedHeader.setFileName(renamedHeaderWithMalformedGuard);
    QVERIFY(renamedHeader.open(QFile::ReadOnly | QFile::Text));
    renamedHeaderContents = renamedHeader.readAll();
    renamedHeader.close();
    QCOMPARE(renamedHeaderContents, originalMalformedGuardContents);

    // Update the c++ model manager again and check for the new includes
    TestCase::waitForProcessedEditorDocument(mainFile);
    CppModelManager::updateSourceFiles(sourceFiles).waitForFinished();
    QCoreApplication::processEvents();
    snapshot = CppModelManager::snapshot();
    for (const FilePath &sourceFile : std::as_const(sourceFiles)) {
        QCOMPARE(snapshot.allIncludesForDocument(sourceFile),
                 QSet<FilePath>{renamedHeaderWithPragmaOnce});
    }
}

void ModelManagerTest::testDocumentsAndRevisions()
{
    TestCase helper;

    // Index two files
    const FilePath testDir = testDataDir("testdata_project1");
    const FilePath filePath1 = testDir / "foo.h";
    const FilePath filePath2 = testDir / "foo.cpp";
    const QSet<FilePath> filesToIndex = {filePath1,filePath2};
    QVERIFY(TestCase::parseFiles(filesToIndex));

    VERIFY_DOCUMENT_REVISION(CppModelManager::document(filePath1), 1U);
    VERIFY_DOCUMENT_REVISION(CppModelManager::document(filePath2), 1U);

    // Open editor for file 1
    TextEditor::BaseTextEditor *editor1;
    QVERIFY(helper.openCppEditor(filePath1, &editor1));
    helper.closeEditorAtEndOfTestCase(editor1);
    QVERIFY(TestCase::waitForProcessedEditorDocument(filePath1));
    VERIFY_DOCUMENT_REVISION(CppModelManager::document(filePath1), 2U);
    VERIFY_DOCUMENT_REVISION(CppModelManager::document(filePath2), 1U);

    // Index again
    QVERIFY(TestCase::parseFiles(filesToIndex));
    VERIFY_DOCUMENT_REVISION(CppModelManager::document(filePath1), 3U);
    VERIFY_DOCUMENT_REVISION(CppModelManager::document(filePath2), 2U);

    // Open editor for file 2
    TextEditor::BaseTextEditor *editor2;
    QVERIFY(helper.openCppEditor(filePath2, &editor2));
    helper.closeEditorAtEndOfTestCase(editor2);
    QVERIFY(TestCase::waitForProcessedEditorDocument(filePath2));
    VERIFY_DOCUMENT_REVISION(CppModelManager::document(filePath1), 3U);
    VERIFY_DOCUMENT_REVISION(CppModelManager::document(filePath2), 3U);

    // Index again
    QVERIFY(TestCase::parseFiles(filesToIndex));
    VERIFY_DOCUMENT_REVISION(CppModelManager::document(filePath1), 4U);
    VERIFY_DOCUMENT_REVISION(CppModelManager::document(filePath2), 4U);
}

/// What fileUpdated() carries is all three of its readers outside this plugin
/// ask of a reading -- the class view, the test tree and the includes filter
/// each only want to know which file to look at again.
void ModelManagerTest::testFileUpdatedSaysWhichFile()
{
    TestCase helper;

    const FilePath testDir = testDataDir("testdata_project1");
    const FilePath filePath1 = testDir / "foo.h";
    const FilePath filePath2 = testDir / "foo.cpp";

    // The signal is emitted on the thread that parsed the file; the context
    // object puts the collecting on this one.
    QSet<FilePath> updated;
    const QMetaObject::Connection connection = connect(
        CppModelManager::instance(), &CppModelManager::fileUpdated,
        this, [&updated](const FilePath &filePath) { updated.insert(filePath); });
    const QScopeGuard disconnectAtEnd([connection] { QObject::disconnect(connection); });

    QVERIFY(TestCase::parseFiles({filePath1, filePath2}));

    QTRY_VERIFY(updated.contains(filePath1));
    QTRY_VERIFY(updated.contains(filePath2));
}

void ModelManagerTest::testSettingsChanges()
{
    const CppCodeModelSettingsData globalSettingsBackup
        = CppCodeModelSettings::settingsForProject(nullptr);
    const QScopeGuard restoreGlobalSettings(
        [&] { CppCodeModelSettings::setGlobal(globalSettingsBackup); });

    ModelManagerTestHelper helper;

    int refreshCount = 0;
    QSet<FilePath> refreshedFiles;
    connect(CppModelManager::instance(), &CppModelManager::sourceFilesRefreshed,
            &helper, [&](const QSet<FilePath> &files) {
        ++refreshCount;
        refreshedFiles.unite(files);
    });
    const auto waitForRefresh = [] {
        return ::CppEditor::Tests::waitForSignalOrTimeout(CppModelManager::instance(),
                                                          &CppModelManager::sourceFilesRefreshed,
                                                          5000);
    };

    const auto setupProjectNodes = [](Project &p, const ProjectFiles &projectFiles) {
        auto rootNode = std::make_unique<ProjectNode>(p.projectFilePath());
        for (const ProjectFile &sourceFile : projectFiles) {
            rootNode->addNestedNode(std::make_unique<FileNode>(sourceFile.path,
                                                               sourceFile.isHeader()
                                                               ? FileType::Header
                                                                   : FileType::Source));
        }
        p.setRootProjectNode(std::move(rootNode));
    };

    // Set up projects.
    const FilePath p1Dir = testDataDir("testdata_project1");
    const FilePaths p1Files
        = Utils::transform(QStringList{"baz.h", "baz2.h", "baz3.h", "foo.cpp", "foo.h", "main.cpp"},
                           [&](const QString &fn) { return p1Dir / fn; });
    const ProjectFiles p1ProjectFiles = Utils::transform(p1Files, [](const FilePath &fp) {
        return ProjectFile(fp, ProjectFile::classify(fp));
    });
    Project * const p1 = helper.createProject("testdata_project1", FilePath::fromString("p1.pro"));
    setupProjectNodes(*p1, p1ProjectFiles);
    RawProjectPart rpp1;
    const auto part1 = ProjectPart::create(p1->projectFilePath(), rpp1, {}, p1ProjectFiles);
    const auto pi1 = ProjectInfo::create(ProjectUpdateInfo(p1, KitInfo(nullptr), {}, {}), {part1});
    const QSet<FilePath> p1Sources = Utils::toSet(p1Files);
    CppModelManager::updateProjectInfo(pi1);

    const FilePath p2Dir("testdata_project2");
    const FilePaths p2Files
        = Utils::transform(QStringList{"bar.h", "bar.cpp", "foobar2000.h", "foobar4000.h", "main.cpp"},
                           [&](const QString &fn) { return p1Dir / fn; });
    const ProjectFiles p2ProjectFiles = Utils::transform(p2Files, [](const FilePath &fp) {
        return ProjectFile(fp, ProjectFile::classify(fp));
    });
    Project * const p2 = helper.createProject("testdata_project2", FilePath::fromString("p2.pro"));
    setupProjectNodes(*p2, p2ProjectFiles);
    RawProjectPart rpp2;
    const auto part2 = ProjectPart::create(p2->projectFilePath(), rpp2, {}, p2ProjectFiles);
    const auto pi2 = ProjectInfo::create(ProjectUpdateInfo(p2, KitInfo(nullptr), {}, {}), {part2});
    const QSet<FilePath> p2Sources = Utils::toSet(p2Files);
    CppModelManager::updateProjectInfo(pi2);

    // Initial check: Have all files been indexed?
    while (refreshCount < 2)
        QVERIFY2(waitForRefresh(), qPrintable(QString::number(refreshCount)));
    const QSet<FilePath> allSources = p1Sources + p2Sources;
    QCOMPARE(refreshedFiles, allSources);

    // Switch first project from global to local settings. Nothing should get re-indexed,
    // as the default values are the same.
    refreshCount = 0;
    refreshedFiles.clear();
    QVERIFY(!CppCodeModelSettings::hasCustomSettings(p1));
    CppCodeModelSettingsData p1Settings = CppCodeModelSettings::settingsForProject(p1);
    CppCodeModelSettings::setSettingsForProject(p1, p1Settings);
    QVERIFY(CppCodeModelSettings::hasCustomSettings(p1));
    QCOMPARE(refreshCount, 0);
    QVERIFY(!waitForRefresh());

    // Change global settings. Only the second project should get re-indexed, as the first one
    // has its own settings, which are still the same.
    CppCodeModelSettingsData globalSettings = CppCodeModelSettings::settingsForProject(nullptr);
    globalSettings.indexerFileSizeLimitInMb = 1;
    CppCodeModelSettings::setGlobal(globalSettings);
    if (refreshCount == 0)
        QVERIFY(waitForRefresh());
    QVERIFY(!waitForRefresh());
    QCOMPARE(refreshedFiles, p2Sources);

    // Change first project's settings. Only this project should get re-indexed.
    refreshCount = 0;
    refreshedFiles.clear();
    p1Settings.ignoreFiles = true;
    p1Settings.ignorePattern = "baz3.h";
    CppCodeModelSettings::setSettingsForProject(p1, p1Settings);
    if (refreshCount == 0)
        QVERIFY(waitForRefresh());
    QVERIFY(!waitForRefresh());
    QSet<FilePath> filteredP1Sources = p1Sources;
    filteredP1Sources -= p1Dir / "baz3.h";
    QCOMPARE(refreshedFiles, filteredP1Sources);
}

static bool True = true;
static bool False = false;

void ModelManagerTest::testOptionalIndexing_data()
{
    QTest::addColumn<bool>("enableGlobally");
    QTest::addColumn<bool *>("enableForP1");
    QTest::addColumn<bool *>("enableForP2");
    QTest::addColumn<bool>("foo1Present");
    QTest::addColumn<bool>("foo2Present");

    QTest::addRow("globally disabled, no custom settings")
        << false << (bool *) nullptr << (bool *) nullptr << false << false;
    QTest::addRow("globally disabled, redundantly disabled for project 2")
        << false << (bool *) nullptr << &False << false << false;
    QTest::addRow("globally disabled, enabled for project 2")
        << false << (bool *) nullptr << &True << false << true;
    QTest::addRow("globally disabled, redundantly disabled for project 1")
        << false << &False << (bool *) nullptr << false << false;
    QTest::addRow("globally disabled, redundantly disabled for both projects")
        << false << &False << &False << false << false;
    QTest::addRow("globally disabled, redundantly disabled for project 1, enabled for project 2")
        << false << &False << &True << false << true;
    QTest::addRow("globally disabled, enabled for project 1")
        << false << &True << (bool *) nullptr << true << false;
    QTest::addRow("globally disabled, enabled for project 1, redundantly disabled for project 2")
        << false << &True << &False << true << false;
    QTest::addRow("globally disabled, enabled for both project")
        << false << &True << &True << true << true;
    QTest::addRow("globally enabled, no custom settings")
        << true << (bool *) nullptr << (bool *) nullptr << true << true ;
    QTest::addRow("globally enabled, disabled for project 2")
        << true << (bool *) nullptr << &False << true << false;
    QTest::addRow("globally enabled, redundantly enabled for project 2")
        << true << (bool *) nullptr << &True << true << true;
    QTest::addRow("globally enabled, disabled for project 1")
        << true << &False << (bool *) nullptr << false << true;
    QTest::addRow("globally enabled, disabled for both projects")
        << true << &False << &False << false << false;
    QTest::addRow("globally enabled, disabled for project 1, redundantly enabled for project 2")
        << true << &False << &True << false << true;
    QTest::addRow("globally enabled, redundantly enabled for project 1")
        << true << &True << (bool *) nullptr << true << true;
    QTest::addRow("globally enabled, redundantly enabled for project 1, disabled for project 2")
        << true << &True << &False << true << false;
    QTest::addRow("globally enabled, redundantly enabled for both projects")
        << true << &True << &True << true << true;
}

void ModelManagerTest::testOptionalIndexing()
{
    if (CppModelManager::isClangCodeModelActive())
        QSKIP("Test only makes sense with built-in locators");

    QFETCH(bool, enableGlobally);
    QFETCH(bool *, enableForP1);
    QFETCH(bool *, enableForP2);
    QFETCH(bool, foo1Present);
    QFETCH(bool, foo2Present);

    // Apply global setting, if necessary. Needs to be reverted in the end.
    class TempIndexingDisabler {
    public:
        TempIndexingDisabler(bool enable)
        {
            if (!enable)
                reset(false);
        }
        ~TempIndexingDisabler() { reset(true); }
    private:
        void reset(bool enable)
        {
            CppCodeModelSettingsData settings = CppCodeModelSettings::global().data();
            settings.enableIndexing = enable;
            CppCodeModelSettings::setGlobal(settings);
        }
    };
    const TempIndexingDisabler disabler(enableGlobally);

    // Set up projects.
    TemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    const FilePath sourceDir = testDataDir("testdata_optionalindexing");
    const FilePath srcFilePath = FilePath::fromString(sourceDir.path());
    const FilePath projectDir = tmpDir.filePath().pathAppended(srcFilePath.fileName());
    const auto copyResult = srcFilePath.copyRecursively(projectDir);
    if (!copyResult)
        qDebug() << copyResult.error();
    QVERIFY(copyResult);
    Kit * const kit  = Utils::findOr(KitManager::kits(), nullptr, [](const Kit *k) {
        return k->isValid() && !k->hasWarning() && k->value("QtSupport.QtInformation").isValid();
    });
    if (!kit)
        QSKIP("The test requires at least one valid kit with a valid Qt");
    const FilePath p1ProjectFile = projectDir.pathAppended("lib1.pro");
    auto projectMgr = std::make_unique<ProjectOpenerAndCloser>();
    SourceFilesRefreshGuard refreshGuard;
    QVERIFY(projectMgr->open(p1ProjectFile, kit));
    QVERIFY(refreshGuard.wait());
    refreshGuard.expect(1);
    Project *p1 = projectMgr->projects().first();
    const FilePath p2ProjectFile = projectDir.pathAppended("lib2.pro");
    QVERIFY(projectMgr->open(p2ProjectFile, kit));
    QVERIFY(refreshGuard.wait());
    refreshGuard.expect(1);
    Project *p2 = projectMgr->projects().last();

    const auto applyProjectSpecificSettings = [&](Project *p, bool *enable) {
        if (!enable)
            return;
        refreshGuard.expect(1);
        CppCodeModelSettingsData settings = CppCodeModelSettings::settingsForProject(p);
        settings.enableIndexing = *enable;
        CppCodeModelSettings::setSettingsForProject(p, settings);
        if (*enable != enableGlobally)
            QVERIFY(refreshGuard.wait());
    };
    applyProjectSpecificSettings(p1, enableForP1);
    applyProjectSpecificSettings(p2, enableForP2);

    // Compare locator results to expectations.
    Core::LocatorFilterEntries entries = Core::LocatorMatcher::runBlocking(
        Core::LocatorMatcher::matchers(Core::MatcherType::Functions), "foo");
    const auto hasEntry = [&](const QString &name) {
        return Utils::contains(entries, [&](const Core::LocatorFilterEntry &e) {
            return e.displayName == name + "()";
        });
    };
    QCOMPARE(hasEntry("foo1"), foo1Present);
    QCOMPARE(hasEntry("foo2"), foo2Present);

    // Close and re-open projects, then check again, to see whether the settings persisted
    // and are taking effect.
    projectMgr.reset(nullptr);
    projectMgr.reset(new ProjectOpenerAndCloser);
    refreshGuard.expect(1);
    QVERIFY(projectMgr->open(p1ProjectFile, kit));
    p1 = projectMgr->projects().first();
    QCOMPARE(
        CppCodeModelSettings::settingsForProject(p1).enableIndexing,
        enableForP1 ? *enableForP1 : enableGlobally);
    QVERIFY(refreshGuard.wait());
    refreshGuard.expect(1);
    QVERIFY(projectMgr->open(p2ProjectFile, kit));
    p2 = projectMgr->projects().last();
    QCOMPARE(
        CppCodeModelSettings::settingsForProject(p2).enableIndexing,
        enableForP2 ? *enableForP2 : enableGlobally);
    QVERIFY(refreshGuard.wait());

    entries = Core::LocatorMatcher::runBlocking(
        Core::LocatorMatcher::matchers(Core::MatcherType::Functions), "foo");
    QCOMPARE(hasEntry("foo1"), foo1Present);
    QCOMPARE(hasEntry("foo2"), foo2Present);
}

// The index keeps up with a file that changes.
//
// What makes this worth pinning: the cxx front end's index reads what a
// pass over the project's files reports and nothing else, so everything
// depends on a change being reported by a pass. Saving a file is one --
// Qt Creator tells the model manager what it wrote -- and if it ever
// stopped being one, the index would quietly describe yesterday's code
// with nothing else looking wrong.
void ModelManagerTest::testTheIndexFollowsAChangedFile()
{
    TemporaryDir dir;
    QVERIFY(dir.isValid());
    const FilePath source = dir.createFile("changing.cpp",
                                           "class DeclaredBefore {};\n"
                                           "void writtenBefore() {}\n");
    QVERIFY(!source.isEmpty());

    CppLocatorData * const locatorData = CppModelManager::locatorData();
    QVERIFY(locatorData);
    const auto indexHolds = [locatorData](const QString &name) {
        return !locatorData->findSymbols(IndexItem::All, name).isEmpty();
    };
    // The cxx front end's readings outlive the pass that asked for them,
    // and until they land the entries are the built-in reading's.
    const auto readThrough = [&](const FilePath &file) {
        if (!CppEditor::Tests::TestCase::parseFiles({file}))
            return false;
        return QTest::qWaitFor([locatorData] {
            return locatorData->cxxFrontendFilesOutstanding() == 0;
        }, 60000);
    };

    QVERIFY(readThrough(source));
    QVERIFY(indexHolds("DeclaredBefore"));
    QVERIFY(indexHolds("writtenBefore"));

    // Whether the cxx front end is the one answering here, which it is
    // only where somebody asked for it. Asked of the setting and not of
    // what the front end has done so far: the checks above are satisfied
    // by the built-in reading's entries on their own, so a front end that
    // has stopped reading anything at all would otherwise excuse itself
    // from the one check that would catch it.
#ifdef QTC_WITH_CXX_FRONTEND
    const bool throughTheCxxFrontEnd = cxxFrontendModelRequested();
#else
    const bool throughTheCxxFrontEnd = false;
#endif

    // The same file, saying something else -- which is what a save is.
    QVERIFY(source.writeFileContents("class DeclaredAfter {};\n"
                                     "void writtenAfter() {}\n"));
    const int readBefore = locatorData->cxxFrontendCacheMisses();
    QVERIFY(readThrough(source));
    if (throughTheCxxFrontEnd) {
        QVERIFY2(locatorData->cxxFrontendCacheMisses() > readBefore,
                 "the front end was never asked to read the file again");
    }

    QVERIFY2(indexHolds("DeclaredAfter"), "the index did not follow the file");
    QVERIFY2(indexHolds("writtenAfter"), "the index did not follow the file");
    QVERIFY2(!indexHolds("DeclaredBefore"), "the index kept what the file no longer says");
    QVERIFY2(!indexHolds("writtenBefore"), "the index kept what the file no longer says");
}

static bool theCxxFrontendModelIsInUse()
{
#ifdef QTC_WITH_CXX_FRONTEND
    return cxxFrontendModelRequested();
#else
    // The store, the index's description of a header and the readings these
    // count are all that model's, and it is not built here.
    return false;
#endif
}

// What a file includes, answered out of the index: no pass has read it here
// and no reading is made, and the answer is still right.
//
// This is what clangd does with every cross-file question -- one parse per
// translation unit ever, and queries served from what that parse was
// distilled into. A *header* is the case that needs more than the store: a
// shard is written per translation unit and a header is never one, being read
// into every unit that includes it. So what a header reaches comes from the
// include graph the index keeps, a node per file with the files it includes
// itself, which is clangd's IncludeGraph and is here for the reason clangd
// has it. A framework's scan asks this of every file of a project, headers
// and all, and reading one is a parse of it and everything it reaches.
void ModelManagerTest::testTheIncludeClosureOfAHeader()
{
    if (!theCxxFrontendModelIsInUse())
        QSKIP("Only this model's index keeps an include graph");

    TemporaryDir dir;
    QVERIFY(dir.isValid());
    // Two headers deep, so that what comes back is a walk of the graph
    // rather than the one include the middle header writes.
    const FilePath leaf = dir.createFile("leaf.h", "class Leaf {};\n");
    const FilePath middle = dir.createFile("middle.h", "#include \"leaf.h\"\n"
                                                       "class Middle {};\n");
    // And one the graph knows of but which includes nothing, since an empty
    // answer is an answer: a leaf must not read as a file the index has
    // never heard of.
    const FilePath source = dir.createFile("unit.cpp", "#include \"middle.h\"\n"
                                                       "class Unit {};\n");
    QVERIFY(!leaf.isEmpty() && !middle.isEmpty() && !source.isEmpty());

    CppLocatorData * const locatorData = CppModelManager::locatorData();
    QVERIFY(locatorData);

    // Only the source is indexed -- the headers are covered by its reading,
    // which is the whole point.
    QVERIFY(CppEditor::Tests::TestCase::parseFiles({source}));
    QVERIFY(QTest::qWaitFor([locatorData] {
        return locatorData->cxxFrontendFilesOutstanding() == 0;
    }, 60000));

    // An empty snapshot, so the built-in answer cannot be the one that comes
    // back, and an empty working copy, so the files count as ones nobody is
    // editing.
#ifdef QTC_WITH_CXX_FRONTEND
    const int readBefore = cxxFrontendReadingsMade();
#endif
    const int servedBefore = locatorData->cxxFrontendClosuresServed();
    const CodeModelQueries read{CPlusPlus::Snapshot(), WorkingCopy()};
    // Sorted, because what order a closure comes back in is nobody's
    // contract: a walk of the graph answers in the order the includes are
    // written, and the front ends answer out of a set.
    QCOMPARE(Utils::sorted(read.includeClosureOf(source)), Utils::sorted(FilePaths({middle, leaf})));
    QCOMPARE(read.includeClosureOf(middle), FilePaths({leaf}));
    QCOMPARE(read.includeClosureOf(leaf), FilePaths());

    // And nothing was read to say so, which is the only thing that
    // distinguishes this from a front end answering the question: a reading
    // gives the same three answers, at a parse of a file and its headers
    // each.
#ifdef QTC_WITH_CXX_FRONTEND
    QCOMPARE(cxxFrontendReadingsMade(), readBefore);
#endif

    // Nor was a shard opened for it. The store can answer for a file it
    // holds a reading of, and a header is one only where nothing covered it;
    // what says the graph is what answered is that the store was not asked.
    QCOMPARE(locatorData->cxxFrontendClosuresServed(), servedBefore);
}

// The other question the same graph answers: what one file includes itself,
// which is a node of it rather than a walk. That is what the model editor
// draws its component dependencies from, and it asks it of headers -- so
// without the graph it has nothing to say wherever the built-in indexing
// pass has not run.
void ModelManagerTest::testWhatOneFileIncludes()
{
    if (!theCxxFrontendModelIsInUse())
        QSKIP("Only this model's index keeps an include graph");

    TemporaryDir dir;
    QVERIFY(dir.isValid());
    // Two headers deep again, so that a walk of the graph and a node of it
    // are told apart: the source reaches the leaf, and includes it not.
    const FilePath leaf = dir.createFile("leaf.h", "class Leaf {};\n");
    const FilePath middle = dir.createFile("middle.h", "#include \"leaf.h\"\n"
                                                       "class Middle {};\n");
    const FilePath source = dir.createFile("unit.cpp", "#include \"middle.h\"\n"
                                                       "class Unit {};\n");
    QVERIFY(!leaf.isEmpty() && !middle.isEmpty() && !source.isEmpty());

    CppLocatorData * const locatorData = CppModelManager::locatorData();
    QVERIFY(locatorData);

    QVERIFY(CppEditor::Tests::TestCase::parseFiles({source}));
    QVERIFY(QTest::qWaitFor([locatorData] {
        return locatorData->cxxFrontendFilesOutstanding() == 0;
    }, 60000));

    QCOMPARE(locatorData->indexedDirectIncludesFor(source), FilePaths({middle}));
    QCOMPARE(locatorData->indexedDirectIncludesFor(middle), FilePaths({leaf}));
    // Covered and including nothing, which is an answer -- and a different
    // one from a file the index has never heard of.
    QCOMPARE(locatorData->indexedDirectIncludesFor(leaf), FilePaths());
    QCOMPARE(locatorData->indexedDirectIncludesFor(dir.filePath() / "absent.h"), std::nullopt);
}

// What a test class declares, answered out of the index and the class's own
// tokens: no pass has read the file, no translation unit is read, and the
// answer is the one a reading gives.
//
// The other half of what clangd does with a cross-file question. The closure
// above comes out of the index's store; this is the rest of the pattern --
// the index says which file writes the class, and where a text-level fact
// about a file nobody parsed is what is wanted, the file is lexed. Reading it
// instead is a parse of it and every header it reaches, and a test
// framework's scan asks this of every test class and of every class those
// derive from.
void ModelManagerTest::testTheIndexedClassShape()
{
    if (!theCxxFrontendModelIsInUse())
        QSKIP("Only this model's index describes the headers a unit read");

    TemporaryDir dir;
    QVERIFY(dir.isValid());

    // A Qt test class as one is really written, with what a lexer has to see
    // past: a class named through an export macro and inside a namespace, a
    // constructor and a destructor, the macros a class body uses, a slot
    // defined where it is declared, a nested class of its own with private
    // slots, and sections that are not private slots at all.
    const FilePath header = dir.createFile(
        "tst_shape.h",
        "#pragma once\n"                                        // 1
        "#define Q_OBJECT\n"                                    // 2
        "#define Q_CLASSINFO(name, value)\n"                     // 3
        "#define Q_SLOT\n"                                       // 4
        "#define TESTED_EXPORT\n"                                // 5
        "class QObject {};\n"                                    // 6
        "class tst_Base : public QObject\n"                      // 7
        "{\n"                                                    // 8
        "private slots:\n"                                        // 9
        "    void inherited();\n"                                // 10
        "};\n"                                                   // 11
        "namespace NS {\n"                                       // 12
        "class tst_Shape;\n"                                     // 13, declared first
        "class TESTED_EXPORT tst_Shape : public tst_Base\n"      // 14
        "{\n"                                                    // 15
        "    Q_OBJECT\n"                                         // 16
        "    Q_CLASSINFO(\"a\", \"b\")\n"                        // 17
        "public:\n"                                              // 18
        "    tst_Shape();\n"                                     // 19
        "    ~tst_Shape();\n"                                    // 20
        "    void notASlot();\n"                                 // 21
        "public slots:\n"                                        // 22
        "    void notPrivate();\n"                               // 23
        "private slots:\n"                                       // 24
        "    void testOne();\n"                                  // 25
        "    void testTwo_data();\n"                             // 26
        "    void testTwo();\n"                                  // 27
        "    void withArguments(int one, int two);\n"            // 28
        "    void writtenHere() { notASlot(); }\n"               // 29
        "    Q_CLASSINFO(\"c\", \"d\")\n"                        // 30
        "private:\n"                                             // 31
        "    Q_SLOT void markedOnItsOwn();\n"                    // 32
        "    void notASlotEither();\n"                           // 33
        "    class Nested\n"                                     // 34
        "    {\n"                                                // 35
        "    private slots:\n"                                    // 36
        "        void notOurs();\n"                              // 37
        "    };\n"                                               // 38
        "};\n"                                                   // 39
        "} // namespace NS\n"                                    // 40
        "template<int V> class Valued {};\n"                     // 41
        "constexpr int Bound = 2;\n"                             // 42
        "class tst_Deep : public Valued<(Bound > 1 ? 2 : 3)>\n"  // 43
        "{\n"                                                    // 44
        "private slots:\n"                                        // 45
        "    void deep();\n"                                     // 46
        "};\n"                                                   // 47
        "class Twin {};\n"                                       // 48
        "namespace NS { class Twin {}; }\n");                    // 49
    const FilePath source = dir.createFile("tst_shape.cpp",
                                           "#include \"tst_shape.h\"\n"
                                           "int main() { NS::tst_Shape shape; }\n");
    QVERIFY(!header.isEmpty() && !source.isEmpty());

    CppLocatorData * const locatorData = CppModelManager::locatorData();
    QVERIFY(locatorData);

    // Indexed, which is what describes the header the unit read.
    QVERIFY(CppEditor::Tests::TestCase::parseFiles({source}));
    QVERIFY(QTest::qWaitFor([locatorData] {
        return locatorData->cxxFrontendFilesOutstanding() == 0;
    }, 60000));

    // Asked with no reading of its own to fall back on: an empty snapshot,
    // so the built-in answer cannot be the one that comes back, and an empty
    // working copy, so the files count as ones nobody is editing.
#ifdef QTC_WITH_CXX_FRONTEND
    const int readBefore = cxxFrontendReadingsMade();
#endif
    const CodeModelQueries read{CPlusPlus::Snapshot(), WorkingCopy()};
    const auto said = [&read, &source](const QString &className) {
        const CodeModelQueries::ClassWithPrivateSlots found
            = read.classWithPrivateSlots(source, className);
        if (!found.klass.isValid())
            return QString("nothing");
        QStringList declared; // "slots" is one of Qt's own macros here
        for (const WrittenFunction &slot : found.privateSlots)
            declared << QString("%1 at %2").arg(slot.signature).arg(slot.line);
        return QString("%1 at %2:%3 | %4 | bases: %5")
            .arg(found.klass.qualifiedName, found.klass.filePath.fileName())
            .arg(found.klass.line)
            .arg(declared.join(", "), found.baseClasses.join(", "));
    };

    // The private slots in the order they are declared and nothing else, the
    // class found through the header the source file includes, and the base
    // as the class names it.
    //
    // The class is declared before it is written, and the index keeps an
    // entry for that declaration as much as for the class: it declares
    // nothing, so the entry that does is the one answered from.
    //
    // Qt's other spelling for a slot is among them: a member marked one on
    // its own in a private section rather than a section of them.
    QCOMPARE(said("NS::tst_Shape"),
             QString("NS::tst_Shape at tst_shape.h:14 | testOne() at 25, "
                     "testTwo_data() at 26, testTwo() at 27, "
                     "withArguments(int one, int two) at 28, writtenHere() at 29, "
                     "markedOnItsOwn() at 32 | bases: tst_Base"));

    // And the base, asked for by the name the class above named it with --
    // which is how a runner walks a hierarchy.
    QCOMPARE(said("tst_Base"),
             QString("tst_Base at tst_shape.h:7 | inherited() at 10 | bases: QObject"));

    // A base clause may write a ">" that closes no bracket, and what nests
    // has to survive it: a scan that loses count never sees the body begin.
    QCOMPARE(said("tst_Deep"),
             QString("tst_Deep at tst_shape.h:43 | deep() at 46 "
                     "| bases: Valued<(Bound > 1 ? 2 : 3)>"));

    // And both without reading a translation unit, which is the whole point
    // and the one thing the answers alone do not say: a reading of the source
    // file answers them correctly too, and costs a parse of it and every
    // header it reaches.
#ifdef QTC_WITH_CXX_FRONTEND
    QCOMPARE(cxxFrontendReadingsMade(), readBefore);
#endif

    // A name the index has nothing under is not answered for out of it: that
    // the index has no such class is no proof that the file writes none --
    // one a macro's body wrote is exactly that -- so the question is passed
    // on, and reading is what it costs.
    QCOMPARE(said("NS::tst_Missing"), QString("nothing"));
#ifdef QTC_WITH_CXX_FRONTEND
    const int afterTheMissingOne = cxxFrontendReadingsMade();
    QVERIFY2(afterTheMissingOne > readBefore,
             "a class the index does not have was answered for without reading");
#endif

    // And neither is a name two classes are really written under. The index
    // is asked with a name as the code writes it, which for a base class is
    // as little as whoever derives from it bothered with, so one file
    // declaring both Twin and NS::Twin is a question only a reading settles.
    read.classWithPrivateSlots(source, "Twin");
#ifdef QTC_WITH_CXX_FRONTEND
    QVERIFY2(cxxFrontendReadingsMade() > afterTheMissingOne,
             "a name two classes are written under was answered for without reading");
#endif
}

// The tags a data function writes, read off the file's own tokens: which
// function a call stands in is which braces it is written between, and the
// tag is the literal it is handed.
//
// Nothing is indexed or parsed here. This is the last of the questions a
// test framework's scan asks that used to cost a translation unit.
void ModelManagerTest::testTheTagsADataFunctionWrites()
{
    if (!theCxxFrontendModelIsInUse())
        QSKIP("Only this model reads a data function's calls off the tokens");

    TemporaryDir dir;
    QVERIFY(dir.isValid());
    const FilePath source = dir.createFile(
        "tags.cpp",
        "namespace QTest { void newRow(const char *); }\n"   // 1, declared here
        "namespace NS {\n"                                   // 2
        "class Thing\n"                                      // 3
        "{\n"                                                // 4
        "private slots:\n"                                    // 5
        "    void inlineRows_data()\n"                       // 6
        "    {\n"                                            // 7
        "        QTest::newRow(\"inline\");\n"               // 8
        "    }\n"                                            // 9
        "    void rows_data();\n"                            // 10
        "};\n"                                               // 11
        "void Thing::rows_data()\n"                          // 12
        "{\n"                                                // 13
        "    QTest::newRow(\"first\") << 1;\n"               // 14
        "    newRow(\"unqualified\");\n"                     // 15
        "    QTest::newRow(\"format %1\", 2);\n"             // 16
        "    Other::newRow(\"somebody else's\");\n"          // 17
        "}\n"                                                // 18
        "} // namespace NS\n"                                // 19
        "#define OPENS_A_BRACE namespace Unseen {\n"          // 20, and the
        "template <class T> void addRows()\n"                // 21 brace in it
        "{\n"                                                // 22 opens nothing
        "#ifdef SOMETHING_UNSET\n"                           // 23
        "    QTest::newRow(\"in a branch\");\n"              // 24
        "#endif\n"                                           // 25
        "    QTest::newRow(\"after a directive\");\n"        // 26
        "}\n");                                              // 27
    QVERIFY(!source.isEmpty());

#ifdef QTC_WITH_CXX_FRONTEND
    const int readBefore = cxxFrontendReadingsMade();
#endif
    const CodeModelQueries read{CPlusPlus::Snapshot(), WorkingCopy()};

    // Put together rather than formatted: a tag written with a "%1" in it is
    // one of the rows below, and arg() would take that for a place of its
    // own to substitute into.
    QStringList said;
    for (const CodeModelQueries::WrittenCall &call : read.callsTo(source, {"QTest::newRow"})) {
        said << call.arguments.join("+") + " in " + call.insideFunction + " at "
                    + QString::number(call.line) + ":" + QString::number(call.column);
    }

    // In the order they are written: a data function defined in its class
    // and one defined outside it, a call reached through a using directive,
    // a second argument that is no literal and says nothing, and a call in
    // a free function. Not the declaration on the first line, and not
    // somebody else's function of the same name.
    QCOMPARE(said.join("\n"),
             QString("inline in NS::Thing::inlineRows_data at 8:9\n"
                     "first in NS::Thing::rows_data at 14:5\n"
                     "unqualified in NS::Thing::rows_data at 15:5\n"
                     "format %1+ in NS::Thing::rows_data at 16:5\n"
                     "in a branch in addRows at 24:5\n"
                     "after a directive in addRows at 26:5"));

    // And no reading, which is the one thing the answer does not say.
#ifdef QTC_WITH_CXX_FRONTEND
    QCOMPARE(cxxFrontendReadingsMade(), readBefore);
#endif

    // A call in a scope the text does not name -- a lambda -- is not
    // answered for out of the tokens: a caller keeping what a "_data"
    // function writes would drop a call it cannot place, and a dropped call
    // is a tag nobody can be sent to.
    const FilePath inALambda = dir.createFile(
        "in_a_lambda.cpp",
        "void rows_data()\n"
        "{\n"
        "    auto write = [] { QTest::newRow(\"hidden\"); };\n"
        "    write();\n"
        "}\n");
    QVERIFY(!inALambda.isEmpty());
    const CodeModelQueries readAgain{CPlusPlus::Snapshot(), WorkingCopy()};
    readAgain.callsTo(inALambda, {"QTest::newRow"});
#ifdef QTC_WITH_CXX_FRONTEND
    const int afterTheLambda = cxxFrontendReadingsMade();
    QVERIFY2(afterTheLambda > readBefore,
             "a call in a scope the tokens do not name was answered for without reading");
#endif

    // Nor is a block a macro opens: it reads like a function's body, so
    // naming it after the macro would leave the answer looking complete.
    const FilePath inAMacrosBlock = dir.createFile(
        "in_a_macros_block.cpp",
        "TEST_F(Suite, Case)\n"
        "{\n"
        "    QTest::newRow(\"in somebody's block\");\n"
        "}\n");
    QVERIFY(!inAMacrosBlock.isEmpty());
    const CodeModelQueries readAThird{CPlusPlus::Snapshot(), WorkingCopy()};
    readAThird.callsTo(inAMacrosBlock, {"QTest::newRow"});
#ifdef QTC_WITH_CXX_FRONTEND
    const int afterTheBlock = cxxFrontendReadingsMade();
    QVERIFY2(afterTheBlock > afterTheLambda,
             "a call in a block a macro opened was answered for without reading");
#endif

    // And neither is a file that writes a macro around the call itself:
    // where the rows are is not something its tokens say any more.
    const FilePath throughAMacro = dir.createFile(
        "through_a_macro.cpp",
        "#define ROW(tag) QTest::newRow(tag)\n"
        "void rows_data()\n"
        "{\n"
        "    ROW(\"through a macro\");\n"
        "}\n");
    QVERIFY(!throughAMacro.isEmpty());
    const CodeModelQueries readAFourth{CPlusPlus::Snapshot(), WorkingCopy()};
    readAFourth.callsTo(throughAMacro, {"QTest::newRow"});
#ifdef QTC_WITH_CXX_FRONTEND
    QVERIFY2(cxxFrontendReadingsMade() > afterTheBlock,
             "a file that writes a macro around the call was answered for without reading");
#endif
}

// Where the project defines the function declared at a place, out of the
// index: the file that defines it is never parsed, and the answer is the one
// a search of the project gives.
//
// clangd's rule for a cross-file question -- served from the index, never by
// parsing a closed file. The reading this stands in front of parses the file
// asked about and then as many of its likely counterparts as its bound
// allows, so the index is both cheaper and more complete.
void ModelManagerTest::testTheIndexedDefinition()
{
    if (!theCxxFrontendModelIsInUse())
        QSKIP("Only this model's index is asked for a definition");

    TemporaryDir dir;
    QVERIFY(dir.isValid());
    const FilePath header = dir.createFile("counted.h",
                                           "#pragma once\n"                       // 1
                                           "namespace NS {\n"                     // 2
                                           "class Counted\n"                      // 3
                                           "{\n"                                  // 4
                                           "public:\n"                            // 5
                                           "    void definedElsewhere();\n"       // 6
                                           "    void definedHere() { }\n"         // 7
                                           "    void definedNowhere();\n"         // 8
                                           "};\n"                                 // 9
                                           "} // namespace NS\n");                // 10
    const FilePath source = dir.createFile("counted.cpp",
                                           "#include \"counted.h\"\n"             // 1
                                           "namespace NS {\n"                     // 2
                                           "void Counted::definedElsewhere()\n"   // 3
                                           "{\n"                                  // 4
                                           "}\n"                                  // 5
                                           "} // namespace NS\n");                // 6
    QVERIFY(!header.isEmpty() && !source.isEmpty());

    CppLocatorData * const locatorData = CppModelManager::locatorData();
    QVERIFY(locatorData);
    QVERIFY(CppEditor::Tests::TestCase::parseFiles({source}));
    QVERIFY(QTest::qWaitFor([locatorData] {
        return locatorData->cxxFrontendFilesOutstanding() == 0;
    }, 60000));

#ifdef QTC_WITH_CXX_FRONTEND
    const int readBefore = cxxFrontendReadingsMade();
#endif
    const CodeModelQueries read{CPlusPlus::Snapshot(), WorkingCopy()};

    // The declaration is in the header and the definition in the source
    // file, which is the whole question -- and the name the index is asked
    // with is what the tokens around the declaration say it is: the
    // namespace, the class, and the name itself.
    const Link elsewhere = read.definitionOfFunctionAt(header, 6, 10);
    QCOMPARE(elsewhere.targetFilePath, source);
    QCOMPARE(elsewhere.target.line, 3);

    // One defined where it is declared is its own definition.
    const Link here = read.definitionOfFunctionAt(header, 7, 10);
    QCOMPARE(here.targetFilePath, header);
    QCOMPARE(here.target.line, 7);

    // And neither is read for, which is the one thing the answers do not
    // say.
#ifdef QTC_WITH_CXX_FRONTEND
    QCOMPARE(cxxFrontendReadingsMade(), readBefore);
#endif

    // A function nothing defines is not answered for out of the index: that
    // it holds no definition is no proof that the project has none -- a file
    // it has not indexed yet is exactly that -- so the question is passed on.
    read.definitionOfFunctionAt(header, 8, 10);
#ifdef QTC_WITH_CXX_FRONTEND
    const int afterTheMissingOne = cxxFrontendReadingsMade();
    QVERIFY2(afterTheMissingOne > readBefore,
             "a definition the index does not have was answered for without reading");
#endif

    // Nor is one whose file has moved on since it was indexed. An entry says
    // where the definition stood when the file was last read for the index,
    // and two lines typed in above it put it somewhere else -- so the place
    // is checked against the file as it stands, which for a file somebody
    // has open is the buffer. A link into the wrong line is worse than the
    // reading this then declines to, which reads the file itself.
    const QByteArray asIndexed = source.fileContents().value_or(QByteArray());
    QVERIFY(!asIndexed.isEmpty());
    WorkingCopy beingEdited;
    beingEdited.insert(source, "\n\n" + asIndexed);
    const CodeModelQueries readAgain{CPlusPlus::Snapshot(), beingEdited};
#ifdef QTC_WITH_CXX_FRONTEND
    const int beforeTheEdited = cxxFrontendReadingsMade();
#endif
    readAgain.definitionOfFunctionAt(header, 6, 10);
#ifdef QTC_WITH_CXX_FRONTEND
    QVERIFY2(cxxFrontendReadingsMade() > beforeTheEdited,
             "a place the file no longer has the function at was handed back");
#endif
}

// Which class a test runs, read off the main() that says so: a file nobody
// has parsed, no reading made, and a class of its own for each of the ways a
// runner is handed one.
//
// Nothing is indexed here on purpose. This is the other half of clangd's
// rule -- where a text-level fact about a file is what is wanted, lex the
// file -- and most of the files this is asked about call no runner at all,
// which is an empty answer for the price of a lex.
void ModelManagerTest::testTheClassesHandedToARunner()
{
    if (!theCxxFrontendModelIsInUse())
        QSKIP("Only this model reads a runner's classes off the tokens");

    TemporaryDir dir;
    QVERIFY(dir.isValid());
    const FilePath source = dir.createFile(
        "main.cpp",
        "namespace QTest { int qExec(void *, int, char **); }\n" // declared, never called
        "namespace NS { class tst_One {}; }\n"
        "class tst_Two {};\n"
        "class tst_Three {};\n"
        "class tst_Four {};\n"
        "class tst_Five {};\n"
        "class tst_Six {};\n"
        "int byValue(int);\n"
        "using namespace QTest;\n"
        "int main(int argc, char **argv)\n"
        "{\n"
        "    NS::tst_One one;\n"
        "    tst_Two *two = new tst_Two;\n"
        "    tst_Four four;\n"
        "    tst_Five five;\n"
        "    tst_Six six;\n"
        "    QTest::qExec(&one, argc, argv);\n"   // the address of an object
        "    QTest::qExec(two, argc, argv);\n"    // a pointer that holds one
        "    QTest::qExec(new tst_Three, argc, argv);\n" // one made right there
        "    qExec(&four, argc, argv);\n"         // as a using directive leaves it
        "    Other::qExec(&five, argc, argv);\n"  // somebody else's function
        "#ifdef SOMETHING_UNSET\n"                // the same class in both
        "    QTest::qExec(&one, argc, argv);\n"   // branches of an #ifdef, which
        "#else\n"                                 // is one class and not two
        "    QTest::qExec(&one, argc, argv);\n"
        "#endif\n"
        "    QTest::qExec(&six, argc, argv);\n"   // and one under a directive,
        "    byValue(argc);\n"                    // whose "endif" is a name too
        "    return 0;\n"
        "}\n");
    QVERIFY(!source.isEmpty());

#ifdef QTC_WITH_CXX_FRONTEND
    const int readBefore = cxxFrontendReadingsMade();
#endif
    const CodeModelQueries read{CPlusPlus::Snapshot(), WorkingCopy()};

    // Each of the four ways one is handed over, once each and in the order
    // they are written. A class handed over in both branches of an #ifdef is
    // one class: a caller that finds several takes the file for one that runs
    // several tests, and takes the checkbox off all of them.
    QCOMPARE(read.classesPassedTo(source, "QTest::qExec").join(", "),
             QString("NS::tst_One, tst_Two, tst_Three, tst_Four, tst_Six"));
    QCOMPARE(read.classesPassedTo(source, "byValue"), QStringList());
    QCOMPARE(read.classesPassedTo(source, "QTest::qExecNot"), QStringList());

    // All of it off the tokens, which is the one thing the answers do not
    // say: a reading answers them too, and costs a parse of the file and
    // every header it reaches.
#ifdef QTC_WITH_CXX_FRONTEND
    QCOMPARE(cxxFrontendReadingsMade(), readBefore);
#endif

    // And a runner handed something the text does not settle is not answered
    // for out of it: what a function hands back is a question for a reading,
    // and reading is what it costs.
    const FilePath unsettled = dir.createFile(
        "unsettled.cpp",
        "#include \"made_elsewhere.h\"\n"
        "int main(int argc, char **argv) { return QTest::qExec(runner(), argc, argv); }\n");
    QVERIFY(!unsettled.isEmpty());
    const CodeModelQueries readAgain{CPlusPlus::Snapshot(), WorkingCopy()};
    readAgain.classesPassedTo(unsettled, "QTest::qExec");
#ifdef QTC_WITH_CXX_FRONTEND
    const int afterTheFirst = cxxFrontendReadingsMade();
    QVERIFY2(afterTheFirst > readBefore,
             "a runner handed what the text does not settle was answered for without reading");
#endif

    // Including an argument that only *begins* with a name: reading that
    // name's own declaration would answer about the wrong thing, and an
    // answer with the call left out of it reads as a file that runs no test
    // at all.
    const FilePath throughAMember = dir.createFile(
        "through_a_member.cpp",
        "class tst_Held {};\n"
        "struct Wrapper { tst_Held *held; };\n"
        "int main(int argc, char **argv)\n"
        "{\n"
        "    Wrapper wrapper;\n"
        "    return QTest::qExec(wrapper.held, argc, argv);\n"
        "}\n");
    QVERIFY(!throughAMember.isEmpty());
    const CodeModelQueries readOnce{CPlusPlus::Snapshot(), WorkingCopy()};
    readOnce.classesPassedTo(throughAMember, "QTest::qExec");
#ifdef QTC_WITH_CXX_FRONTEND
    const int afterTheMember = cxxFrontendReadingsMade();
    QVERIFY2(afterTheMember > afterTheFirst,
             "a runner handed a member of something was answered for without reading");
#endif

    // And neither is a file that writes a macro around the call: where the
    // runner is called is not something its tokens say any more.
    const FilePath throughAMacro = dir.createFile(
        "through_a_macro.cpp",
        "#define RUN(test) QTest::qExec(test, argc, argv)\n"
        "class tst_Run {};\n"
        "int main(int argc, char **argv)\n"
        "{\n"
        "    tst_Run run;\n"
        "    return RUN(&run);\n"
        "}\n");
    QVERIFY(!throughAMacro.isEmpty());
    const CodeModelQueries readAgainOnce{CPlusPlus::Snapshot(), WorkingCopy()};
    readAgainOnce.classesPassedTo(throughAMacro, "QTest::qExec");
#ifdef QTC_WITH_CXX_FRONTEND
    QVERIFY2(cxxFrontendReadingsMade() > afterTheMember,
             "a file that writes a macro around the runner was answered for without reading");
#endif
}

// What indexing a real project costs, which is the only apples-to-apples way
// to compare the two models: the same project, the same kit, in the editor
// that will do it.
//
// Skipped unless QTC_INDEX_PROJECT names a project file, so it costs a
// normal run nothing. Raise QTEST_FUNCTION_TIMEOUT for anything large, or the
// run is killed as a failure rather than measured.
void ModelManagerTest::testIndexingCost()
{
    const FilePath projectFile = FilePath::fromUserInput(
        qtcEnvironmentVariable("QTC_INDEX_PROJECT"));
    if (projectFile.isEmpty())
        QSKIP("Set QTC_INDEX_PROJECT to a project file to measure indexing");

    Kit * const kit = Utils::findOr(KitManager::kits(), nullptr,
                                    [](const Kit *k) { return k->isValid(); });
    if (!kit)
        QSKIP("The measurement requires a valid kit");

    bool refreshed = false;
    QObject context;
    connect(CppModelManager::instance(), &CppModelManager::sourceFilesRefreshed,
            &context, [&refreshed] { refreshed = true; });

    CppLocatorData * const locatorData = CppModelManager::locatorData();
    QVERIFY(locatorData);

    QElapsedTimer timer;
    timer.start();

    // When the other model's index is complete, watched for rather than
    // waited on: where the project's own file list drives it, it finishes
    // before the built-in pass does and the wait below would never see it.
    //
    // A clock is read here to *timestamp* an event, not to wait for one --
    // nothing is asserted on this, and the waits either side of it are on
    // the state itself.
    qint64 cxxElapsed = -1;
    bool cxxBegun = false;
    QTimer watch;
    watch.setInterval(5);
    connect(&watch, &QTimer::timeout, &context, [&] {
        if (locatorData->cxxFrontendCacheHits() + locatorData->cxxFrontendCacheMisses() > 0)
            cxxBegun = true;
        if (cxxElapsed < 0 && cxxBegun && locatorData->cxxFrontendFilesOutstanding() == 0)
            cxxElapsed = timer.elapsed();
    });
    watch.start();

    ProjectOpenerAndCloser projectMgr;
    const ProjectInfo::ConstPtr projectInfo = projectMgr.open(projectFile, kit);
    QVERIFY(projectInfo);

    // Not SourceFilesRefreshGuard: it gives up after ten seconds, which is
    // less than a real project's index takes.
    QTRY_VERIFY_WITH_TIMEOUT(refreshed, 3600000);
    const qint64 builtinElapsed = timer.elapsed();

    // The cxx front end's reads outlive that signal -- they are handed to a
    // pool as the indexer reports each file -- so the index is not complete
    // when it arrives, and timing to it alone would credit this model with
    // work it has not finished.
    QTRY_VERIFY_WITH_TIMEOUT(locatorData->cxxFrontendFilesOutstanding() == 0, 3600000);
    const qint64 indexElapsed = timer.elapsed();
    watch.stop();


    // What a session would pay to have the whole index out of the store with
    // no built-in pass behind it, where QTC_INDEX_STOREONLY asks for it.
    //
    // This is the number the whole store exists for and the one thing the
    // harness could not say: every measurement above rides on the built-in
    // indexer, which parses every file of the project every session and has
    // no store of its own. Here each of the project's translation units is
    // asked of the store and nothing else -- every shard checked against a
    // digest of each file it names, and not a line parsed.
#ifdef QTC_WITH_CXX_FRONTEND
    if (qtcEnvironmentVariableIsSet("QTC_INDEX_STOREONLY")) {
        // Timed on one thread and on as many as the index itself uses,
        // because the second is what a session would really pay: the store is
        // consulted on the pool, and in a warm run that work hides behind the
        // built-in pass entirely.
        const QStringList macros = cxxFrontendIndexInputs().predefinedMacros;
        const QSet<FilePath> sources = projectInfo->sourceFiles();

        // The keys first, so that what is timed below is the store and not
        // the project's data, which belongs to this thread anyway.
        QList<std::pair<FilePath, QByteArray>> asked;
        asked.reserve(sources.size());
        for (const FilePath &source : sources)
            asked.append({source, cxxFrontendProjectKey(source)});

        // And once more over a store whose digests are already remembered,
        // which separates reading the shards from checking them: a shard
        // names a thousand files and each has to be digested before it can
        // be believed.
        {
            const CxxFrontendIndexCache twice(macros);
            QThreadPool pool;
            pool.setMaxThreadCount(6);
            const auto sweep = [&] {
                return QtConcurrent::blockingMapped(
                    &pool, asked, [&twice](const std::pair<FilePath, QByteArray> &one) {
                        return twice.take(one.first, one.second) ? 1 : 0;
                    });
            };
            sweep();
            QElapsedTimer again;
            again.start();
            sweep();
            qInfo() << "StoreOnly: shards alone" << again.elapsed()
                    << "ms on 6 readers, the digests already remembered";
        }

        for (const int readers : {1, 6}) {
            const CxxFrontendIndexCache store(macros);
            QThreadPool pool;
            pool.setMaxThreadCount(readers);
            QElapsedTimer fromStore;
            fromStore.start();
            const QList<int> described = QtConcurrent::blockingMapped(
                &pool, asked, [&store](const std::pair<FilePath, QByteArray> &one) {
                    const std::optional<CxxFrontendIndexRead> read
                        = store.take(one.first, one.second);
                    return read ? int(read->files.size()) : 0;
                });
            qInfo() << "StoreOnly:" << fromStore.elapsed() << "ms on" << readers
                    << "readers, of" << sources.size() << "units, hits" << store.hits()
                    << "misses" << store.misses() << "files described"
                    << std::accumulate(described.begin(), described.end(), 0);
        }
    }
#endif

    // What typing costs the index, where QTC_INDEX_TYPING asks for it.
    //
    // A real reparse and not a refresh pretending to be one: the text is
    // changed and the editor's own parser runs, which is what reports the
    // document and everything read into it. Written because two rounds of
    // guessing at this cost got it wrong in both directions -- the answer
    // is that an unsaved edit costs no reading at all, the file on disk
    // being unchanged and the store answering for it.
    if (qtcEnvironmentVariableIsSet("QTC_INDEX_TYPING")) {
        const FilePath open = projectInfo->sourceFiles().values().first();
        TextEditor::BaseTextEditor *editor = nullptr;
        QVERIFY(CppEditor::Tests::TestCase::openCppEditor(open, &editor));
        QVERIFY(CppEditor::Tests::TestCase::waitForProcessedEditorDocument(open));
        QTRY_VERIFY_WITH_TIMEOUT(locatorData->cxxFrontendFilesOutstanding() == 0, 600000);

        // Bound on the document really being read again, not on a pause
        // in the clock: the editor reparses on an idle timer, so asking
        // whether its parser is running answers no before it has begun.
        int parses = 0;
        connect(CppModelManager::instance(), &CppModelManager::documentUpdated,
                &context, [&parses, open](const CPlusPlus::Document::Ptr &document) {
                    if (document->filePath() == open)
                        ++parses;
                });

        const int readBefore = locatorData->cxxFrontendCacheMisses();
        QElapsedTimer typing;
        typing.start();
        const int pauses = 5;
        for (int i = 0; i < pauses; ++i) {
            const int was = parses;
            QTextCursor cursor = editor->editorWidget()->textCursor();
            cursor.movePosition(QTextCursor::End);
            cursor.insertText(QString("\nint typed%1;\n").arg(i));
            QTRY_VERIFY_WITH_TIMEOUT(parses > was, 600000);
            QTRY_VERIFY_WITH_TIMEOUT(locatorData->cxxFrontendFilesOutstanding() == 0, 600000);
        }
        // Put back what was typed, so that what is left behind is a
        // document nobody has to be asked about on the way out.
        for (int i = 0; i < pauses; ++i)
            editor->editorWidget()->undo();
        QVERIFY(!editor->document()->isModified());

        qInfo().noquote()
            << QString("Typing: file=%1 pauses=%2 readings=%3 elapsed=%4ms")
                   .arg(open.fileName()).arg(pauses)
                   .arg(locatorData->cxxFrontendCacheMisses() - readBefore)
                   .arg(typing.elapsed());
    }

    qInfo().noquote() << QString("IndexingCost: files=%1 builtin=%2ms index=%3ms cxx=%4ms "
                                 "stored=%5 read=%6")
                             .arg(projectInfo->sourceFiles().size())
                             .arg(builtinElapsed)
                             .arg(indexElapsed)
                             .arg(cxxElapsed)
                             .arg(locatorData->cxxFrontendCacheHits())
                             .arg(locatorData->cxxFrontendCacheMisses());
}

} // CppEditor::Internal
