// Copyright  (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <abstractview.h>
#include <QHash>
#include <QTimer>

QT_BEGIN_NAMESPACE
class QShortcut;
class QStackedWidget;
class QTimer;
QT_END_NAMESPACE

namespace QmlDesigner {

class ModelNode;
class MaterialEditorQmlBackend;

class MaterialEditorView : public AbstractView
{
    Q_OBJECT

public:
    MaterialEditorView(QWidget *parent = nullptr);
    ~MaterialEditorView() override;

    bool hasWidget() const override;
    WidgetInfo widgetInfo() override;

    void selectedNodesChanged(const QList<ModelNode> &selectedNodeList,
                              const QList<ModelNode> &lastSelectedNodeList) override;

    void propertiesRemoved(const QList<AbstractProperty> &propertyList) override;

    void modelAttached(Model *model) override;
    void modelAboutToBeDetached(Model *model) override;

    void variantPropertiesChanged(const QList<VariantProperty> &propertyList, PropertyChangeFlags propertyChange) override;
    void bindingPropertiesChanged(const QList<BindingProperty> &propertyList, PropertyChangeFlags propertyChange) override;
    void auxiliaryDataChanged(const ModelNode &node,
                              AuxiliaryDataKeyView key,
                              const QVariant &data) override;

    void resetView();
    void currentStateChanged(const ModelNode &node) override;
    void instancePropertyChanged(const QList<QPair<ModelNode, PropertyName> > &propertyList) override;

    void nodeTypeChanged(const ModelNode& node, const TypeName &type, int majorVersion, int minorVersion) override;
    void modelNodePreviewPixmapChanged(const ModelNode &node, const QPixmap &pixmap) override;
    void importsChanged(const QList<Import> &addedImports, const QList<Import> &removedImports) override;
    void customNotification(const AbstractView *view, const QString &identifier,
                            const QList<ModelNode> &nodeList, const QList<QVariant> &data) override;

    void dragStarted(QMimeData *mimeData) override;
    void dragEnded() override;

    void changeValue(const QString &name);
    void changeExpression(const QString &name);
    void exportPropertyAsAlias(const QString &name);
    void removeAliasExport(const QString &name);

    bool locked() const;

    void currentTimelineChanged(const ModelNode &node) override;

public slots:
    void handleToolBarAction(int action);

protected:
    void timerEvent(QTimerEvent *event) override;
    void setValue(const QmlObjectNode &fxObjectNode, const PropertyName &name, const QVariant &value);

private:
    static QString materialEditorResourcesPath();

    void reloadQml();
    void highlightSupportedProperties(bool highlight = true);
    QString generateIdFromName(const QString &name);

    void requestPreviewRender();
    void applyMaterialToSelectedModels(const ModelNode &material, bool add = false);

    void delayedResetView();
    void setupQmlBackend();

    void commitVariantValueToModel(const PropertyName &propertyName, const QVariant &value);
    void commitAuxValueToModel(const PropertyName &propertyName, const QVariant &value);
    void removePropertyFromModel(const PropertyName &propertyName);
    void renameMaterial(ModelNode &material, const QString &newName);
    void duplicateMaterial(const ModelNode &material);

    bool noValidSelection() const;

    ModelNode m_selectedMaterial;
    QTimer m_ensureMatLibTimer;
    QShortcut *m_updateShortcut = nullptr;
    int m_timerId = 0;
    QStackedWidget *m_stackedWidget = nullptr;
    QList<ModelNode> m_selectedModels;
    QHash<QString, MaterialEditorQmlBackend *> m_qmlBackendHash;
    MaterialEditorQmlBackend *m_qmlBackEnd = nullptr;
    bool m_locked = false;
    bool m_setupCompleted = false;
    bool m_hasQuick3DImport = false;
};

} // namespace QmlDesigner
