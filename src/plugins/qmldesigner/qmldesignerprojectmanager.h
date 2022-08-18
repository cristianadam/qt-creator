// Copyright  (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <QList>
#include <QObject>

#include <memory>
#include <mutex>

QT_FORWARD_DECLARE_CLASS(QQmlEngine)

namespace Core {
class IEditor;
} // namespace Core

namespace ProjectExplorer {
class Project;
class Target;
} // namespace ProjectExplorer

namespace Sqlite {
class Database;
}

namespace QmlDesigner {

template<typename Database>
class ProjectStorage;

class QmlDesignerProjectManager : public QObject
{
    Q_OBJECT

    class QmlDesignerProjectManagerProjectData;
    class PreviewImageCacheData;
    class ImageCacheData;

public:
    QmlDesignerProjectManager();
    ~QmlDesignerProjectManager();

    void registerPreviewImageProvider(QQmlEngine *engine) const;

    class AsynchronousImageCache &asynchronousImageCache();
    ProjectStorage<Sqlite::Database> &projectStorage();

private:
    void editorOpened(::Core::IEditor *editor);
    void currentEditorChanged(::Core::IEditor *);
    void editorsClosed(const QList<Core::IEditor *> &editor);
    void projectAdded(::ProjectExplorer::Project *project);
    void aboutToRemoveProject(::ProjectExplorer::Project *project);
    void projectRemoved(::ProjectExplorer::Project *project);
    ImageCacheData *imageCacheData();

    void fileListChanged();
    void activeTargetChanged(::ProjectExplorer::Target *target);
    void aboutToRemoveTarget(::ProjectExplorer::Target *target);
    void kitChanged();
    void projectChanged();

private:
    void update();

private:
    std::once_flag imageCacheFlag;
    std::unique_ptr<ImageCacheData> m_imageCacheData;
    std::unique_ptr<PreviewImageCacheData> m_previewImageCacheData;
    std::unique_ptr<QmlDesignerProjectManagerProjectData> m_projectData;
};
} // namespace QmlDesigner
