// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmakeparsernodes.h"

#include "qmakeproject.h"
#include "qmakeprojectmanagertr.h"

#include <android/androidconstants.h>

#include <coreplugin/documentmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/iversioncontrol.h>
#include <coreplugin/vcsmanager.h>

#include <projectexplorer/editorconfiguration.h>
#include <projectexplorer/extracompiler.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/target.h>

#include <qtsupport/profilereader.h>

#include <texteditor/icodestylepreferences.h>
#include <texteditor/tabsettings.h>
#include <texteditor/texteditorsettings.h>

#include <utils/algorithm.h>
#include <utils/async.h>
#include <utils/filepath.h>
#include <utils/filesystemwatcher.h>
#include <utils/fileutils.h>
#include <utils/mimeconstants.h>
#include <utils/mimeutils.h>
#include <utils/qtcprocess.h>
#include <utils/qtcassert.h>
#include <utils/stringutils.h>
#include <utils/temporarydirectory.h>

#include <QLoggingCategory>
#include <QMessageBox>
#include <QXmlStreamWriter>

using namespace Core;
using namespace ProjectExplorer;
using namespace QmakeProjectManager;
using namespace QmakeProjectManager::Internal;
using namespace QMakeInternal;
using namespace Utils;

namespace QmakeProjectManager {

static Q_LOGGING_CATEGORY(qmakeParse, "qtc.qmake.parsing", QtWarningMsg);

size_t qHash(Variable key, uint seed) { return ::qHash(static_cast<int>(key), seed); }
size_t qHash(FileOrigin fo) { return ::qHash(int(fo)); }

namespace Internal {

Q_LOGGING_CATEGORY(qmakeNodesLog, "qtc.qmake.nodes", QtWarningMsg)

class QmakeEvalInput
{
public:
    FilePath projectDir;
    FilePath projectFilePath;
    FilePath buildDirectory;
    FilePath sysroot;
    QtSupport::ProFileReader *readerExact;
    QtSupport::ProFileReader *readerCumulative;
    QMakeGlobals *qmakeGlobals;
    QMakeVfs *qmakeVfs;
    QSet<FilePath> parentFilePaths;
    bool includedInExcactParse;
};

class QmakePriFileEvalResult
{
public:
    QSet<FilePath> folders;
    QSet<FilePath> recursiveEnumerateFiles;
    QMap<FileType, QSet<FilePath>> foundFilesExact;
    QMap<FileType, QSet<FilePath>> foundFilesCumulative;
};

class QmakeIncludedPriFile
{
public:
    ProFile *proFile;
    FilePath name;
    QmakePriFileEvalResult result;
    bool fromExactParse = true;
    QMap<FilePath, QmakeIncludedPriFile *> children;

    ~QmakeIncludedPriFile()
    {
        qDeleteAll(children);
    }
};

class QmakeEvalResult
{
public:
    ~QmakeEvalResult() { qDeleteAll(directChildren); }

    enum EvalResultState { EvalAbort, EvalFail, EvalPartial, EvalOk };
    EvalResultState state;
    ProjectType projectType;

