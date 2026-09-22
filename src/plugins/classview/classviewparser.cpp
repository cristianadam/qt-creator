// Copyright (C) 2016 Denis Mingulov
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "classviewparser.h"

#include <cppeditor/cppcodemodelqueries.h>
#include <cppeditor/cppmodelmanager.h>
#include <cppeditor/cppprojectfile.h>

#include <cplusplus/CppDocument.h>

#include <QElapsedTimer>
#include <QDebug>
#include <QHash>
#include <QSet>

enum { debug = false };

using namespace ProjectExplorer;
using namespace Utils;

namespace ClassView::Internal {

// ----------------------------- ParserPrivate ---------------------------------

/*!
   \class ParserPrivate
   \brief The ParserPrivate class defines private class data for the Parser
   class.
   \sa Parser
 */

/*!
   \class Parser
   \brief The Parser class parses C++ information. Multithreading is supported.
*/

class ParserPrivate
{
public:
    // How many times a file has been read again since this parser started,
    // which is all a cached tree needs to know about it. Counted here rather
    // than taken off a parsed document's revision: whether a front end holds
    // a document for a file is that front end's business, and the built-in
    // one holds none for a project it was never asked to index.
    unsigned revision(const FilePath &filePath) const
    {
        return m_fileRevision.value(filePath, 0);
    }

    struct DocumentCache {
        unsigned treeRevision = 0;
        ParserTreeItem::ConstPtr tree;
    };
    struct ProjectCache {
        unsigned treeRevision = 0;
        ParserTreeItem::ConstPtr tree;
        QString projectName;
        QSet<FilePath> fileNames;
    };

    // Project file path to its cached data
    QHash<FilePath, DocumentCache> m_documentCache;
    // Project file path to its cached data
    QHash<FilePath, ProjectCache> m_projectCache;
    // Source file path to the number of times it was reported read again
    QHash<FilePath, unsigned> m_fileRevision;

    // What a question about a file is asked with: the built-in front end's
    // reading, which stands for it where it is the one that has the file, and
    // the text of whatever is being edited.
    CPlusPlus::Snapshot m_snapshot;
    CppEditor::WorkingCopy m_workingCopy;

