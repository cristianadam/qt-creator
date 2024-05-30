// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "projectnodes.h"

#include <coreplugin/iversioncontrol.h>
#include <coreplugin/vcsmanager.h>

#include <utils/algorithm.h>
#include <utils/async.h>
#include <utils/filepath.h>
#include <utils/futuresynchronizer.h>
#include <utils/scopedtimer.h>

#include <QPromise>

namespace ProjectExplorer {
namespace Internal {

struct DirectoryScanResult
{
    QList<FileNode *> nodes;
    Utils::FilePaths subDirectories;
};

template<typename Result>
DirectoryScanResult scanForFiles(
    QPromise<Result> &promise,
    double progressStart,
    double progressRange,
    const Utils::FilePath &directory,
    const QDir::Filters &filter,
    const std::function<FileNode *(const Utils::FilePath &)> factory,
    const QList<Core::IVersionControl *> &versionControls)
{
    DirectoryScanResult result;

    QTC_SCOPED_TIMER("scan for files " + directory.toString());
    const Utils::FilePaths entries = directory.dirEntries(filter);
    double progress = 0;
    const double progressIncrement = progressRange / static_cast<double>(entries.count());
    int lastIntProgress = 0;
    Utils::FilePaths subdirs;
    for (const Utils::FilePath &entry : entries) {
        if (promise.isCanceled())
            return result;

        if (!Utils::contains(versionControls, [entry](const Core::IVersionControl *vc) {
                return vc->isVcsFileOrDirectory(entry);
            })) {
            if (entry.isDir()) {
                result.subDirectories.append(entry);
            } else if (FileNode *node = factory(entry)) {
                result.nodes.append(node);
            }
            progress += progressIncrement;
            const int intProgress = std::min(static_cast<int>(progressStart + progress),
                                             promise.future().progressMaximum());
            if (lastIntProgress < intProgress) {
                promise.setProgressValue(intProgress);
                lastIntProgress = intProgress;
            }
        }
    }

    promise.setProgressValue(std::min(static_cast<int>(progressStart + progressRange),
                                      promise.future().progressMaximum()));
    return result;
}

template<typename Result>
QList<FileNode *> scanForFilesRecursively(
    QPromise<Result> &promise,
    double progressStart,
    double progressRange,
    const Utils::FilePath &directory,
    const QDir::Filters &filter,
    const std::function<FileNode *(const Utils::FilePath &)> factory,
    QSet<Utils::FilePath> visited,
    const QList<Core::IVersionControl *> &versionControls)
{
    QTC_SCOPED_TIMER("scan for files recursively " + directory.toString());

    DirectoryScanResult result = scanForFiles(
        promise, progressStart, progressRange, directory, filter, factory, versionControls);
    QList<FileNode *> fileNodes = result.nodes;
    QList<Utils::FilePath> subDirectories = result.subDirectories;
    while (!subDirectories.isEmpty()) {
        Tasking::TaskTree taskTree;
        QList<Tasking::GroupItem> items;
        items.append(Tasking::parallelIdealThreadCountLimit);
        for (const Utils::FilePath &subDirectory : subDirectories) {
            if (!Utils::insert(visited, subDirectory.canonicalPath()))
                continue;

            Utils::AsyncTask<DirectoryScanResult> task{
                [=, &promise](Utils::Async<DirectoryScanResult> &task) {
                    task.setConcurrentCallData([=, &promise](QPromise<DirectoryScanResult> &p) {
                        p.addResult(scanForFiles(
                            promise,
                            progressStart,
                            progressRange,
                            subDirectory,
                            filter,
                            factory,
                            versionControls));
                    });
                },
                [&](const Utils::Async<DirectoryScanResult> &task) {
                    fileNodes.append(task.result().nodes);
                    subDirectories.append(task.result().subDirectories);
                }};
            items.append(task);
        }
        subDirectories.clear();
        taskTree.setRecipe(items);
        taskTree.runBlocking();
    }
    return fileNodes;
}

} // namespace Internal

template<typename Result>
QList<FileNode *> scanForFiles(
    QPromise<Result> &promise,
    const Utils::FilePath &directory,
    const QDir::Filters &filter,
    const std::function<FileNode *(const Utils::FilePath &)> factory)
{
    QSet<Utils::FilePath> visited;
    promise.setProgressRange(0, 1000000);
    return Internal::scanForFilesRecursively(promise,
                                             0.0,
                                             1000000.0,
                                             directory,
                                             filter,
                                             factory,
                                             visited,
                                             Core::VcsManager::versionControls());
}

} // namespace ProjectExplorer