    FilePaths subProjectsNotToDeploy;
    QSet<FilePath> exactSubdirs;
    QmakeIncludedPriFile includedFiles;
    TargetInformation targetInformation;
    InstallsList installsList;
    QHash<Variable, QStringList> newVarValues;
    QStringList errors;
    QSet<QString> directoriesWithWildcards;
    QList<QmakePriFile *> directChildren;
    QList<QPair<QmakePriFile *, QmakePriFileEvalResult>> priFiles;
    QList<QmakeProFile *> proFiles;
};

} // namespace Internal

QmakePriFile::QmakePriFile(QmakeBuildSystem *buildSystem, QmakeProFile *qmakeProFile,
                           const FilePath &filePath) : m_filePath(filePath)
{
    finishInitialization(buildSystem, qmakeProFile);
}

QmakePriFile::QmakePriFile(const FilePath &filePath) : m_filePath(filePath) { }

void QmakePriFile::finishInitialization(QmakeBuildSystem *buildSystem, QmakeProFile *qmakeProFile)
{
    QTC_ASSERT(buildSystem, return);
    m_buildSystem = buildSystem;
    m_qmakeProFile = qmakeProFile;
}

FilePath QmakePriFile::directoryPath() const
{
    return filePath().parentDir();
}

QString QmakePriFile::deviceRoot() const
{
    if (!m_filePath.isLocal())
        return m_filePath.withNewPath("/").toFSPathString();
    return {};
}

QString QmakePriFile::displayName() const
{
    return filePath().completeBaseName();
}

QmakePriFile *QmakePriFile::parent() const
{
    return m_parent;
}

QmakeProject *QmakePriFile::project() const
{
    return static_cast<QmakeProject *>(m_buildSystem->project());
}

const QList<QmakePriFile *> QmakePriFile::children() const
{
    return m_children;
}

QmakePriFile *QmakePriFile::findPriFile(const FilePath &fileName)
{
    if (fileName == filePath())
        return this;
    for (QmakePriFile *n : std::as_const(m_children)) {
        if (QmakePriFile *result = n->findPriFile(fileName))
            return result;
    }
    return nullptr;
}

const QmakePriFile *QmakePriFile::findPriFile(const FilePath &fileName) const
{
    if (fileName == filePath())
        return this;
    for (const QmakePriFile *n : std::as_const(m_children)) {
        if (const QmakePriFile *result = n->findPriFile(fileName))
            return result;
    }
    return nullptr;
}

void QmakePriFile::makeEmpty()
{
    qDeleteAll(m_children);
    m_children.clear();
}

SourceFiles QmakePriFile::files(const FileType &type) const
{
    return m_files.value(type);
}

const QSet<FilePath> QmakePriFile::collectFiles(const FileType &type) const
{
    QSet<FilePath> allFiles = transform(files(type),
                                        [](const SourceFile &sf) { return sf.first; });
    for (const QmakePriFile * const priFile : m_children) {
        if (!dynamic_cast<const QmakeProFile *>(priFile))
            allFiles.unite(priFile->collectFiles(type));
    }
    return allFiles;
}

QmakePriFile::~QmakePriFile()
{
    watchFolders( {} );
    qDeleteAll(m_children);
}

void QmakePriFile::scheduleUpdate()
{
    QTC_ASSERT(m_buildSystem, return);
    QtSupport::ProFileCacheManager::instance()->discardFile(
        deviceRoot(), filePath().path(), m_buildSystem->qmakeVfs());
    m_qmakeProFile->scheduleUpdate(QmakeProFile::ParseLater);
}

QStringList QmakePriFile::baseVPaths(QtSupport::ProFileReader *reader, const QString &projectDir, const QString &buildDir)
{
    QStringList result;
    if (!reader)
        return result;
    result += reader->absolutePathValues(QLatin1String("VPATH"), projectDir);
    result << projectDir; // QMAKE_ABSOLUTE_SOURCE_PATH
    result << buildDir;
    result.removeDuplicates();
    return result;
}

QStringList QmakePriFile::fullVPaths(const QStringList &baseVPaths, QtSupport::ProFileReader *reader,
                                               const QString &qmakeVariable, const QString &projectDir)
{
    QStringList vPaths;
    if (!reader)
        return vPaths;
    vPaths = reader->absolutePathValues(QLatin1String("VPATH_") + qmakeVariable, projectDir);
    vPaths += baseVPaths;
    vPaths.removeDuplicates();
    return vPaths;
}

QSet<FilePath> QmakePriFile::recursiveEnumerate(const QString &folder)
{
    QSet<FilePath> result;
    QDir dir(folder);
    dir.setFilter(dir.filter() | QDir::NoDotAndDotDot);
    const QFileInfoList entries = dir.entryInfoList();
    for (const QFileInfo &file : entries) {
        if (file.isDir() && !file.isSymLink())
            result += recursiveEnumerate(file.absoluteFilePath());
        else if (!Core::EditorManager::isAutoSaveFile(file.fileName()))
            result += FilePath::fromFileInfo(file);
    }
    return result;
}

static QStringList fileListForVar(
        const QHash<QString, QList<ProFileEvaluator::SourceFile>> &sourceFiles,
        const QString &varName)
{
    const QList<ProFileEvaluator::SourceFile> &sources = sourceFiles[varName];
    QStringList result;
    result.reserve(sources.size());
    for (const ProFileEvaluator::SourceFile &sf : sources)
        result << sf.fileName;
    return result;
}

static void extractSources(const QString &device,
        QHash<int, QmakePriFileEvalResult *> proToResult, QmakePriFileEvalResult *fallback,
        const QList<ProFileEvaluator::SourceFile> &sourceFiles, FileType type, bool cumulative)
{
    for (const ProFileEvaluator::SourceFile &source : sourceFiles) {
        auto *result = proToResult.value(source.proFileId);
        if (!result)
            result = fallback;
        auto &foundFiles = cumulative ? result->foundFilesCumulative : result->foundFilesExact;
        foundFiles[type].insert(FilePath::fromUserInput(device + source.fileName));
    }
}

static void extractInstalls(const QString &device,
        QHash<int, QmakePriFileEvalResult *> proToResult, QmakePriFileEvalResult *fallback,
        const InstallsList &installList)
{
    for (const InstallsItem &item : installList.items) {
        for (const ProFileEvaluator::SourceFile &source : item.files) {
            auto *result = proToResult.value(source.proFileId);
            if (!result)
                result = fallback;
            result->folders.insert(FilePath::fromUserInput(device + source.fileName));
        }
    }
}

void QmakePriFile::processValues(QmakePriFileEvalResult &result)
{
    // Remove non existing items and non folders
    auto it = result.folders.begin();
    while (it != result.folders.end()) {
        QFileInfo fi((*it).toFileInfo());
        if (fi.exists()) {
            if (fi.isDir()) {
                result.recursiveEnumerateFiles += recursiveEnumerate((*it).toUrlishString());
                // keep directories
                ++it;
            } else {
                // move files directly to recursiveEnumerateFiles
                result.recursiveEnumerateFiles += (*it);
                it = result.folders.erase(it);
            }
        } else {
            // do remove non exsting stuff
            it = result.folders.erase(it);
        }
    }

    for (int i = 0; i < static_cast<int>(FileType::FileTypeSize); ++i) {
        auto type = static_cast<FileType>(i);
        for (QSet<FilePath> * const foundFiles
             : {&result.foundFilesExact[type], &result.foundFilesCumulative[type]}) {
            result.recursiveEnumerateFiles.subtract(*foundFiles);
            QSet<FilePath> newFilePaths = filterFilesProVariables(type, *foundFiles);
            newFilePaths += filterFilesRecursiveEnumerata(type, result.recursiveEnumerateFiles);
            *foundFiles = newFilePaths;
        }
    }
}

void QmakePriFile::update(const Internal::QmakePriFileEvalResult &result)
{
    m_recursiveEnumerateFiles = result.recursiveEnumerateFiles;
    watchFolders(result.folders);

    for (int i = 0; i < static_cast<int>(FileType::FileTypeSize); ++i) {
        const auto type = static_cast<FileType>(i);
        SourceFiles &files = m_files[type];
        files.clear();
        const QSet<FilePath> exactFps = result.foundFilesExact.value(type);
        for (const FilePath &exactFp : exactFps)
            files.insert({exactFp, FileOrigin::ExactParse});
        for (const FilePath &cumulativeFp : result.foundFilesCumulative.value(type)) {
            if (!exactFps.contains(cumulativeFp))
                files.insert({cumulativeFp, FileOrigin::CumulativeParse});
        }
    }
}

void QmakePriFile::watchFolders(const QSet<FilePath> &folders)
{
    const QSet<QString> folderStrings = Utils::transform(folders, &FilePath::toUrlishString);
    QSet<QString> toUnwatch = m_watchedFolders;
    toUnwatch.subtract(folderStrings);

    QSet<QString> toWatch = folderStrings;
    toWatch.subtract(m_watchedFolders);

    if (m_buildSystem) {
        // Check needed on early exit of QmakeProFile::applyEvaluate?
        m_buildSystem->unwatchFolders(Utils::toList(toUnwatch), this);
        m_buildSystem->watchFolders(Utils::toList(toWatch), this);
    }

    m_watchedFolders = folderStrings;
}

QString QmakePriFile::continuationIndent() const
{
    const EditorConfiguration *editorConf = project()->editorConfiguration();
    const TextEditor::TabSettings &tabSettings = editorConf->useGlobalSettings()
            ? TextEditor::TextEditorSettings::codeStyle()->tabSettings()
            : editorConf->codeStyle()->tabSettings();
    if (tabSettings.m_continuationAlignBehavior == TextEditor::TabSettings::ContinuationAlignWithIndent
            && tabSettings.m_tabPolicy == TextEditor::TabSettings::TabsOnlyTabPolicy) {
        return QString("\t");
    }
    return QString(tabSettings.m_indentSize, ' ');
}

QmakeBuildSystem *QmakePriFile::buildSystem() const
{
    return m_buildSystem;
}

bool QmakePriFile::knowsFile(const FilePath &filePath) const
{
    return m_recursiveEnumerateFiles.contains(filePath);
}

bool QmakePriFile::folderChanged(const QString &changedFolder, const QSet<FilePath> &newFiles)
{
    qCDebug(qmakeParse()) << "QmakePriFile::folderChanged";

    const QSet<FilePath> addedFiles = newFiles - m_recursiveEnumerateFiles;
    const QSet<FilePath> removedFiles = Utils::filtered(m_recursiveEnumerateFiles - newFiles,
                                                        [changedFolder](const FilePath &file) {
        return file.isChildOf(FilePath::fromString(changedFolder));
    });

    if (addedFiles.isEmpty() && removedFiles.isEmpty())
        return false;

    m_recursiveEnumerateFiles = newFiles;

    // Apply the differences per file type
    for (int i = 0; i < static_cast<int>(FileType::FileTypeSize); ++i) {
        auto type = static_cast<FileType>(i);
        const QSet<FilePath> add = filterFilesRecursiveEnumerata(type, addedFiles);
        const QSet<FilePath> remove = filterFilesRecursiveEnumerata(type, removedFiles);

        if (!add.isEmpty() || !remove.isEmpty()) {
            qCDebug(qmakeParse()) << "For type" << static_cast<int>(type) <<"\n"
                                  << "added files"  <<  add << "\n"
                                  << "removed files" << remove;
            SourceFiles &currentFiles = m_files[type];
            for (const FilePath &fp : add) {
                if (!contains(currentFiles, [&fp](const SourceFile &sf) { return sf.first == fp; }))
                    currentFiles.insert({fp, FileOrigin::ExactParse});
            }
            for (const FilePath &fp : remove) {
                const auto it = std::find_if(currentFiles.begin(), currentFiles.end(),
                                             [&fp](const SourceFile &sf) {
                    return sf.first == fp; });
                if (it != currentFiles.end())
                    currentFiles.erase(it);
            }
        }
    }
    return true;
}

bool QmakePriFile::deploysFolder(const FilePath &folder) const
{
    QString f = folder.toUrlishString();
    const QChar slash = QLatin1Char('/');
    if (!f.endsWith(slash))
        f.append(slash);

    for (const QString &wf : std::as_const(m_watchedFolders)) {
        if (f.startsWith(wf)
            && (wf.endsWith(slash)
                || (wf.length() < f.length() && f.at(wf.length()) == slash)))
            return true;
    }
    return false;
}

QList<QmakePriFile *> QmakePriFile::subPriFilesExact() const
{
    return Utils::filtered(m_children, &QmakePriFile::includedInExactParse);
}

QmakeProFile *QmakePriFile::proFile() const
{
    return m_qmakeProFile;
}

bool QmakePriFile::includedInExactParse() const
{
    return m_includedInExactParse;
}

void QmakePriFile::setIncludedInExactParse(bool b)
{
    m_includedInExactParse = b;
}

bool QmakePriFile::canAddSubProject(const FilePath &proFilePath) const
{
    return proFilePath.suffix() == "pro" || proFilePath.suffix() == "pri";
}

static FilePath simplifyProFilePath(const FilePath &proFilePath)
{
    // if proFilePath is like: _path_/projectName/projectName.pro
    // we simplify it to: _path_/projectName
    QFileInfo fi = proFilePath.toFileInfo(); // FIXME
    const QString parentPath = fi.absolutePath();
    QFileInfo parentFi(parentPath);
    if (parentFi.fileName() == fi.completeBaseName())
        return FilePath::fromString(parentPath);
    return proFilePath;
}

bool QmakePriFile::addSubProject(const FilePath &proFile)
{
    FilePaths uniqueProFilePaths;
    if (!m_recursiveEnumerateFiles.contains(proFile))
        uniqueProFilePaths.append(simplifyProFilePath(proFile));

    FilePaths failedFiles;
    changeFiles(Utils::Constants::PROFILE_MIMETYPE, uniqueProFilePaths, &failedFiles, AddToProFile);

    return failedFiles.isEmpty();
}

bool QmakePriFile::removeSubProjects(const FilePath &proFilePath)
{
    FilePaths failedOriginalFiles;
    changeFiles(Utils::Constants::PROFILE_MIMETYPE, {proFilePath}, &failedOriginalFiles, RemoveFromProFile);

    FilePaths simplifiedProFiles = Utils::transform(failedOriginalFiles, &simplifyProFilePath);

    FilePaths failedSimplifiedFiles;
    changeFiles(Utils::Constants::PROFILE_MIMETYPE, simplifiedProFiles, &failedSimplifiedFiles, RemoveFromProFile);

    return failedSimplifiedFiles.isEmpty();
}

bool QmakePriFile::addFiles(const FilePaths &filePaths, FilePaths *notAdded)
{
    // If a file is already referenced in the .pro file then we don't add them.
    // That ignores scopes and which variable was used to reference the file
    // So it's obviously a bit limited, but in those cases you need to edit the
    // project files manually anyway.

    using TypeFileMap = QMap<QString, FilePaths>;
    // Split into lists by file type and bulk-add them.
    TypeFileMap typeFileMap;
    for (const FilePath &file : filePaths) {
        const MimeType mt = Utils::mimeTypeForFile(file);
        typeFileMap[mt.name()] << file;
    }

    FilePaths failedFiles;
    for (auto it = typeFileMap.constBegin(); it != typeFileMap.constEnd(); ++it) {
        const FilePaths &typeFiles = *it;
        FilePaths qrcFiles; // the list of qrc files referenced from ui files
        if (it.key() == QLatin1String(Utils::Constants::RESOURCE_MIMETYPE)) {
            for (const FilePath &formFile : typeFiles) {
                const FilePaths resourceFiles = formResources(formFile);
                for (const FilePath &resourceFile : resourceFiles)
                    if (!qrcFiles.contains(resourceFile))
                        qrcFiles.append(resourceFile);
            }
        }

        FilePaths uniqueQrcFiles;
        for (const FilePath &file : std::as_const(qrcFiles)) {
            if (!m_recursiveEnumerateFiles.contains(file))
                uniqueQrcFiles.append(file);
        }

        FilePaths uniqueFilePaths;
        for (const FilePath &file : typeFiles) {
            if (!m_recursiveEnumerateFiles.contains(file))
                uniqueFilePaths.append(file);
        }
        FilePath::sort(uniqueFilePaths);

        changeFiles(it.key(), uniqueFilePaths, &failedFiles, AddToProFile);
        if (notAdded)
            *notAdded += failedFiles;
        changeFiles(QLatin1String(Utils::Constants::RESOURCE_MIMETYPE), uniqueQrcFiles, &failedFiles, AddToProFile);
        if (notAdded)
            *notAdded += failedFiles;
    }
    return failedFiles.isEmpty();
}

bool QmakePriFile::removeFiles(const FilePaths &filePaths, FilePaths *notRemoved)
{
    // Split into lists by file type and bulk-add them.
    QMap<QString, FilePaths> typeFileMap;
    for (const FilePath &file : filePaths) {
        const MimeType mt = Utils::mimeTypeForFile(file);
        typeFileMap[mt.name()] << file;
    }
    FilePaths failedFiles;
    for (auto it = typeFileMap.cbegin(); it != typeFileMap.cend(); ++it) {
        changeFiles(it.key(), *it, &failedFiles, RemoveFromProFile);
        if (notRemoved)
            *notRemoved = failedFiles;
    }
    return failedFiles.isEmpty();
}

bool QmakePriFile::deleteFiles(const FilePaths &filePaths)
{
    removeFiles(filePaths);
    return true;
}

bool QmakePriFile::canRenameFile(const FilePath &oldFilePath, const FilePath &newFilePath)
{
    if (newFilePath.isEmpty())
        return false;

    bool changeProFileOptional = deploysFolder(oldFilePath.absolutePath());
    if (changeProFileOptional)
        return true;

    return renameFile(oldFilePath, newFilePath, Change::TestOnly);
}

bool QmakePriFile::renameFile(const FilePath &oldFilePath, const FilePath &newFilePath)
{
    if (newFilePath.isEmpty())
        return false;

    bool changeProFileOptional = deploysFolder(oldFilePath.absolutePath());
    if (renameFile(oldFilePath, newFilePath, Change::Save))
        return true;
    return changeProFileOptional;
}

bool QmakePriFile::addDependencies(const QStringList &dependencies)
{
    if (dependencies.isEmpty())
        return true;
    if (!prepareForChange())
        return false;

    QStringList qtDependencies = filtered(dependencies, [](const QString &dep) {
        return dep.length() > 3 && dep.startsWith("Qt.");
    });
    qtDependencies = transform(qtDependencies, [](const QString &dep) {
        return dep.mid(3).toLower();
    });
    if (qtDependencies.isEmpty())
        return true;
    qtDependencies.removeOne("core");

    const QPair<ProFile *, QStringList> pair = readProFile();
    ProFile * const includeFile = pair.first;
    if (!includeFile)
        return false;
    QStringList lines = pair.second;

    const QString indent = continuationIndent();
    const ProWriter::PutFlags appendFlags(ProWriter::AppendValues | ProWriter::AppendOperator);
    if (!proFile()->variableValue(Variable::Config).contains("qt")) {
        if (lines.removeAll("CONFIG -= qt") == 0) {
            ProWriter::putVarValues(includeFile, &lines, {"qt"}, "CONFIG", appendFlags,
                                    QString(), indent);
        }
    }

    const QStringList currentQtDependencies = proFile()->variableValue(Variable::Qt);
    qtDependencies = filtered(qtDependencies, [currentQtDependencies](const QString &dep) {
        return !currentQtDependencies.contains(dep);
    });
    if (!qtDependencies.isEmpty()) {
        ProWriter::putVarValues(includeFile, &lines, qtDependencies,  "QT", appendFlags,
                                QString(), indent);
    }

    save(lines);
    includeFile->deref();
    return true;
}

bool QmakePriFile::saveModifiedEditors()
{
    Core::IDocument *document = Core::DocumentModel::documentForFilePath(filePath());
    if (!document || !document->isModified())
        return true;

    if (!Core::DocumentManager::saveDocument(document))
        return false;

    // force instant reload of ourselves
    QtSupport::ProFileCacheManager::instance()->discardFile(
        deviceRoot(), filePath().path(), m_buildSystem->qmakeVfs());

    m_buildSystem->notifyChanged(filePath());
    return true;
}

FilePaths QmakePriFile::formResources(const FilePath &formFile) const
{
    QStringList resourceFiles;
    QFile file(formFile.toUrlishString());
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QXmlStreamReader reader(&file);

    QFileInfo fi(formFile.toUrlishString());
    QDir formDir = fi.absoluteDir();
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            if (reader.name() == QLatin1String("iconset")) {
                const QXmlStreamAttributes attributes = reader.attributes();
                if (attributes.hasAttribute(QLatin1String("resource")))
                    resourceFiles.append(QDir::cleanPath(formDir.absoluteFilePath(
                                  attributes.value(QLatin1String("resource")).toString())));
            } else if (reader.name() == QLatin1String("include")) {
                const QXmlStreamAttributes attributes = reader.attributes();
                if (attributes.hasAttribute(QLatin1String("location")))
                    resourceFiles.append(QDir::cleanPath(formDir.absoluteFilePath(
                                  attributes.value(QLatin1String("location")).toString())));

            }
        }
    }

    if (reader.hasError())
        qWarning() << "Could not read form file:" << formFile;

    return FilePaths::fromStrings(resourceFiles);
}

