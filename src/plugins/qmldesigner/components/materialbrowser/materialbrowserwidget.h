// Copyright  (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include "itemlibraryinfo.h"
#include "import.h"
#include "modelnode.h"

#include <utils/fancylineedit.h>
#include <utils/dropsupport.h>

#include <QFrame>
#include <QToolButton>
#include <QFileIconProvider>
#include <QQuickWidget>
#include <QQmlPropertyMap>
#include <QTimer>
#include <QPointF>

#include <memory>

QT_BEGIN_NAMESPACE
class QStackedWidget;
class QShortcut;
QT_END_NAMESPACE

namespace QmlDesigner {

class MaterialBrowserModel;
class PreviewImageProvider;

class MaterialBrowserWidget : public QFrame
{
    Q_OBJECT

public:
    MaterialBrowserWidget();
    ~MaterialBrowserWidget() = default;

    QList<QToolButton *> createToolBarWidgets();

    static QString qmlSourcesPath();
    void clearSearchFilter();

    QPointer<MaterialBrowserModel> materialBrowserModel() const;
    void updateMaterialPreview(const ModelNode &node, const QPixmap &pixmap);

    Q_INVOKABLE void handleSearchfilterChanged(const QString &filterText);
    Q_INVOKABLE void startDragMaterial(int index, const QPointF &mousePos);

    QQuickWidget *quickWidget() const;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void reloadQmlSource();
    void updateSearch();

    QPointer<MaterialBrowserModel> m_materialBrowserModel;
    QScopedPointer<QQuickWidget> m_quickWidget;

    QShortcut *m_qmlSourceUpdateShortcut = nullptr;
    PreviewImageProvider *m_previewImageProvider = nullptr;

    QString m_filterText;

    ModelNode m_materialToDrag;
    QPoint m_dragStartPoint;
};

} // namespace QmlDesigner