    //! Flat mode
    bool flatMode = false;
};

// The files of a project worth asking what they declare. A project lists
// everything it is built from -- forms, resources, QML, whatever it installs
// -- and a file no front end reads as C++ has nothing to say here.
static QSet<FilePath> filesToRead(const FilePaths &filesInProject)
{
    QSet<FilePath> files;
    files.reserve(filesInProject.size());
    for (const FilePath &filePath : filesInProject) {
        if (CppEditor::ProjectFile::isCppFile(filePath))
            files.insert(filePath);
    }
    return files;
}

// ----------------------------- Parser ---------------------------------

/*!
    Constructs the parser object.
*/

Parser::Parser(QObject *parent)
    : QObject(parent),
    d(new ParserPrivate())
{
}

/*!
    Destructs the parser object.
*/

Parser::~Parser()
{
    delete d;
}

/*!
    Switches to flat mode (without subprojects) if \a flat returns \c true.
*/

void Parser::setFlatMode(bool flatMode)
{
    if (flatMode == d->flatMode)
        return;

    // change internal
    d->flatMode = flatMode;

    // regenerate and resend current tree
    requestCurrentState();
}

/*!
    Parses the class and produces a new tree.

    \sa addProject
*/

ParserTreeItem::ConstPtr Parser::parse()
{
    QScopedPointer<QElapsedTimer> timer;
    if (debug) {
        timer.reset(new QElapsedTimer());
        timer->start();
    }

    QHash<SymbolInformation, ParserTreeItem::ConstPtr> projectTrees;

    for (auto it = d->m_projectCache.cbegin(); it != d->m_projectCache.cend(); ++it) {
        const ParserPrivate::ProjectCache &projectCache = it.value();
        const FilePath projectPath = it.key();
        const SymbolInformation projectInfo = {projectCache.projectName, projectPath.path()};
        ParserTreeItem::ConstPtr item = getCachedOrParseProjectTree(projectPath, projectCache.fileNames);
        if (!item)
            continue;
        projectTrees.insert(projectInfo, item);
    }

    ParserTreeItem::ConstPtr rootItem(new ParserTreeItem(projectTrees));

    if (debug) {
        qDebug() << "Class View:" << QDateTime::currentDateTime().toString()
                 << "Parsed in " << timer->elapsed() << "msecs.";
    }

    return rootItem;
}

/*!
    Parses the project with the \a projectId and adds the documents from the
    \a fileList to the project. Updates the internal cached tree for this
    project.
*/

ParserTreeItem::ConstPtr Parser::getParseProjectTree(const FilePath &projectPath,
                                                     const QSet<FilePath> &filesInProject)
{
    //! \todo Way to optimize - for documentUpdate - use old cached project and subtract
    //! changed files only (old edition), and add curent editions

    QList<ParserTreeItem::ConstPtr> docTrees;
    unsigned revision = 0;
    for (const FilePath &fileInProject : filesInProject) {
        revision += d->revision(fileInProject);

        const ParserTreeItem::ConstPtr docTree = getCachedOrParseDocumentTree(fileInProject);
        if (!docTree)
            continue;
        docTrees.append(docTree);
    }

    ParserTreeItem::ConstPtr item = ParserTreeItem::mergeTrees(projectPath, docTrees);

    // update the cache
    if (!projectPath.isEmpty()) {
        ParserPrivate::ProjectCache &projectCache = d->m_projectCache[projectPath];
        projectCache.tree = item;
        projectCache.treeRevision = revision;
    }
    return item;
}

/*!
    Gets the project with \a projectId from the cache if it is valid or parses
    the project and adds the documents from the \a fileList to the project.
    Updates the internal cached tree for this project.
*/

ParserTreeItem::ConstPtr Parser::getCachedOrParseProjectTree(const FilePath &projectPath,
                                                             const QSet<FilePath> &filesInProject)
{
    const auto it = d->m_projectCache.constFind(projectPath);
    if (it != d->m_projectCache.constEnd() && it.value().tree) {
        // calculate project's revision
        unsigned revision = 0;
        for (const FilePath &fileInProject : filesInProject)
            revision += d->revision(fileInProject);

        // if even revision is the same, return cached project
        if (revision == it.value().treeRevision)
            return it.value().tree;
    }

    return getParseProjectTree(projectPath, filesInProject);
}

/*!
    Asks what \a filePath declares and makes a tree of it. Updates the
    internal cached tree for this file.

    \sa fromDeclarations
*/

ParserTreeItem::ConstPtr Parser::getParseDocumentTree(const FilePath &filePath)
{
    if (filePath.isEmpty())
        return ParserTreeItem::ConstPtr();

    const CppEditor::CodeModelQueries queries(d->m_snapshot, d->m_workingCopy);
    ParserTreeItem::ConstPtr itemPtr = ParserTreeItem::fromDeclarations(
        queries.declarationsIn(filePath));

    d->m_documentCache.insert(filePath, { d->revision(filePath), itemPtr } );
    return itemPtr;
}

/*!
    Gets the tree for \a filePath from the cache, or makes one if what is
    cached is older than the last reading of the file.

    \sa getParseDocumentTree
*/

ParserTreeItem::ConstPtr Parser::getCachedOrParseDocumentTree(const FilePath &filePath)
{
    if (filePath.isEmpty())
        return ParserTreeItem::ConstPtr();

    const auto it = d->m_documentCache.constFind(filePath);
    if (it != d->m_documentCache.constEnd() && it.value().tree
            && it.value().treeRevision == d->revision(filePath)) {
        return it.value().tree;
    }
    return getParseDocumentTree(filePath);
}

/*!
    Makes a tree of what each file of \a documentPaths declares, those having
    just been read again, and adds it to the internal storage.
*/

void Parser::updateDocuments(const QSet<FilePath> &documentPaths,
                             const CppEditor::WorkingCopy &workingCopy)
{
    d->m_workingCopy = workingCopy;
    d->m_snapshot = CppEditor::CppModelManager::snapshot();

    // Read again is what makes a tree of it older than the file, so this is
    // counted before anything asks for one.
    for (const FilePath &documentPath : documentPaths)
        ++d->m_fileRevision[documentPath];

    updateDocumentTrees(documentPaths);
}

void Parser::updateDocumentTrees(const QSet<FilePath> &documentPaths)
{
    for (const FilePath &documentPath : documentPaths)
        getParseDocumentTree(documentPath);
    requestCurrentState();
}

/*!
    Removes the files defined in the \a fileList from the parsing.
*/

void Parser::removeFiles(const FilePaths &fileList)
{
    if (fileList.isEmpty())
        return;

    for (const FilePath &filePath : fileList) {
        d->m_documentCache.remove(filePath);
        d->m_fileRevision.remove(filePath);
        d->m_projectCache.remove(filePath);
        for (auto it = d->m_projectCache.begin(); it != d->m_projectCache.end(); ++it) {
            if (!it.value().fileNames.remove(filePath))
                continue;

            // The tree it holds is of a set of files this one is no longer
            // in, and no revision moved to say so -- a file that is gone
            // reports nothing read again. Told here instead, which is where
            // the set changes.
            it.value().tree = {};
            it.value().treeRevision = 0;
        }
    }
    requestCurrentState();
}

/*!
    Fully resets the internal state of the code parser to the \a projects
    given and the files they are built from.
*/
void Parser::resetData(const QHash<FilePath, QPair<QString, FilePaths>> &projects,
                       const CppEditor::WorkingCopy &workingCopy)
{
    d->m_projectCache.clear();
    d->m_documentCache.clear();
    d->m_fileRevision.clear();
    d->m_workingCopy = workingCopy;
    d->m_snapshot = CppEditor::CppModelManager::snapshot();

    for (auto it = projects.cbegin(); it != projects.cend(); ++it) {
        const auto projectData = it.value();
        d->m_projectCache.insert(
            it.key(), {0, nullptr, projectData.first, filesToRead(projectData.second)});
    }

    requestCurrentState();
}

void Parser::addProject(const FilePath &projectPath, const QString &projectName,
                        const FilePaths &filesInProject,
                        const CppEditor::WorkingCopy &workingCopy)
{
    d->m_workingCopy = workingCopy;
    d->m_snapshot = CppEditor::CppModelManager::snapshot();

    const QSet<FilePath> files = filesToRead(filesInProject);
    d->m_projectCache.insert(projectPath, {0, nullptr, projectName, files});
    updateDocumentTrees(files);
}

void Parser::removeProject(const FilePath &projectPath)
{
    auto it = d->m_projectCache.find(projectPath);
    if (it == d->m_projectCache.end())
        return;

    const QSet<FilePath> &filesInProject = it.value().fileNames;
    for (const FilePath &fileInProject : filesInProject) {
        d->m_documentCache.remove(fileInProject);
        d->m_fileRevision.remove(fileInProject);
    }

    d->m_projectCache.erase(it);

    requestCurrentState();
}

/*!
    Requests to emit a signal with the current tree state.
*/
void Parser::requestCurrentState()
{
    emit treeRegenerated(parse());
}

} // namespace ClassView::Internal