bool QmakePriFile::ensureWriteableProFile(const FilePath &file)
{
    // Ensure that the file is not read only
    if (!file.isWritableFile()) {
        // Try via vcs manager
        Core::IVersionControl *versionControl =
            Core::VcsManager::findVersionControlForDirectory(file.parentDir());
        if (!versionControl || !versionControl->vcsOpen(file)) {
            bool makeWritable = file.setPermissions(file.permissions() | QFile::WriteUser);
            if (!makeWritable) {
                QMessageBox::warning(Core::ICore::dialogParent(),
                                     Tr::tr("Failed"),
                                     Tr::tr("Could not write project file %1.").arg(file.toUserOutput()));
                return false;
            }
        }
    }
    return true;
}

QPair<ProFile *, QStringList> QmakePriFile::readProFile()
{
    TextFileFormat::ReadResult result =
            m_textFormat.readFile(filePath(), EditorManager::defaultTextEncoding());
    if (result.code != TextFileFormat::ReadSuccess) {
        QmakeBuildSystem::proFileParseError(result.error, filePath());
        return {nullptr, {}};
    }

    QMakeVfs vfs;
    QtSupport::ProMessageHandler handler;
    QMakeParser parser(nullptr, &vfs, &handler);
    ProFile *includeFile = parser.parsedProBlock(deviceRoot(),
                                                 QStringView(result.content),
                                                 0,
                                                 filePath().toUrlishString(),
                                                 1);

    return {includeFile, result.content.split('\n')};
}

bool QmakePriFile::prepareForChange()
{
    return saveModifiedEditors() && ensureWriteableProFile(filePath());
}

bool QmakePriFile::renameFile(const FilePath &oldFilePath, const FilePath &newFilePath, Change mode)
{
    if (!prepareForChange())
        return false;

    QPair<ProFile *, QStringList> pair = readProFile();
    ProFile *includeFile = pair.first;
    QStringList lines = pair.second;

    if (!includeFile)
        return false;

    ProWriter::VarLocations removedLocations;
    const QStringList notChanged = ProWriter::removeFiles(includeFile,
                                                          &lines,
                                                          m_qmakeProFile->directoryPath(),
                                                          {oldFilePath},
                                                          varNamesForRemoving(),
                                                          &removedLocations);

    includeFile->deref();
    if (!notChanged.isEmpty())
        return false;
    QTC_ASSERT(!removedLocations.isEmpty(), return false);

    int endLine = lines.count();
    reverseForeach(removedLocations,
                   [this, &newFilePath, &lines, &endLine](const ProWriter::VarLocation &loc) {
        QStringList currentLines = lines.mid(loc.second, endLine - loc.second);
        const QString currentContents = currentLines.join('\n');

        // Reparse necessary due to changed contents.
        QMakeParser parser(nullptr, nullptr, nullptr);
        ProFile *const proFile = parser.parsedProBlock(deviceRoot(),
                                                       QStringView(currentContents),
                                                       0,
                                                       filePath().path(),
                                                       1,
                                                       QMakeParser::FullGrammar);
        QTC_ASSERT(proFile, return); // The file should still be valid after what we did.

        ProWriter::addFiles(proFile,
                            &currentLines,
                            {newFilePath.toUrlishString()},
                            loc.first,
                            continuationIndent());
        lines = lines.mid(0, loc.second) + currentLines + lines.mid(endLine);
        endLine = loc.second;
        proFile->deref();
    });

    if (mode == Change::Save)
        save(lines);
    return true;
}

void QmakePriFile::changeFiles(const QString &mimeType,
                               const FilePaths &filePaths,
                               FilePaths *notChanged,
                               ChangeType change, Change mode)
{
    if (filePaths.isEmpty())
        return;

    *notChanged = filePaths;

    // Check for modified editors
    if (!prepareForChange())
        return;

    QPair<ProFile *, QStringList> pair = readProFile();
    ProFile *includeFile = pair.first;
    QStringList lines = pair.second;

    if (!includeFile)
        return;

    qCDebug(qmakeNodesLog) << Q_FUNC_INFO << "mime type:" << mimeType << "file paths:"
                           << filePaths << "change type:" << int(change) << "mode:" << int(mode);
    if (change == AddToProFile) {
        // Use the first variable for adding.
        ProWriter::addFiles(includeFile, &lines,
                            Utils::transform(filePaths, &FilePath::toUrlishString),
                            varNameForAdding(mimeType),
                            continuationIndent());
        notChanged->clear();
    } else { // RemoveFromProFile
        *notChanged = FilePaths::fromStrings(
            ProWriter::removeFiles(includeFile,
                                   &lines,
                                   m_qmakeProFile->directoryPath(),
                                   filePaths,
                                   varNamesForRemoving()));
    }

    // save file
    if (mode == Change::Save)
        save(lines);
    includeFile->deref();
}

void QmakePriFile::addChild(QmakePriFile *pf)
{
    QTC_ASSERT(!m_children.contains(pf), return);
    QTC_ASSERT(!pf->parent(), return);
    m_children.append(pf);
    pf->setParent(this);
}

void QmakePriFile::setParent(QmakePriFile *p)
{
    QTC_ASSERT(!m_parent, return);
    m_parent = p;
}

bool QmakePriFile::setProVariable(const QString &var, const QStringList &values, const QString &scope, int flags)
{
    if (!prepareForChange())
        return false;

    QPair<ProFile *, QStringList> pair = readProFile();
    ProFile *includeFile = pair.first;
    QStringList lines = pair.second;

    if (!includeFile)
        return false;

    ProWriter::putVarValues(includeFile, &lines, values, var,
                            ProWriter::PutFlags(flags),
                            scope, continuationIndent());

    save(lines);
    includeFile->deref();
    return true;
}

