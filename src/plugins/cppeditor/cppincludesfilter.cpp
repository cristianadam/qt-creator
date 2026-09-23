// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppincludesfilter.h"

#include "cppcodemodelqueries.h"
#include "cppeditorconstants.h"
#include "cppeditortr.h"
#include "cppmodelmanager.h"

#include <coreplugin/editormanager/documentmodel.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>

using namespace Core;
using namespace ProjectExplorer;
using namespace QtTaskTree;
using namespace Utils;

namespace CppEditor::Internal {

FilePaths filesIncludedBy(const CodeModelQueries &queries,
                          const std::unordered_set<FilePath> &inputFilePaths,
                          const QFuture<void> &future)
{
    FilePaths results;
    std::unordered_set<FilePath> resultsCache;
    std::unordered_set<FilePath> queuedPaths = inputFilePaths;

    // One walk with one set of the files it has been to, rather than each
    // file's whole closure asked for in turn: a project's sources share
    // nearly all of their closures, and asking per file re-walks that.
    while (!queuedPaths.empty()) {
        if (future.isCanceled())
            return {};

        const auto iterator = queuedPaths.cbegin();
        const FilePath filePath = *iterator;
        queuedPaths.erase(iterator);
        const FilePaths includedFiles = queries.directIncludesOf(filePath);
        for (const FilePath &includedFile : includedFiles) {
            if (resultsCache.emplace(includedFile).second) {
                queuedPaths.emplace(includedFile);
                results.append(includedFile);
            }
        }
    }
    return results;
}

CppIncludesFilter::CppIncludesFilter()
{
    setId(Constants::INCLUDES_FILTER_ID);
    setDisplayName(Tr::tr(Constants::INCLUDES_FILTER_DISPLAY_NAME));
    setDescription(
        Tr::tr("Locates files that are included by C++ files of any open project. Append "
               "\"+<number>\" or \":<number>\" to jump to the given line number. Append another "
               "\"+<number>\" or \":<number>\" to jump to the column number as well."));
    setDefaultShortcutString("ai");
    setDefaultIncludedByDefault(true);
    const auto invalidate = [this] { m_cache.invalidate(); };
    setRefreshRecipe(QSyncTask([invalidate] { invalidate(); }));
    setPriority(ILocatorFilter::Low);

    connect(ProjectExplorerPlugin::instance(), &ProjectExplorerPlugin::fileListChanged,
            this, invalidate);
    connect(CppModelManager::instance(), &CppModelManager::fileUpdated,
            this, invalidate);
    connect(CppModelManager::instance(), &CppModelManager::aboutToRemoveFiles,
            this, invalidate);
    connect(DocumentModel::model(), &QAbstractItemModel::rowsInserted,
            this, invalidate);
    connect(DocumentModel::model(), &QAbstractItemModel::rowsRemoved,
            this, invalidate);
    connect(DocumentModel::model(), &QAbstractItemModel::dataChanged,
            this, invalidate);
    connect(DocumentModel::model(), &QAbstractItemModel::modelReset,
            this, invalidate);

    const auto generatorProvider = [] {
        // This body runs in main thread
        std::unordered_set<FilePath> inputFilePaths;
        for (Project *project : ProjectManager::projects()) {
            const FilePaths allFiles = project->files(Project::SourceFiles);
            for (const FilePath &filePath : allFiles)
                inputFilePaths.insert(filePath);
        }
        const QList<DocumentModel::Entry *> entries = DocumentModel::entries();
        for (DocumentModel::Entry *entry : entries) {
            if (entry)
                inputFilePaths.insert(entry->filePath());
        }
        // Made here because a working copy is what the editors hold, which
        // is this thread's to read -- and kept alive by the lambda below,
        // which runs elsewhere.
        const auto queries = std::make_shared<CodeModelQueries>(
            CppModelManager::snapshot(), CppModelManager::workingCopy());
        return [queries, inputFilePaths](const QFuture<void> &future) {
            // This body runs in non-main thread
            return filesIncludedBy(*queries, inputFilePaths, future);
        };
    };
    m_cache.setGeneratorProvider(generatorProvider);
}

} // namespace CppEditor::Internal
