// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "abstractview.h"

#include <utils/uniqueobjectptr.h>

#include <QPointer>
#include <QTimer>

#include <mutex>

namespace Utils {
class FilePath;
}

namespace QmlDesigner {

class AssetsLibraryWidget;
class AsynchronousImageCache;

class AssetsLibraryView : public AbstractView
{
    Q_OBJECT

public:
    AssetsLibraryView(ExternalDependenciesInterface &externalDependencies);
    ~AssetsLibraryView() override;

    bool hasWidget() const override;
    WidgetInfo widgetInfo() override;

    // AbstractView
    void modelAttached(Model *model) override;
    void modelAboutToBeDetached(Model *model) override;
    void modelNodePreviewPixmapChanged(const ModelNode &node,
                                       const QPixmap &pixmap,
                                       const QByteArray &requestId) override;
    void nodeReparented(const ModelNode &node, const NodeAbstractProperty &newPropertyParent,
                        const NodeAbstractProperty &oldPropertyParent,
                        AbstractView::PropertyChangeFlags propertyChange) override;
    void nodeAboutToBeRemoved(const ModelNode &removedNode) override;
    void nodeIdChanged(const ModelNode &node, const QString &newId, const QString &oldId) override;

    void setResourcePath(const QString &resourcePath);

private:
    class ImageCacheData;
    ImageCacheData *imageCacheData();

    void customNotification(const AbstractView *view, const QString &identifier,
                            const QList<ModelNode> &nodeList, const QList<QVariant> &data) override;
    void initMaterialsMetaData();
    QHash<QString, Utils::FilePath> collectMatFiles(const Utils::FilePath &dirPath);

    std::once_flag imageCacheFlag;
    std::unique_ptr<ImageCacheData> m_imageCacheData;
    Utils::UniqueObjectPtr<AssetsLibraryWidget> m_widget;
    QString m_lastResourcePath;
    int m_matLibRetries = 0;
    QTimer m_matSyncTimer;
};

}