void QmakePriFile::save(const QStringList &lines)
{
    {
        QTC_ASSERT(m_textFormat.encoding().isValid(), return);
        FileChangeBlocker changeGuard(filePath());
        if (const Result<> res = m_textFormat.writeFile(filePath(), lines.join('\n')); !res)
            QMessageBox::critical(Core::ICore::dialogParent(), Tr::tr("File Error"), res.error());
    }

    // This is a hack.
    // We are saving twice in a very short timeframe, once the editor and once the ProFile.
    // So the modification time might not change between those two saves.
    // We manually tell each editor to reload it's file.
    // (The .pro files are notified by the file system watcher.)
    QStringList errorStrings;
    Core::IDocument *document = Core::DocumentModel::documentForFilePath(filePath());
    if (document) {
        Result<> res = document->reload(Core::IDocument::FlagReload, Core::IDocument::TypeContents);
        if (!res)
            errorStrings << res.error();
    }
    if (!errorStrings.isEmpty())
        QMessageBox::warning(Core::ICore::dialogParent(), Tr::tr("File Error"),
                             errorStrings.join(QLatin1Char('\n')));
}

QStringList QmakePriFile::varNames(FileType type, QtSupport::ProFileReader *readerExact)
{
    QStringList vars;
    switch (type) {
    case FileType::Header:
        vars << "HEADERS" << "OBJECTIVE_HEADERS" << "PRECOMPILED_HEADER";
        break;
    case FileType::Source: {
        vars << QLatin1String("SOURCES");
        const QStringList listOfExtraCompilers = readerExact->values("QMAKE_EXTRA_COMPILERS");
        for (const QString &var : listOfExtraCompilers) {
            const QStringList inputs = readerExact->values(var + QLatin1String(".input"));
            for (const QString &input : inputs)
                // FORMS, RESOURCES, and STATECHARTS are handled below, HEADERS and SOURCES above
                if (input != "FORMS"
                        && input != "STATECHARTS"
                        && input != "RESOURCES"
                        && input != "SOURCES"
                        && input != "HEADERS"
                        && input != "OBJECTIVE_HEADERS"
                        && input != "PRECOMPILED_HEADER") {
                    vars << input;
                }
        }
        break;
    }
    case FileType::Resource:
        vars << QLatin1String("RESOURCES");
        break;
    case FileType::Form:
        vars << QLatin1String("FORMS");
        break;
    case FileType::StateChart:
        vars << QLatin1String("STATECHARTS");
        break;
    case FileType::Project:
        vars << QLatin1String("SUBDIRS");
        break;
    case FileType::QML:
        vars << QLatin1String("OTHER_FILES");
        vars << QLatin1String("DISTFILES");
        break;
    default:
        vars << "DISTFILES" << "ICON" << "OTHER_FILES" << "QMAKE_INFO_PLIST" << "TRANSLATIONS";
        break;
    }
    return vars;
}

//!
//! \brief QmakePriFile::varNames
//! \param mimeType
//! \return the qmake variable name for the mime type
//! Note: Only used for adding.
//!
QString QmakePriFile::varNameForAdding(const QString &mimeType)
{
    using namespace Utils::Constants;

    if (mimeType == QLatin1String(CPP_HEADER_MIMETYPE)
            || mimeType == QLatin1String(C_HEADER_MIMETYPE)) {
        return QLatin1String("HEADERS");
    }

    if (mimeType == QLatin1String(CPP_SOURCE_MIMETYPE)
               || mimeType == QLatin1String(OBJECTIVE_CPP_SOURCE_MIMETYPE)
               || mimeType == QLatin1String(C_SOURCE_MIMETYPE)) {
        return QLatin1String("SOURCES");
    }

    if (mimeType == QLatin1String(RESOURCE_MIMETYPE))
        return QLatin1String("RESOURCES");

    if (mimeType == QLatin1String(FORM_MIMETYPE))
        return QLatin1String("FORMS");

    if (mimeType == QLatin1String(QML_MIMETYPE)
            || mimeType == QLatin1String(QMLUI_MIMETYPE)) {
        return QLatin1String("DISTFILES");
    }

    if (mimeType == QLatin1String(SCXML_MIMETYPE))
        return QLatin1String("STATECHARTS");

    if (mimeType == QLatin1String(PROFILE_MIMETYPE))
        return QLatin1String("SUBDIRS");

    if (mimeType == QLatin1String(Utils::Constants::LINGUIST_MIMETYPE))
        return QLatin1String("TRANSLATIONS");

    return QLatin1String("DISTFILES");
}

//!
//! \brief QmakePriFile::varNamesForRemoving
//! \return all qmake variables which are displayed in the project tree
//! Note: Only used for removing.
//!
QStringList QmakePriFile::varNamesForRemoving()
{
    QStringList vars;
    vars << QLatin1String("HEADERS");
    vars << QLatin1String("OBJECTIVE_HEADERS");
    vars << QLatin1String("PRECOMPILED_HEADER");
    vars << QLatin1String("SOURCES");
    vars << QLatin1String("OBJECTIVE_SOURCES");
    vars << QLatin1String("RESOURCES");
    vars << QLatin1String("FORMS");
    vars << QLatin1String("OTHER_FILES");
    vars << QLatin1String("SUBDIRS");
    vars << QLatin1String("DISTFILES");
    vars << QLatin1String("ICON");
    vars << QLatin1String("QMAKE_INFO_PLIST");
    vars << QLatin1String("STATECHARTS");
    vars << QLatin1String("TRANSLATIONS");
    return vars;
}

QSet<FilePath> QmakePriFile::filterFilesProVariables(FileType fileType, const QSet<FilePath> &files)
{
    if (fileType != FileType::QML && fileType != FileType::Unknown)
        return files;
    QSet<FilePath> result;
    if (fileType == FileType::QML) {
        for (const FilePath &file : files)
            if (file.endsWith(QLatin1String(".qml")))
                result << file;
    } else {
        for (const FilePath &file : files)
            if (!file.endsWith(QLatin1String(".qml")))
                result << file;
    }
    return result;
}

QSet<FilePath> QmakePriFile::filterFilesRecursiveEnumerata(FileType fileType, const QSet<FilePath> &files)
{
    QSet<FilePath> result;
    if (fileType != FileType::QML && fileType != FileType::Unknown)
        return result;
    if (fileType == FileType::QML) {
        for (const FilePath &file : files)
            if (file.endsWith(QLatin1String(".qml")))
                result << file;
    } else {
        for (const FilePath &file : files)
            if (!file.endsWith(QLatin1String(".qml")))
                result << file;
    }
    return result;
}

} // namespace QmakeProjectManager

static ProjectType proFileTemplateTypeToProjectType(ProFileEvaluator::TemplateType type)
{
    switch (type) {
    case ProFileEvaluator::TT_Unknown:
    case ProFileEvaluator::TT_Application:
        return ProjectType::ApplicationTemplate;
    case ProFileEvaluator::TT_StaticLibrary:
        return ProjectType::StaticLibraryTemplate;
    case ProFileEvaluator::TT_SharedLibrary:
        return ProjectType::SharedLibraryTemplate;
    case ProFileEvaluator::TT_Script:
        return ProjectType::ScriptTemplate;
    case ProFileEvaluator::TT_Aux:
        return ProjectType::AuxTemplate;
    case ProFileEvaluator::TT_Subdirs:
        return ProjectType::SubDirsTemplate;
    default:
        return ProjectType::Invalid;
    }
}

QmakeProFile *QmakeProFile::findProFile(const FilePath &fileName)
{
    return static_cast<QmakeProFile *>(findPriFile(fileName));
}

const QmakeProFile *QmakeProFile::findProFile(const FilePath &fileName) const
{
    return static_cast<const QmakeProFile *>(findPriFile(fileName));
}

QByteArray QmakeProFile::cxxDefines() const
{
    QByteArray result;
    const QStringList defs = variableValue(Variable::Defines);
    for (const QString &def : defs) {
        // 'def' is shell input, so interpret it.
        ProcessArgs::SplitError error = ProcessArgs::SplitOk;
        const QStringList args = ProcessArgs::splitArgs(def, HostOsInfo::hostOs(), false, &error);
        if (error != ProcessArgs::SplitOk || args.size() == 0)
            continue;

        result += "#define ";
        const QString defInterpreted = args.first();
        const int index = defInterpreted.indexOf(QLatin1Char('='));
        if (index == -1) {
            result += defInterpreted.toLatin1();
            result += " 1\n";
        } else {
            const QString name = defInterpreted.left(index);
            const QString value = defInterpreted.mid(index + 1);
            result += name.toLatin1();
            result += ' ';
            result += value.toLocal8Bit();
            result += '\n';
        }
    }
    return result;
}

/*!
  \class QmakeProFile
  Implements abstract ProjectNode class
  */
QmakeProFile::QmakeProFile(QmakeBuildSystem *buildSystem, const FilePath &filePath) :
    QmakePriFile(buildSystem, this, filePath)
{
}

QmakeProFile::QmakeProFile(const FilePath &filePath) : QmakePriFile(filePath) { }

QmakeProFile::~QmakeProFile()
{
    qDeleteAll(m_extraCompilers);
    cleanupFutureWatcher();
    cleanupProFileReaders();
}

void QmakeProFile::cleanupFutureWatcher()
{
    if (!m_parseFutureWatcher)
        return;

    m_parseFutureWatcher->disconnect();
    m_parseFutureWatcher->cancel();
    m_parseFutureWatcher->waitForFinished();
    m_parseFutureWatcher->deleteLater();
    m_parseFutureWatcher = nullptr;
    m_buildSystem->decrementPendingEvaluateFutures();
}

void QmakeProFile::setupFutureWatcher()
{
    QTC_ASSERT(!m_parseFutureWatcher, return);

    m_parseFutureWatcher = new QFutureWatcher<Internal::QmakeEvalResultPtr>;
    QObject::connect(m_parseFutureWatcher, &QFutureWatcherBase::finished, [this] {
        applyEvaluate(m_parseFutureWatcher->result());
        cleanupFutureWatcher();
    });
    m_buildSystem->incrementPendingEvaluateFutures();
}

