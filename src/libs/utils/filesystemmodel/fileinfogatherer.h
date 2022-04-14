/****************************************************************************
**
** Copyright (C) 2022 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtWidgets module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#pragma once

#include "extendedinformation.h"

#include <QFileIconProvider>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QMutex>
#include <QStack>
#include <QThread>
#include <QWaitCondition>

namespace Utils {

class FileInfoGatherer : public QThread
{
    Q_OBJECT

Q_SIGNALS:
    void updates(const QString &directory, const QList<QPair<QString, QFileInfo>> &updates);
    void newListOfFiles(const QString &directory, const QStringList &listOfFiles) const;
    void nameResolved(const QString &fileName, const QString &resolvedName) const;
    void directoryLoaded(const QString &path);

public:
    explicit FileInfoGatherer(QObject *parent = nullptr);
    ~FileInfoGatherer();

    FilePaths watchedFiles() const;
    FilePaths watchedDirectories() const;
    void watchPaths(const FilePaths &paths);
    void unwatchPaths(const FilePaths &paths);

    bool isWatching() const;
    void setWatching(bool v);

    // only callable from this->thread():
    void clear();
    void removePath(const FilePath &path);
    ExtendedInformation getInfo(const FilePath &info) const;
    QFileIconProvider *iconProvider() const;
    bool resolveSymlinks() const;

public Q_SLOTS:
    void list(const FilePath &directoryPath);
    void fetchExtendedInformation(const FilePaths &paths);
    void updateFile(const FilePath &path);
    void setResolveSymlinks(bool enable);
    void setIconProvider(QFileIconProvider *provider);

private Q_SLOTS:
    void driveAdded();
    void driveRemoved();

private:
    void run() override;
    // called by run():
    void getFileInfos(const QString &path, const QStringList &files);
    void fetch(const QFileInfo &info, QElapsedTimer &base, bool &firstTime,
               QList<QPair<QString, QFileInfo>> &updatedFiles, const QString &path);

private:
    void createWatcher();

    mutable QMutex mutex;
    // begin protected by mutex
    QWaitCondition condition;
    QStack<QString> gatherPaths;
    QStack<QStringList> gatherFiles;
    // end protected by mutex
    QAtomicInt abort;

    QFileSystemWatcher *m_watcher = nullptr;
    bool m_watching = true;

    QFileIconProvider *m_iconProvider; // not accessed by run()
    QFileIconProvider defaultProvider;
    bool m_resolveSymlinks = true; // not accessed by run() // Windows only
};

}
