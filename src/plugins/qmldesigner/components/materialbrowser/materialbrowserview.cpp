// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "materialbrowserview.h"
#include "bindingproperty.h"
#include "materialbrowsermodel.h"
#include "materialbrowserwidget.h"
#include "nodeabstractproperty.h"
#include "nodemetainfo.h"
#include "qmlobjectnode.h"
#include "variantproperty.h"

#include <coreplugin/icore.h>
#include <designmodecontext.h>
#include <nodeinstanceview.h>
#include <nodemetainfo.h>
#include <qmldesignerconstants.h>

#include <QQuickItem>

namespace QmlDesigner {

MaterialBrowserView::MaterialBrowserView() {}

MaterialBrowserView::~MaterialBrowserView()
{}

bool MaterialBrowserView::hasWidget() const
{
    return true;
}

WidgetInfo MaterialBrowserView::widgetInfo()
{
    if (m_widget.isNull()) {
        m_widget = new MaterialBrowserWidget(this);

        auto matEditorContext = new Internal::MaterialBrowserContext(m_widget.data());
        Core::ICore::addContextObject(matEditorContext);

        MaterialBrowserModel *matBrowserModel = m_widget->materialBrowserModel().data();

        // custom notifications below are sent to the MaterialEditor

        connect(matBrowserModel, &MaterialBrowserModel::selectedIndexChanged, this, [&] (int idx) {
            ModelNode matNode = m_widget->materialBrowserModel()->materialAt(idx);
            emitCustomNotification("selected_material_changed", {matNode}, {});
        });

        connect(matBrowserModel, &MaterialBrowserModel::applyToSelectedTriggered, this,
                [&] (const ModelNode &material, bool add) {
            emitCustomNotification("apply_to_selected_triggered", {material}, {add});
        });

        connect(matBrowserModel, &MaterialBrowserModel::renameMaterialTriggered, this,
                [&] (const ModelNode &material, const QString &newName) {
            emitCustomNotification("rename_material", {material}, {newName});
        });

        connect(matBrowserModel, &MaterialBrowserModel::addNewMaterialTriggered, this, [&] {
            emitCustomNotification("add_new_material");
        });

        connect(matBrowserModel, &MaterialBrowserModel::duplicateMaterialTriggered, this,
                [&] (const ModelNode &material) {
            emitCustomNotification("duplicate_material", {material});
        });

        connect(matBrowserModel, &MaterialBrowserModel::pasteMaterialPropertiesTriggered, this,
                [&] (const ModelNode &material, const QList<AbstractProperty> &props, bool all) {
            QmlObjectNode mat(material);
            executeInTransaction(__FUNCTION__, [&] {
                if (all) { // all material properties copied
                    // remove current properties
                    const PropertyNameList propNames = material.propertyNames();
                    for (const PropertyName &propName : propNames) {
                        if (propName != "objectName")
                            mat.removeProperty(propName);
                    }
                }

                // apply pasted properties
                for (const AbstractProperty &prop : props) {
                    if (prop.name() == "objectName")
                        continue;

                    if (prop.isVariantProperty())
                        mat.setVariantProperty(prop.name(), prop.toVariantProperty().value());
                    else if (prop.isBindingProperty())
                        mat.setBindingProperty(prop.name(), prop.toBindingProperty().expression());
                }
            });
        });
    }

    return createWidgetInfo(m_widget.data(),
                            "MaterialBrowser",
                            WidgetInfo::LeftPane,
                            0,
                            tr("Material Browser"));
}

void MaterialBrowserView::modelAttached(Model *model)
{
    AbstractView::modelAttached(model);

    m_widget->clearSearchFilter();
    m_widget->materialBrowserModel()->setHasMaterialRoot(
        rootModelNode().metaInfo().isQtQuick3DMaterial());
    m_hasQuick3DImport = model->hasImport("QtQuick3D");

    loadPropertyGroups();

    // Project load is already very busy and may even trigger puppet reset, so let's wait a moment
    // before refreshing the model
    QTimer::singleShot(1000, this, [this]() {
        refreshModel(true);
    });
}

void MaterialBrowserView::refreshModel(bool updateImages)
{
    if (!model() || !model()->nodeInstanceView())
        return;

    ModelNode matLib = modelNodeForId(Constants::MATERIAL_LIB_ID);
    QList <ModelNode> materials;

    if (m_hasQuick3DImport && matLib.isValid()) {
        const QList <ModelNode> matLibNodes = matLib.directSubModelNodes();
        for (const ModelNode &node : matLibNodes) {
            if (isMaterial(node))
                materials.append(node);
        }
    }

    m_widget->materialBrowserModel()->setMaterials(materials, m_hasQuick3DImport);

    if (updateImages) {
        for (const ModelNode &node : std::as_const(materials))
            model()->nodeInstanceView()->previewImageDataForGenericNode(node, {});
    }
}

bool MaterialBrowserView::isMaterial(const ModelNode &node) const
{
    if (!node.isValid())
        return false;

    return node.metaInfo().isQtQuick3DMaterial();
}

void MaterialBrowserView::modelAboutToBeDetached(Model *model)
{
    m_widget->materialBrowserModel()->setMaterials({}, m_hasQuick3DImport);

    AbstractView::modelAboutToBeDetached(model);
}

void MaterialBrowserView::selectedNodesChanged(const QList<ModelNode> &selectedNodeList,
                                               [[maybe_unused]] const QList<ModelNode> &lastSelectedNodeList)
{
    ModelNode selectedModel;

    for (const ModelNode &node : selectedNodeList) {
        if (node.metaInfo().isQtQuick3DModel()) {
            selectedModel = node;
            break;
        }
    }

    m_widget->materialBrowserModel()->setHasModelSelection(selectedModel.isValid());

    if (!m_autoSelectModelMaterial)
        return;

    if (selectedNodeList.size() > 1 || !selectedModel.isValid())
        return;

    QmlObjectNode qmlObjNode(selectedModel);
    QString matExp = qmlObjNode.expression("materials");
    if (matExp.isEmpty())
        return;

    QString matId = matExp.remove('[').remove(']').split(',', Qt::SkipEmptyParts).at(0);
    ModelNode mat = modelNodeForId(matId);
    if (!mat.isValid())
        return;

    // if selected object is a model, select its material in the material browser and editor
    int idx = m_widget->materialBrowserModel()->materialIndex(mat);
    m_widget->materialBrowserModel()->selectMaterial(idx);
}

void MaterialBrowserView::modelNodePreviewPixmapChanged(const ModelNode &node, const QPixmap &pixmap)
{
    if (isMaterial(node))
        m_widget->updateMaterialPreview(node, pixmap);
}

void MaterialBrowserView::variantPropertiesChanged(const QList<VariantProperty> &propertyList,
                                                   [[maybe_unused]] PropertyChangeFlags propertyChange)
{
    for (const VariantProperty &property : propertyList) {
        ModelNode node(property.parentModelNode());

        if (isMaterial(node) && property.name() == "objectName")
            m_widget->materialBrowserModel()->updateMaterialName(node);
    }
}

void MaterialBrowserView::nodeReparented(const ModelNode &node,
                                         const NodeAbstractProperty &newPropertyParent,
                                         const NodeAbstractProperty &oldPropertyParent,
                                         [[maybe_unused]] PropertyChangeFlags propertyChange)
{
    if (!isMaterial(node))
        return;

    ModelNode newParentNode = newPropertyParent.parentModelNode();
    ModelNode oldParentNode = oldPropertyParent.parentModelNode();
    bool matAdded = newParentNode.isValid() && newParentNode.id() == Constants::MATERIAL_LIB_ID;
    bool matRemoved = oldParentNode.isValid() && oldParentNode.id() == Constants::MATERIAL_LIB_ID;

    if (matAdded || matRemoved) {
        if (matAdded && !m_puppetResetPending) {
            // Workaround to fix various material issues all likely caused by QTBUG-103316
            resetPuppet();
            m_puppetResetPending = true;
        }
        refreshModel(!matAdded);
        int idx = m_widget->materialBrowserModel()->materialIndex(node);
        m_widget->materialBrowserModel()->selectMaterial(idx);
    }
}

void MaterialBrowserView::nodeAboutToBeRemoved(const ModelNode &removedNode)
{
    // removing the material editor node
    if (removedNode.isValid() && removedNode.id() == Constants::MATERIAL_LIB_ID) {
        m_widget->materialBrowserModel()->setMaterials({}, m_hasQuick3DImport);
        return;
    }

    // not a material under the material editor
    if (!isMaterial(removedNode)
        || removedNode.parentProperty().parentModelNode().id() != Constants::MATERIAL_LIB_ID) {
        return;
    }

    m_widget->materialBrowserModel()->removeMaterial(removedNode);
}

void MaterialBrowserView::nodeRemoved([[maybe_unused]] const ModelNode &removedNode,
                                      const NodeAbstractProperty &parentProperty,
                                      [[maybe_unused]] PropertyChangeFlags propertyChange)
{
    if (parentProperty.parentModelNode().id() != Constants::MATERIAL_LIB_ID)
        return;

    m_widget->materialBrowserModel()->updateSelectedMaterial();
}

void QmlDesigner::MaterialBrowserView::loadPropertyGroups()
{
    if (!m_hasQuick3DImport || m_propertyGroupsLoaded)
        return;

    QString matPropsPath = model()->metaInfo("QtQuick3D.Material").importDirectoryPath()
                               + "/designer/propertyGroups.json";
    m_propertyGroupsLoaded = m_widget->materialBrowserModel()->loadPropertyGroups(matPropsPath);
}

void MaterialBrowserView::importsChanged([[maybe_unused]] const QList<Import> &addedImports,
                                         [[maybe_unused]] const QList<Import> &removedImports)
{
    bool hasQuick3DImport = model()->hasImport("QtQuick3D");

    if (hasQuick3DImport == m_hasQuick3DImport)
        return;

    m_hasQuick3DImport = hasQuick3DImport;

    loadPropertyGroups();

    // Import change will trigger puppet reset, so we don't want to update previews immediately
    refreshModel(false);
}

void MaterialBrowserView::customNotification(const AbstractView *view,
                                             const QString &identifier,
                                             const QList<ModelNode> &nodeList,
                                             [[maybe_unused]] const QList<QVariant> &data)
{
    if (view == this)
        return;

    if (identifier == "selected_material_changed") {
        int idx = m_widget->materialBrowserModel()->materialIndex(nodeList.first());
        if (idx != -1)
            m_widget->materialBrowserModel()->selectMaterial(idx);
    } else if (identifier == "refresh_material_browser") {
        QTimer::singleShot(0, this, [this]() {
            refreshModel(true);
        });
    } else if (identifier == "delete_selected_material") {
        m_widget->materialBrowserModel()->deleteSelectedMaterial();
    }
}

void MaterialBrowserView::instancesCompleted(const QVector<ModelNode> &completedNodeList)
{
    for (const ModelNode &node : completedNodeList) {
        // We use root node completion as indication of puppet reset
        if (node.isRootNode()) {
            m_puppetResetPending  = false;
            QTimer::singleShot(1000, this, [this]() {
                if (!model() || !model()->nodeInstanceView())
                    return;
                const QList<ModelNode> materials = m_widget->materialBrowserModel()->materials();
                for (const ModelNode &node : materials)
                    model()->nodeInstanceView()->previewImageDataForGenericNode(node, {});
            });
            break;
        }
    }
}

} // namespace QmlDesigner