bool QmakeProFile::isParent(QmakeProFile *node)
{
    while ((node = dynamic_cast<QmakeProFile *>(node->parent()))) {
        if (node == this)
            return true;
    }
    return false;
}

QString QmakeProFile::displayName() const
{
    if (!m_displayName.isEmpty())
        return m_displayName;
    return QmakePriFile::displayName();
}

QList<QmakeProFile *> QmakeProFile::allProFiles()
{
    QList<QmakeProFile *> result = { this };
    for (QmakePriFile *c : std::as_const(m_children)) {
        auto proC = dynamic_cast<QmakeProFile *>(c);
        if (proC)
            result.append(proC->allProFiles());
    }
    return result;
}

ProjectType QmakeProFile::projectType() const
{
    return m_projectType;
}

QStringList QmakeProFile::variableValue(const Variable var) const
{
    return m_varValues.value(var);
}

QString QmakeProFile::singleVariableValue(const Variable var) const
{
    const QStringList &values = variableValue(var);
    return values.isEmpty() ? QString() : values.first();
}

FilePaths QmakeProFile::filePathsValue(const Variable var) const
{
    return transform(variableValue(var), [this](const QString &fileName) {
        return m_filePath.withNewPath(fileName);
    });
}

void QmakeProFile::setParseInProgressRecursive(bool b)
{
    setParseInProgress(b);
    for (QmakePriFile *c : children()) {
        if (auto node = dynamic_cast<QmakeProFile *>(c))
            node->setParseInProgressRecursive(b);
    }
}

void QmakeProFile::setParseInProgress(bool b)
{
    m_parseInProgress = b;
}

// Do note the absence of signal emission, always set validParse
// before setParseInProgress, as that will emit the signals
void QmakeProFile::setValidParseRecursive(bool b)
{
    m_validParse = b;
    for (QmakePriFile *c : children()) {
        if (auto *node = dynamic_cast<QmakeProFile *>(c))
            node->setValidParseRecursive(b);
    }
}

bool QmakeProFile::validParse() const
{
    return m_validParse;
}

bool QmakeProFile::parseInProgress() const
{
    return m_parseInProgress;
}

void QmakeProFile::scheduleUpdate(QmakeProFile::AsyncUpdateDelay delay)
{
    setParseInProgressRecursive(true);
    m_buildSystem->scheduleAsyncUpdateFile(this, delay);
}

void QmakeProFile::asyncUpdate()
{
    cleanupFutureWatcher();
    setupFutureWatcher();
    setupReader();
    if (!includedInExactParse())
        m_readerExact->setExact(false);
    QmakeEvalInput input = evalInput();
    QFuture<QmakeEvalResultPtr> future = Utils::asyncRun(ProjectExplorerPlugin::sharedThreadPool(),
                                                         QThread::LowestPriority,
                                                         &QmakeProFile::asyncEvaluate, this, input);
    m_parseFutureWatcher->setFuture(future);
}

bool QmakeProFile::isFileFromWildcard(const QString &filePath) const
{
    const QFileInfo fileInfo(filePath);
    const auto directoryIterator = m_wildcardDirectoryContents.constFind(fileInfo.path());
    return (directoryIterator != m_wildcardDirectoryContents.end()
            && directoryIterator.value().contains(fileInfo.fileName()));
}

QmakeEvalInput QmakeProFile::evalInput() const
{
    QmakeEvalInput input;
    input.projectDir = directoryPath();
    input.projectFilePath = filePath();
    input.buildDirectory = m_buildSystem->buildDir(m_filePath);
    input.sysroot = m_buildSystem->qmakeSysroot();
    input.readerExact = m_readerExact;
    input.readerCumulative = m_readerCumulative;
    input.qmakeGlobals = m_buildSystem->qmakeGlobals();
    input.qmakeVfs = m_buildSystem->qmakeVfs();
    input.includedInExcactParse = includedInExactParse();
    for (const QmakePriFile *pri = this; pri; pri = pri->parent())
        input.parentFilePaths.insert(pri->filePath());
    return input;
}

void QmakeProFile::setupReader()
{
    Q_ASSERT(!m_readerExact);
    Q_ASSERT(!m_readerCumulative);

    m_readerExact = m_buildSystem->createProFileReader(this);

    m_readerCumulative = m_buildSystem->createProFileReader(this);
    m_readerCumulative->setCumulative(true);
}

static bool evaluateOne(const QmakeEvalInput &input, ProFile *pro,
                        QtSupport::ProFileReader *reader, bool cumulative,
                        QtSupport::ProFileReader **buildPassReader)
{
    if (!reader->accept(pro, QMakeEvaluator::LoadAll))
        return false;

    QStringList builds = reader->values(QLatin1String("BUILDS"));
    if (builds.isEmpty()) {
        *buildPassReader = reader;
    } else {
        QString build = builds.first();
        QHash<QString, QStringList> basevars;
        QStringList basecfgs = reader->values(build + QLatin1String(".CONFIG"));
        basecfgs += build;
        basecfgs += QLatin1String("build_pass");
        basecfgs += "qtc_run";
        basevars[QLatin1String("BUILD_PASS")] = QStringList(build);
        QStringList buildname = reader->values(build + QLatin1String(".name"));
        basevars[QLatin1String("BUILD_NAME")] = (buildname.isEmpty() ? QStringList(build) : buildname);

        // We don't increase/decrease m_qmakeGlobalsRefCnt here, because the outer profilereaders keep m_qmakeGlobals alive anyway
        auto bpReader = new QtSupport::ProFileReader(input.qmakeGlobals, input.qmakeVfs); // needs to access m_qmakeGlobals, m_qmakeVfs

        // Core parts of the ProParser hard-assert on non-local items.
        bpReader->setOutputDir(input.buildDirectory.toFSPathString());
        bpReader->setCumulative(cumulative);
        bpReader->setExtraVars(basevars);
        bpReader->setExtraConfigs(basecfgs);

        if (bpReader->accept(pro, QMakeEvaluator::LoadAll))
            *buildPassReader = bpReader;
        else
            delete bpReader;
    }

    return true;
}

QmakeEvalResultPtr QmakeProFile::evaluate(const QmakeEvalInput &input)
{
    QmakeEvalResultPtr result(new QmakeEvalResult);
    QtSupport::ProFileReader *exactBuildPassReader = nullptr;
    QtSupport::ProFileReader *cumulativeBuildPassReader = nullptr;
    ProFile *pro = input.readerExact->parsedProFile(input.qmakeGlobals->device_root, input.projectFilePath.path());
    if (pro) {
        bool exactOk = evaluateOne(input, pro, input.readerExact, false, &exactBuildPassReader);
        bool cumulOk = evaluateOne(input, pro, input.readerCumulative, true, &cumulativeBuildPassReader);
        pro->deref();
        result->state = exactOk ? QmakeEvalResult::EvalOk
                                : cumulOk ? QmakeEvalResult::EvalPartial : QmakeEvalResult::EvalFail;
    } else {
        result->state = QmakeEvalResult::EvalFail;
    }

    if (result->state == QmakeEvalResult::EvalFail)
        return result;

    result->includedFiles.proFile = pro;
    result->includedFiles.name = input.projectFilePath;

    QHash<int, QmakePriFileEvalResult *> proToResult;

    result->projectType
            = proFileTemplateTypeToProjectType(
                (result->state == QmakeEvalResult::EvalOk ? input.readerExact
                                                          : input.readerCumulative)->templateType());
    if (result->state == QmakeEvalResult::EvalOk) {
        if (result->projectType == ProjectType::SubDirsTemplate) {
            QStringList errors;
            const FilePaths subDirs = subDirsPaths(input.readerExact, input.projectDir,
                                                   &result->subProjectsNotToDeploy, &errors);
            result->errors.append(errors);

            for (const FilePath &subDirName : subDirs) {
                auto subDir = new QmakeIncludedPriFile;
                subDir->proFile = nullptr;
                subDir->name = subDirName;
                result->includedFiles.children.insert(subDirName, subDir);
            }

            result->exactSubdirs = Utils::toSet(subDirs);
        }

        // Convert ProFileReader::includeFiles to IncludedPriFile structure
        QHash<ProFile *, QList<ProFile *>> includeFiles = input.readerExact->includeFiles();
        QList<QmakeIncludedPriFile *> toBuild = {&result->includedFiles};
        while (!toBuild.isEmpty()) {
            QmakeIncludedPriFile *current = toBuild.takeFirst();
            if (!current->proFile)
                continue;  // Don't attempt to map subdirs here
            const QList<ProFile *> children = includeFiles.value(current->proFile);
            for (ProFile *child : children) {
                const FilePath childName = child->fullName();
                auto it = current->children.find(childName);
                if (it == current->children.end()) {
                    auto childTree = new QmakeIncludedPriFile;
                    childTree->proFile = child;
                    childTree->name = childName;
                    current->children.insert(childName, childTree);
                    proToResult[child->id()] = &childTree->result;
                }
            }
            toBuild.append(current->children.values());
        }
    }

    if (result->projectType == ProjectType::SubDirsTemplate) {
        const FilePaths subDirs = subDirsPaths(input.readerCumulative, input.projectDir, nullptr, nullptr);
        for (const FilePath &subDirName : subDirs) {
            auto it = result->includedFiles.children.find(subDirName);
            if (it == result->includedFiles.children.end()) {
                auto subDir = new QmakeIncludedPriFile;
                subDir->proFile = nullptr;
                subDir->name = subDirName;
                result->includedFiles.children.insert(subDirName, subDir);
            }
        }
    }

    // Add ProFileReader::includeFiles information from cumulative parse to IncludedPriFile structure
    QHash<ProFile *, QList<ProFile *>> includeFiles = input.readerCumulative->includeFiles();
    QList<QmakeIncludedPriFile *> toBuild = {&result->includedFiles};
    while (!toBuild.isEmpty()) {
        QmakeIncludedPriFile *current = toBuild.takeFirst();
        if (!current->proFile)
            continue;  // Don't attempt to map subdirs here
        const QList<ProFile *> children = includeFiles.value(current->proFile);
        for (ProFile *child : children) {
            const FilePath childName = child->fullName();
            auto it = current->children.find(childName);
            if (it == current->children.end()) {
                auto childTree = new QmakeIncludedPriFile;
                childTree->proFile = child;
                childTree->name = childName;
                childTree->fromExactParse = false;
                current->children.insert(childName, childTree);
                proToResult[child->id()] = &childTree->result;
            }
        }
        toBuild.append(current->children.values());
    }

    auto exactReader = exactBuildPassReader ? exactBuildPassReader : input.readerExact;
    auto cumulativeReader = cumulativeBuildPassReader ? cumulativeBuildPassReader : input.readerCumulative;

    QHash<QString, QList<ProFileEvaluator::SourceFile>> exactSourceFiles;
    QHash<QString, QList<ProFileEvaluator::SourceFile>> cumulativeSourceFiles;

    const QString &device = input.qmakeGlobals->device_root;
    const QStringList baseVPathsExact
            = baseVPaths(exactReader, input.projectDir.path(), input.buildDirectory.path());
    const QStringList baseVPathsCumulative
            = baseVPaths(cumulativeReader, input.projectDir.path(), input.buildDirectory.path());

    for (int i = 0; i < static_cast<int>(FileType::FileTypeSize); ++i) {
        const auto type = static_cast<FileType>(i);
        const QStringList qmakeVariables = varNames(type, exactReader);
        for (const QString &qmakeVariable : qmakeVariables) {
            QHash<ProString, bool> handled;
            if (result->state == QmakeEvalResult::EvalOk) {
                const QStringList vPathsExact = fullVPaths(
                            baseVPathsExact, exactReader, qmakeVariable, input.projectDir.path());
                auto sourceFiles = exactReader->absoluteFileValues(
                            qmakeVariable, input.projectDir.path(), vPathsExact, &handled, result->directoriesWithWildcards);
                exactSourceFiles[qmakeVariable] = sourceFiles;
                extractSources(device, proToResult, &result->includedFiles.result, sourceFiles, type, false);
            }
            const QStringList vPathsCumulative = fullVPaths(
                        baseVPathsCumulative, cumulativeReader, qmakeVariable, input.projectDir.path());
            auto sourceFiles = cumulativeReader->absoluteFileValues(
                        qmakeVariable, input.projectDir.path(), vPathsCumulative, &handled, result->directoriesWithWildcards);
            cumulativeSourceFiles[qmakeVariable] = sourceFiles;
            extractSources(device, proToResult, &result->includedFiles.result, sourceFiles, type, true);
        }
    }

    // This is used for two things:
    // - Actual deployment, in which case we need exact values.
    // - The project tree, in which case we also want exact values to avoid recursively
    //   watching bogus paths. However, we accept the values even if the evaluation
    //   failed, to at least have a best-effort result.
    result->installsList = installsList(exactBuildPassReader,
                                        input.projectFilePath.path(),
                                        input.projectDir.path(),
                                        input.buildDirectory.path());
    extractInstalls(device, proToResult, &result->includedFiles.result, result->installsList);

    if (result->state == QmakeEvalResult::EvalOk) {
        result->targetInformation = targetInformation(input.readerExact, exactBuildPassReader,
                                                      input.buildDirectory, input.projectFilePath);

        // update other variables
        result->newVarValues[Variable::Defines] = exactReader->values(QLatin1String("DEFINES"));
        result->newVarValues[Variable::IncludePath] = includePaths(exactReader, input.sysroot,
                                                            input.buildDirectory, input.projectDir.path());
        result->newVarValues[Variable::CppFlags] = exactReader->values(QLatin1String("QMAKE_CXXFLAGS"));
        result->newVarValues[Variable::CFlags] = exactReader->values(QLatin1String("QMAKE_CFLAGS"));
        result->newVarValues[Variable::ExactSource] =
                fileListForVar(exactSourceFiles, QLatin1String("SOURCES")) +
                fileListForVar(exactSourceFiles, QLatin1String("HEADERS")) +
                fileListForVar(exactSourceFiles, QLatin1String("OBJECTIVE_HEADERS"));
        result->newVarValues[Variable::CumulativeSource] =
                fileListForVar(cumulativeSourceFiles, QLatin1String("SOURCES")) +
                fileListForVar(cumulativeSourceFiles, QLatin1String("HEADERS")) +
                fileListForVar(cumulativeSourceFiles, QLatin1String("OBJECTIVE_HEADERS"));
        result->newVarValues[Variable::UiDir] = QStringList() << uiDirPath(exactReader, input.buildDirectory);
        result->newVarValues[Variable::HeaderExtension] = QStringList() << exactReader->value(QLatin1String("QMAKE_EXT_H"));
        result->newVarValues[Variable::CppExtension] = QStringList() << exactReader->value(QLatin1String("QMAKE_EXT_CPP"));
        result->newVarValues[Variable::MocDir] = QStringList() << mocDirPath(exactReader, input.buildDirectory);
        result->newVarValues[Variable::ExactResource] = fileListForVar(exactSourceFiles, QLatin1String("RESOURCES"));
        result->newVarValues[Variable::CumulativeResource] = fileListForVar(cumulativeSourceFiles, QLatin1String("RESOURCES"));
        result->newVarValues[Variable::PkgConfig] = exactReader->values(QLatin1String("PKGCONFIG"));
        result->newVarValues[Variable::PrecompiledHeader] = ProFileEvaluator::sourcesToFiles(exactReader->fixifiedValues(
                    QLatin1String("PRECOMPILED_HEADER"), input.projectDir.path(), input.buildDirectory.path(), false));
        result->newVarValues[Variable::LibDirectories] = libDirectories(exactReader);
        result->newVarValues[Variable::Config] = exactReader->values(QLatin1String("CONFIG"));
        result->newVarValues[Variable::QmlImportPath] = exactReader->absolutePathValues(
                    QLatin1String("QML_IMPORT_PATH"), input.projectDir.path());
        result->newVarValues[Variable::QmlDesignerImportPath] = exactReader->absolutePathValues(
                    QLatin1String("QML_DESIGNER_IMPORT_PATH"), input.projectDir.path());
        result->newVarValues[Variable::Makefile] = exactReader->values(QLatin1String("MAKEFILE"));
        result->newVarValues[Variable::Qt] = exactReader->values(QLatin1String("QT"));
        result->newVarValues[Variable::ObjectExt] = exactReader->values(QLatin1String("QMAKE_EXT_OBJ"));
        result->newVarValues[Variable::ObjectsDir] = exactReader->values(QLatin1String("OBJECTS_DIR"));
        result->newVarValues[Variable::Version] = exactReader->values(QLatin1String("VERSION"));
        result->newVarValues[Variable::TargetExt] = exactReader->values(QLatin1String("TARGET_EXT"));
        result->newVarValues[Variable::TargetVersionExt]
                = exactReader->values(QLatin1String("TARGET_VERSION_EXT"));
        result->newVarValues[Variable::StaticLibExtension] = exactReader->values(QLatin1String("QMAKE_EXTENSION_STATICLIB"));
        result->newVarValues[Variable::ShLibExtension] = exactReader->values(QLatin1String("QMAKE_EXTENSION_SHLIB"));
        result->newVarValues[Variable::AndroidAbi] = exactReader->values(QLatin1String(Android::Constants::ANDROID_TARGET_ARCH));
        result->newVarValues[Variable::AndroidDeploySettingsFile] = exactReader->values(QLatin1String(Android::Constants::ANDROID_DEPLOYMENT_SETTINGS_FILE));
        result->newVarValues[Variable::AndroidPackageSourceDir] = exactReader->values(QLatin1String(Android::Constants::ANDROID_PACKAGE_SOURCE_DIR));
        result->newVarValues[Variable::AndroidAbis] = exactReader->values(QLatin1String(Android::Constants::ANDROID_ABIS));
        result->newVarValues[Variable::AndroidApplicationArgs] = exactReader->values(QLatin1String(Android::Constants::ANDROID_APPLICATION_ARGUMENTS));
        result->newVarValues[Variable::AndroidExtraLibs] = exactReader->values(QLatin1String(Android::Constants::ANDROID_EXTRA_LIBS));
        result->newVarValues[Variable::IosDeploymentTarget] = exactReader->values("QMAKE_IOS_DEPLOYMENT_TARGET");
        result->newVarValues[Variable::IsoIcons] = exactReader->values(QLatin1String("ISO_ICONS"));
        result->newVarValues[Variable::QmakeProjectName] = exactReader->values(QLatin1String("QMAKE_PROJECT_NAME"));
        result->newVarValues[Variable::QmakeCc] = exactReader->values("QMAKE_CC");
        result->newVarValues[Variable::QmakeCxx] = exactReader->values("QMAKE_CXX");
    }

    if (result->state == QmakeEvalResult::EvalOk || result->state == QmakeEvalResult::EvalPartial) {

        QList<QmakeIncludedPriFile *> toExtract = {&result->includedFiles};
        while (!toExtract.isEmpty()) {
            QmakeIncludedPriFile *current = toExtract.takeFirst();
            processValues(current->result);
            toExtract.append(current->children.values());
        }
    }

    if (exactBuildPassReader && exactBuildPassReader != input.readerExact)
        delete exactBuildPassReader;
    if (cumulativeBuildPassReader && cumulativeBuildPassReader != input.readerCumulative)
        delete cumulativeBuildPassReader;

    QList<QPair<QmakePriFile *, QmakeIncludedPriFile *>>
            toCompare{{nullptr, &result->includedFiles}};
    while (!toCompare.isEmpty()) {
        QmakePriFile *pn = toCompare.first().first;
        QmakeIncludedPriFile *tree = toCompare.first().second;
        toCompare.pop_front();

        // Loop prevention: Make sure that exact same node is not in our parent chain
        for (QmakeIncludedPriFile *priFile : std::as_const(tree->children)) {
            bool loop = input.parentFilePaths.contains(priFile->name);
            for (const QmakePriFile *n = pn; n && !loop; n = n->parent()) {
                if (n->filePath() == priFile->name)
                    loop = true;
            }
            if (loop)
                continue; // Do nothing

            if (priFile->proFile) {
                auto *qmakePriFileNode = new QmakePriFile(priFile->name);
                if (pn)
                    pn->addChild(qmakePriFileNode);
                else
                    result->directChildren << qmakePriFileNode;
                qmakePriFileNode->setIncludedInExactParse(input.includedInExcactParse
                        && priFile->fromExactParse && result->state == QmakeEvalResult::EvalOk);
                result->priFiles.push_back({qmakePriFileNode, priFile->result});
                toCompare.push_back({qmakePriFileNode, priFile});
            } else {
                auto *qmakeProFileNode = new QmakeProFile(priFile->name);
                if (pn)
                    pn->addChild(qmakeProFileNode);
                else
                    result->directChildren << qmakeProFileNode;
                qmakeProFileNode->setIncludedInExactParse(input.includedInExcactParse
                            && result->exactSubdirs.contains(qmakeProFileNode->filePath()));
                qmakeProFileNode->setParseInProgress(true);
                result->proFiles << qmakeProFileNode;
            }
        }
    }

    return result;
}

void QmakeProFile::asyncEvaluate(QPromise<QmakeEvalResultPtr> &promise, QmakeEvalInput input)
{
    promise.addResult(evaluate(input));
}

bool sortByParserNodes(Node *a, Node *b)
{
    return a->filePath() < b->filePath();
}

void QmakeProFile::applyEvaluate(const QmakeEvalResultPtr &result)
{
    if (!m_readerExact)
        return;

    if (m_buildSystem->asyncUpdateState() == QmakeBuildSystem::ShuttingDown) {
        cleanupProFileReaders();
        return;
    }

    for (const QString &error : std::as_const(result->errors))
        QmakeBuildSystem::proFileParseError(error, filePath());

    // we are changing what is executed in that case
    if (result->state == QmakeEvalResult::EvalFail || m_buildSystem->wasEvaluateCanceled()) {
        m_validParse = false;
        cleanupProFileReaders();
        setValidParseRecursive(false);
        setParseInProgressRecursive(false);

        if (result->state == QmakeEvalResult::EvalFail) {
            QmakeBuildSystem::proFileParseError(Tr::tr("Error while parsing file %1. Giving up.")
                                                .arg(filePath().toUserOutput()), filePath());
            if (m_projectType == ProjectType::Invalid)
                return;

            makeEmpty();

            m_projectType = ProjectType::Invalid;
        }
        return;
    }

    qCDebug(qmakeParse()) << "QmakeProFile - updating files for file " << filePath();

    if (result->projectType != m_projectType) {
        // probably all subfiles/projects have changed anyway
        // delete files && folders && projects
        for (QmakePriFile *c : children()) {
            if (auto qmakeProFile = dynamic_cast<QmakeProFile *>(c)) {
                qmakeProFile->setValidParseRecursive(false);
                qmakeProFile->setParseInProgressRecursive(false);
            }
        }

        makeEmpty();
        m_projectType = result->projectType;
    }

    //
    // Add/Remove pri files, sub projects
    //
    FilePath buildDirectory = m_buildSystem->buildDir(m_filePath);
    makeEmpty();
    for (QmakePriFile * const toAdd : std::as_const(result->directChildren))
        addChild(toAdd);
    result->directChildren.clear();

    for (const auto &priFiles : std::as_const(result->priFiles)) {
        priFiles.first->finishInitialization(m_buildSystem, this);
        priFiles.first->update(priFiles.second);
    }

    for (QmakeProFile * const proFile : std::as_const(result->proFiles)) {
        proFile->finishInitialization(m_buildSystem, proFile);
        proFile->asyncUpdate();
    }
    QmakePriFile::update(result->includedFiles.result);

    m_validParse = (result->state == QmakeEvalResult::EvalOk);
    if (m_validParse) {
        // update TargetInformation
        m_qmakeTargetInformation = result->targetInformation;

        m_subProjectsNotToDeploy = result->subProjectsNotToDeploy;
        m_installsList = result->installsList;

        if (m_varValues != result->newVarValues)
            m_varValues = result->newVarValues;

        m_displayName = singleVariableValue(Variable::QmakeProjectName);
        m_featureRoots = m_readerExact->featureRoots();
    } // result == EvalOk

    if (!result->directoriesWithWildcards.isEmpty()) {
        if (!m_wildcardWatcher) {
            m_wildcardWatcher = std::make_unique<FileSystemWatcher>();
            QObject::connect(
                m_wildcardWatcher.get(), &FileSystemWatcher::directoryChanged,
                [this](const FilePath &filePath) {
                    const QString path = filePath.toFSPathString();
                    QStringList directoryContents = QDir(path).entryList();
                    if (m_wildcardDirectoryContents.value(path) != directoryContents) {
                        m_wildcardDirectoryContents.insert(path, directoryContents);
                        scheduleUpdate();
                    }
                });
        }
        FilePaths directoriesToAdd;
        for (const QString &dir : std::as_const(result->directoriesWithWildcards)) {
            const FilePath directory = FilePath::fromString(dir);
            if (!m_wildcardWatcher->watchesDirectory(directory))
                directoriesToAdd.append(directory);
        }
        for (const FilePath &dir : directoriesToAdd) {
            const QString path = dir.toFSPathString();
            m_wildcardDirectoryContents.insert(path, QDir(path).entryList());
        }
        m_wildcardWatcher->addDirectories(directoriesToAdd, FileSystemWatcher::WatchModifiedDate);
    }
    if (m_wildcardWatcher) {
        if (result->directoriesWithWildcards.isEmpty()) {
            m_wildcardWatcher.reset();
            m_wildcardDirectoryContents.clear();
        } else {
            const FilePaths directoriesToRemove =
                Utils::filtered<FilePaths>(
                    m_wildcardWatcher->directories(),
                    [&result](const FilePath &path) {
                        return !result->directoriesWithWildcards.contains(path.toFSPathString());
                    });
            m_wildcardWatcher->removeDirectories(directoriesToRemove);
            for (const FilePath &path : directoriesToRemove)
                m_wildcardDirectoryContents.remove(path.toFSPathString());
        }
    }

    setParseInProgress(false);

    updateGeneratedFiles(buildDirectory);

    cleanupProFileReaders();
}

void QmakeProFile::cleanupProFileReaders()
{
    if (m_readerExact)
        m_buildSystem->destroyProFileReader(m_readerExact);
    if (m_readerCumulative)
        m_buildSystem->destroyProFileReader(m_readerCumulative);

    m_readerExact = nullptr;
    m_readerCumulative = nullptr;
}

QString QmakeProFile::uiDirPath(QtSupport::ProFileReader *reader, const FilePath &buildDir)
{
    QString path = reader->value(QLatin1String("UI_DIR"));
    if (QFileInfo(path).isRelative())
        path = QDir::cleanPath(buildDir.toFSPathString() + QLatin1Char('/') + path);
    return path;
}

QString QmakeProFile::mocDirPath(QtSupport::ProFileReader *reader, const FilePath &buildDir)
{
    QString path = reader->value(QLatin1String("MOC_DIR"));
    if (QFileInfo(path).isRelative())
        path = QDir::cleanPath(buildDir.toFSPathString() + QLatin1Char('/') + path);
    return path;
}

QString QmakeProFile::sysrootify(const QString &path, const QString &sysroot,
                                     const QString &baseDir, const QString &outputDir)
{
#ifdef Q_OS_WIN
    Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    if (sysroot.isEmpty() || path.startsWith(sysroot, cs)
        || path.startsWith(baseDir, cs) || path.startsWith(outputDir, cs)) {
        return path;
    }
    QString sysrooted = QDir::cleanPath(sysroot + path);
    return !IoUtils::exists({}, sysrooted) ? path : sysrooted;
}

QStringList QmakeProFile::includePaths(QtSupport::ProFileReader *reader, const FilePath &sysroot,
                                           const FilePath &buildDir, const QString &projectDir)
{
    QStringList paths;
    bool nextIsAnIncludePath = false;
    const QStringList flagList = reader->values(QLatin1String("QMAKE_CXXFLAGS"));
    for (const QString &cxxflags : flagList) {
        if (nextIsAnIncludePath) {
            nextIsAnIncludePath = false;
            paths.append(cxxflags);
        } else if (cxxflags.startsWith(QLatin1String("-I"))) {
            paths.append(cxxflags.mid(2));
        } else if (cxxflags.startsWith(QLatin1String("-isystem"))) {
            nextIsAnIncludePath = true;
        }
    }

    bool tryUnfixified = false;

    // These paths should not be checked for existence, to ensure consistent include path lists
    // before and after building.
    const FilePath mocDir = FilePath::fromString(mocDirPath(reader, buildDir));
    const FilePath uiDir = FilePath::fromString(uiDirPath(reader, buildDir));

    /* We need to make a full valid Utils::FilePath to make sure that remote paths are compared
     * correctly. E.g. we may need to compare paths:
     *   "/C/Users/Admin/DEV/project/build"
     * and
     *   "C:/__qtc_devices__/docker/builder_container.latest/C/Users/Admin/DEV/project/build"
     * Due to buildDir being remote we are sure that the first path of these two must have the same
     * device but it does not. In such cases device from buildDir is added to the path.
     */
    auto createFullFilePath = [&buildDir](QString path) ->FilePath {
        FilePath result = FilePath::fromString(path);
        if (!buildDir.isLocal() && result.isLocal()){
            result.setParts(buildDir.scheme(), buildDir.host(), result.path());
        }
        return result;
    };

    const QList<ProFileEvaluator::SourceFile> elList = reader->fixifiedValues(
                QLatin1String("INCLUDEPATH"), projectDir, buildDir.path(), false);
    for (const ProFileEvaluator::SourceFile &el : elList) {
        const QString sysrootifiedPath = sysrootify(el.fileName, sysroot.path(),
                                                    projectDir,
                                                    buildDir.path());
        const FilePath sysrootifiedFilePath = createFullFilePath(sysrootifiedPath);
        if (sysrootifiedFilePath.isAbsolutePath()
                && (sysrootifiedFilePath.exists() || sysrootifiedFilePath == mocDir
                    || sysrootifiedFilePath == uiDir)) {
            paths << sysrootifiedPath;
        } else {
            tryUnfixified = true;
        }
    }

    // If sysrootifying a fixified path does not yield a valid path, try again with the
    // unfixified value. This can be necessary for cross-building; see QTCREATORBUG-21164.
    if (tryUnfixified) {
        const QStringList rawValues = reader->values("INCLUDEPATH");
        for (const QString &p : rawValues) {
            const QString sysrootifiedPath = sysrootify(QDir::cleanPath(p), sysroot.toUrlishString(),
                                                        projectDir, buildDir.toUrlishString());
            const FilePath sysrootifiedFilePath = createFullFilePath(sysrootifiedPath);
            if (sysrootifiedFilePath.isAbsolutePath() && sysrootifiedFilePath.exists())
                paths << sysrootifiedPath;
        }
    }

    paths.removeDuplicates();
    return paths;
}

QStringList QmakeProFile::libDirectories(QtSupport::ProFileReader *reader)
{
    QStringList result;
    const QStringList values = reader->values(QLatin1String("LIBS"));
    for (const QString &str : values) {
        if (str.startsWith(QLatin1String("-L")))
            result.append(str.mid(2));
    }
    return result;
}

FilePaths QmakeProFile::subDirsPaths(QtSupport::ProFileReader *reader,
                                            const FilePath &projectDir,
                                            FilePaths *subProjectsNotToDeploy,
                                            QStringList *errors)
{
    FilePaths subProjectPaths;

    const QStringList subDirVars = reader->values(QLatin1String("SUBDIRS"));
    for (const QString &subDirVar : subDirVars) {
        // Special case were subdir is just an identifier:
        //   "SUBDIR = subid
        //    subid.subdir = realdir"
        // or
        //   "SUBDIR = subid
        //    subid.file = realdir/realfile.pro"

        FilePath realDir;
        const QString subDirKey = subDirVar + QLatin1String(".subdir");
        const QString subDirFileKey = subDirVar + QLatin1String(".file");
        if (reader->contains(subDirKey))
            realDir = FilePath::fromString(reader->value(subDirKey));
        else if (reader->contains(subDirFileKey))
            realDir = FilePath::fromString(reader->value(subDirFileKey));
        else
            realDir = FilePath::fromString(subDirVar);
        if (!realDir.isAbsolutePath())
            realDir = projectDir / realDir.path();

        FilePath realFile;
        if (realDir.isDir())
            realFile = realDir.pathAppended(realDir.fileName() + ".pro");
        else
            realFile = realDir;

        if (realFile.exists()) {
            subProjectPaths << realFile;
            if (subProjectsNotToDeploy && !subProjectsNotToDeploy->contains(realFile)
                    && reader->values(subDirVar + QLatin1String(".CONFIG"))
                        .contains(QLatin1String("no_default_target"))) {
                subProjectsNotToDeploy->append(realFile);
            }
        } else {
            if (errors)
                errors->append(Tr::tr("Could not find .pro file for subdirectory \"%1\" in \"%2\".")
                               .arg(subDirVar).arg(realDir.toUserOutput()));
        }
    }

    return Utils::filteredUnique(subProjectPaths);
}

TargetInformation QmakeProFile::targetInformation(QtSupport::ProFileReader *reader,
                                                  QtSupport::ProFileReader *readerBuildPass,
                                                  const FilePath &buildDir,
                                                  const FilePath &projectFilePath)
{
    TargetInformation result;
    if (!reader || !readerBuildPass)
        return result;

    QStringList builds = reader->values(QLatin1String("BUILDS"));
    if (!builds.isEmpty()) {
        QString build = builds.first();
        result.buildTarget = reader->value(build + QLatin1String(".target"));
    }

    // BUILD DIR
    result.buildDir = buildDir;

    result.destDir = result.buildDir.resolvePath(readerBuildPass->value(QLatin1String("DESTDIR")));

    // Target
    result.target = readerBuildPass->value(QLatin1String("TARGET"));
    if (result.target.isEmpty())
        result.target = projectFilePath.baseName();

    result.valid = true;

    return result;
}

TargetInformation QmakeProFile::targetInformation() const
{
    return m_qmakeTargetInformation;
}

InstallsList QmakeProFile::installsList(const QtSupport::ProFileReader *reader, const QString &projectFilePath,
                                        const QString &projectDir, const QString &buildDir)
{
    InstallsList result;
    if (!reader)
        return result;
    const QStringList &itemList = reader->values(QLatin1String("INSTALLS"));
    if (itemList.isEmpty())
        return result;

    const QStringList installPrefixVars{"QT_INSTALL_PREFIX", "QT_INSTALL_EXAMPLES"};
    QList<QPair<QString, QString>> installPrefixValues;
    for (const QString &installPrefix : installPrefixVars) {
        installPrefixValues.push_back({reader->propertyValue(installPrefix),
                                       reader->propertyValue(installPrefix + "/dev")});
    }

    for (const QString &item : itemList) {
        const QStringList config = reader->values(item + ".CONFIG");
        const bool active = !config.contains("no_default_install");
        const bool executable = config.contains("executable");
        const QString pathVar = item + QLatin1String(".path");
        const QStringList &itemPaths = reader->values(pathVar);
        if (itemPaths.count() != 1) {
            qDebug("Invalid RHS: Variable '%s' has %d values.",
                qPrintable(pathVar), int(itemPaths.count()));
            if (itemPaths.isEmpty()) {
                qDebug("%s: Ignoring INSTALLS item '%s', because it has no path.",
                    qPrintable(projectFilePath), qPrintable(item));
                continue;
            }
        }

        QString itemPath = itemPaths.last();
        for (const auto &prefixValuePair : std::as_const(installPrefixValues)) {
            if (prefixValuePair.first == prefixValuePair.second
                    || !itemPath.startsWith(prefixValuePair.first)) {
                continue;
            }
            // This is a hack for projects which install into $$[QT_INSTALL_*],
            // in particular Qt itself, examples being most relevant.
            // Projects which implement their own install path policy must
            // parametrize their INSTALLS themselves depending on the intended
            // installation/deployment mode.
            itemPath.replace(0, prefixValuePair.first.length(), prefixValuePair.second);
            break;
        }
        if (item == QLatin1String("target")) {
            if (active)
                result.targetPath = itemPath;
        } else {
            const auto &itemFiles = reader->fixifiedValues(
                        item + QLatin1String(".files"), projectDir, buildDir, true);
            result.items << InstallsItem(itemPath, itemFiles, active, executable);
        }
    }
    return result;
}

InstallsList QmakeProFile::installsList() const
{
    return m_installsList;
}

FilePath QmakeProFile::sourceDir() const
{
    return directoryPath();
}

FilePaths QmakeProFile::generatedFiles(const FilePath &buildDir,
                                       const FilePath &sourceFile,
                                       const FileType &sourceFileType) const
{
    // The mechanism for finding the file names is rather crude, but as we
    // cannot parse QMAKE_EXTRA_COMPILERS and qmake has facilities to put
    // ui_*.h files into a special directory, or even change the .h suffix, we
    // cannot help doing this here.

    if (sourceFileType == FileType::Form) {
        FilePath location;
        auto it = m_varValues.constFind(Variable::UiDir);
        if (it != m_varValues.constEnd() && !it.value().isEmpty())
            location = buildDir.resolvePath(it.value().front());
        else
            location = buildDir;
        if (location.isEmpty())
            return { };
        location = location.pathAppended("ui_"
                                         + sourceFile.completeBaseName()
                                         + singleVariableValue(Variable::HeaderExtension));
        return {location.cleanPath()};
    } else if (sourceFileType == FileType::StateChart) {
        if (buildDir.isEmpty())
            return { };
        const FilePath location = buildDir.pathAppended(sourceFile.completeBaseName());
        return {
            location.stringAppended(singleVariableValue(Variable::HeaderExtension)),
            location.stringAppended(singleVariableValue(Variable::CppExtension))
        };
    }
    return {};
}

QList<ExtraCompiler *> QmakeProFile::extraCompilers() const
{
    return m_extraCompilers;
}

ExtraCompiler *QmakeProFile::findExtraCompiler(
        const std::function<bool(ProjectExplorer::ExtraCompiler *)> &filter)
{
    for (ExtraCompiler * const ec : std::as_const(m_extraCompilers)) {
        if (filter(ec))
            return ec;
    }
    for (QmakePriFile * const priFile : std::as_const(m_children)) {
        if (const auto proFile = dynamic_cast<QmakeProFile *>(priFile)) {
            if (ExtraCompiler * const ec = proFile->findExtraCompiler(filter))
                return ec;
        }
    }
    return nullptr;
}

void QmakeProFile::setupExtraCompiler(const FilePath &buildDir,
                                      const FileType &fileType, ExtraCompilerFactory *factory)
{
    for (const FilePath &fn : collectFiles(fileType)) {
        const FilePaths generated = generatedFiles(buildDir, fn, fileType);
        if (!generated.isEmpty())
            m_extraCompilers.append(factory->create(m_buildSystem->project(), fn, generated));
    }
}

void QmakeProFile::updateGeneratedFiles(const FilePath &buildDir)
{
    // We can do this because other plugins are not supposed to keep the compilers around.
    qDeleteAll(m_extraCompilers);
    m_extraCompilers.clear();

    // Only those project types can have generated files for us
    if (m_projectType != ProjectType::ApplicationTemplate
            && m_projectType != ProjectType::SharedLibraryTemplate
            && m_projectType != ProjectType::StaticLibraryTemplate) {
        return;
    }

    const QList<ExtraCompilerFactory *> factories =
            ProjectExplorer::ExtraCompilerFactory::extraCompilerFactories();

    ExtraCompilerFactory *formFactory
            = Utils::findOrDefault(factories, Utils::equal(&ExtraCompilerFactory::sourceType, FileType::Form));
    if (formFactory)
        setupExtraCompiler(buildDir, FileType::Form, formFactory);
    ExtraCompilerFactory *scxmlFactory
            = Utils::findOrDefault(factories, Utils::equal(&ExtraCompilerFactory::sourceType, FileType::StateChart));
    if (scxmlFactory)
        setupExtraCompiler(buildDir, FileType::StateChart, scxmlFactory);
}
