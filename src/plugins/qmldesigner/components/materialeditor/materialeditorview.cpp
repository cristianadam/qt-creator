// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "materialeditorview.h"

#include "asset.h"
#include "auxiliarydataproperties.h"
#include "bindingproperty.h"
#include "designdocument.h"
#include "designmodewidget.h"
#include "dynamicpropertiesmodel.h"
#include "externaldependenciesinterface.h"
#include "materialeditorcontextobject.h"
#include "materialeditordynamicpropertiesproxymodel.h"
#include "materialeditorqmlbackend.h"
#include "materialeditortransaction.h"
#include "metainfo.h"
#include "nodeinstanceview.h"
#include "nodelistproperty.h"
#include "nodemetainfo.h"
#include "propertyeditorqmlbackend.h"
#include "propertyeditorvalue.h"
#include "qmldesignerconstants.h"
#include "qmldesignerplugin.h"
#include "qmltimeline.h"
#include <sourcepathcacheinterface.h>
#include "variantproperty.h"
#include <uniquename.h>
#include <utils3d.h>

#include <coreplugin/icore.h>
#include <coreplugin/messagebox.h>

#include <sourcepathstorage/sourcepathcache.h>

#include <utils/environment.h>
#include <utils/qtcassert.h>

#include <QDir>
#include <QFileInfo>
#include <QQuickItem>
#include <QStackedWidget>
#include <QShortcut>
#include <QColorDialog>

namespace QmlDesigner {

static const char MATERIAL_EDITOR_IMAGE_REQUEST_ID[] = "MaterialEditor";

static bool containsTexture(const ModelNode &node)
{
    if (node.metaInfo().isQtQuick3DTexture())
        return true;

    const ModelNodes children = node.allSubModelNodes();
    for (const ModelNode &child : children) {
        if (child.metaInfo().isQtQuick3DTexture())
            return true;
    }
    return false;
};

static bool isPreviewAuxiliaryKey(AuxiliaryDataKeyView key)
{
    static const QVector<AuxiliaryDataKeyView> previewKeys = [] {
        QVector<AuxiliaryDataKeyView> previewKeys{
            materialPreviewEnvDocProperty,
            materialPreviewEnvValueDocProperty,
            materialPreviewModelDocProperty,
            materialPreviewEnvProperty,
            materialPreviewEnvValueProperty,
            materialPreviewModelProperty,
        };
        std::sort(previewKeys.begin(), previewKeys.end());
        return previewKeys;
    }();
    return Utils::containsInSorted(previewKeys, key);
}

MaterialEditorView::MaterialEditorView(ExternalDependenciesInterface &externalDependencies)
    : AbstractView{externalDependencies}
    , m_stackedWidget(new QStackedWidget)
    , m_propertyComponentGenerator{PropertyEditorQmlBackend::propertyEditorResourcesPath(), model()}
    , m_dynamicPropertiesModel(new DynamicPropertiesModel(true, this))
{
    m_updateShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F7), m_stackedWidget);
    connect(m_updateShortcut, &QShortcut::activated, this, &MaterialEditorView::reloadQml);

    QmlDesignerPlugin::trackWidgetFocusTime(m_stackedWidget, Constants::EVENT_MATERIALEDITOR_TIME);

    MaterialEditorDynamicPropertiesProxyModel::registerDeclarativeType();
}

MaterialEditorView::~MaterialEditorView()
{
    qDeleteAll(m_qmlBackendHash);
}

QUrl MaterialEditorView::getPaneUrl()
{
    QString panePath = MaterialEditorQmlBackend::materialEditorResourcesPath();
    if (m_selectedMaterial.isValid() && m_hasQuick3DImport
        && (Utils3D::materialLibraryNode(this).isValid() || m_hasMaterialRoot))
        panePath.append("/MaterialEditorPane.qml");
    else {
        panePath.append("/EmptyMaterialEditorPane.qml");
    }

    return QUrl::fromLocalFile(panePath);
}

namespace {

#ifdef QDS_USE_PROJECTSTORAGE
QUrl getSpecificsUrl(const NodeMetaInfos &prototypes, const SourcePathCacheInterface &pathCache)
{
    Utils::PathString specificsPath;

    for (const NodeMetaInfo &prototype : prototypes) {
        auto sourceId = prototype.propertyEditorPathId();
        if (sourceId) {
            auto path = pathCache.sourcePath(sourceId);
            if (path.endsWith("Specifics.qml")) {
                specificsPath = path;
                break;
            }
        }
    }

    return QUrl::fromLocalFile(specificsPath.toQString());
}
#endif

} // namespace

// from material editor to model
void MaterialEditorView::changeValue(const QString &name)
{
    PropertyName propertyName = name.toUtf8();

    if (propertyName.isNull() || locked() || noValidSelection() || propertyName == "id"
        || propertyName == Constants::PROPERTY_EDITOR_CLASSNAME_PROPERTY) {
        return;
    }

    if (propertyName == "objectName") {
        renameMaterial(m_selectedMaterial, m_qmlBackEnd->propertyValueForName("objectName")->value().toString());
        return;
    }

    PropertyName underscoreName(propertyName);
    underscoreName.replace('.', '_');
    PropertyEditorValue *value = m_qmlBackEnd->propertyValueForName(QString::fromLatin1(underscoreName));

    if (!value)
        return;

    if (propertyName.endsWith("__AUX")) {
        commitAuxValueToModel(propertyName, value->value());
        return;
    }

    const NodeMetaInfo metaInfo = m_selectedMaterial.metaInfo();

    QVariant castedValue;

    if (auto property = metaInfo.property(propertyName)) {
        castedValue = property.castedValue(value->value());
    } else {
        qWarning() << __FUNCTION__ << propertyName << "cannot be casted (metainfo)";
        return;
    }

    if (value->value().isValid() && !castedValue.isValid()) {
        qWarning() << __FUNCTION__ << propertyName << "not properly casted (metainfo)";
        return;
    }

    bool propertyTypeUrl = false;

    if (auto property = metaInfo.property(propertyName)) {
        if (property.propertyType().isUrl()) {
            // turn absolute local file paths into relative paths
            propertyTypeUrl = true;
            QString filePath = castedValue.toUrl().toString();
            QFileInfo fi(filePath);
            if (fi.exists() && fi.isAbsolute()) {
                QDir fileDir(QFileInfo(model()->fileUrl().toLocalFile()).absolutePath());
                castedValue = QUrl(fileDir.relativeFilePath(filePath));
            }
        }
    }

    if (name == "state" && castedValue.toString() == "base state")
        castedValue = "";

    if (castedValue.typeId() == QMetaType::QColor) {
        QColor color = castedValue.value<QColor>();
        QColor newColor = QColor(color.name());
        newColor.setAlpha(color.alpha());
        castedValue = QVariant(newColor);
    }

    if (!value->value().isValid() || (propertyTypeUrl && value->value().toString().isEmpty())) { // reset
        removePropertyFromModel(propertyName);
    } else {
        // QVector*D(0, 0, 0) detects as null variant though it is valid value
        if (castedValue.isValid()
            && (!castedValue.isNull() || castedValue.typeId() == QMetaType::QVector2D
                || castedValue.typeId() == QMetaType::QVector3D
                || castedValue.typeId() == QMetaType::QVector4D)) {
            commitVariantValueToModel(propertyName, castedValue);
        }
    }

    requestPreviewRender();
}

static bool isTrueFalseLiteral(const QString &expression)
{
    return (expression.compare("false", Qt::CaseInsensitive) == 0)
           || (expression.compare("true", Qt::CaseInsensitive) == 0);
}

void MaterialEditorView::setupCurrentQmlBackend(const ModelNode &selectedNode,
                                                const QUrl &qmlSpecificsFile,
                                                const QString &specificQmlData)
{

    QmlModelState currState = currentStateNode();
    QString currentStateName = currState.isBaseState() ? QStringLiteral("invalid state")
                                                       : currState.name();

    QmlObjectNode qmlObjectNode{selectedNode};
    if (specificQmlData.isEmpty())
        m_qmlBackEnd->contextObject()->setSpecificQmlData(specificQmlData);
    m_qmlBackEnd->setup(qmlObjectNode, currentStateName, qmlSpecificsFile, this);
    m_qmlBackEnd->contextObject()->setSpecificQmlData(specificQmlData);
}

void MaterialEditorView::setupWidget()
{
    m_qmlBackEnd->widget()->installEventFilter(this);
    m_stackedWidget->setCurrentWidget(m_qmlBackEnd->widget());
    m_stackedWidget->setMinimumSize({400, 300});
}

void MaterialEditorView::changeExpression(const QString &propertyName)
{
    PropertyName name = propertyName.toUtf8();

    if (name.isNull() || locked() || noValidSelection())
        return;

    executeInTransaction(__FUNCTION__, [this, name] {
        PropertyName underscoreName(name);
        underscoreName.replace('.', '_');

        QmlObjectNode qmlObjectNode(m_selectedMaterial);
        PropertyEditorValue *value = m_qmlBackEnd->propertyValueForName(QString::fromLatin1(underscoreName));

        if (!value) {
            qWarning() << __FUNCTION__ << "no value for " << underscoreName;
            return;
        }

        if (auto property = m_selectedMaterial.metaInfo().property(name)) {
            auto propertyType = property.propertyType();
            if (propertyType.isColor()) {
                if (QColor(value->expression().remove('"')).isValid()) {
                    qmlObjectNode.setVariantProperty(name, QColor(value->expression().remove('"')));
                    return;
                }
            } else if (propertyType.isBool()) {
                if (isTrueFalseLiteral(value->expression())) {
                    if (value->expression().compare("true", Qt::CaseInsensitive) == 0)
                        qmlObjectNode.setVariantProperty(name, true);
                    else
                        qmlObjectNode.setVariantProperty(name, false);
                    return;
                }
            } else if (propertyType.isInteger()) {
                bool ok;
                int intValue = value->expression().toInt(&ok);
                if (ok) {
                    qmlObjectNode.setVariantProperty(name, intValue);
                    return;
                }
            } else if (propertyType.isFloat()) {
                bool ok;
                qreal realValue = value->expression().toDouble(&ok);
                if (ok) {
                    qmlObjectNode.setVariantProperty(name, realValue);
                    return;
                }
            } else if (propertyType.isVariant()) {
                bool ok;
                qreal realValue = value->expression().toDouble(&ok);
                if (ok) {
                    qmlObjectNode.setVariantProperty(name, realValue);
                    return;
                } else if (isTrueFalseLiteral(value->expression())) {
                    if (value->expression().compare("true", Qt::CaseInsensitive) == 0)
                        qmlObjectNode.setVariantProperty(name, true);
                    else
                        qmlObjectNode.setVariantProperty(name, false);
                    return;
                }
            }
        }

        if (value->expression().isEmpty()) {
            value->resetValue();
            return;
        }

        if (qmlObjectNode.expression(name) != value->expression() || !qmlObjectNode.propertyAffectedByCurrentState(name))
            qmlObjectNode.setBindingProperty(name, value->expression());

        requestPreviewRender();
    }); // end of transaction
}

void MaterialEditorView::exportPropertyAsAlias(const QString &name)
{
    if (name.isNull() || locked() || noValidSelection())
        return;

    executeInTransaction(__FUNCTION__, [this, name] {
        const QString id = m_selectedMaterial.validId();
        QString upperCasePropertyName = name;
        upperCasePropertyName.replace(0, 1, upperCasePropertyName.at(0).toUpper());
        QString aliasName = id + upperCasePropertyName;
        aliasName.replace(".", ""); //remove all dots

        PropertyName propertyName = aliasName.toUtf8();
        if (rootModelNode().hasProperty(propertyName)) {
            Core::AsynchronousMessageBox::warning(tr("Cannot Export Property as Alias"),
                                                  tr("Property %1 does already exist for root component.").arg(aliasName));
            return;
        }
        rootModelNode().bindingProperty(propertyName).setDynamicTypeNameAndExpression("alias", id + "." + name);
    });
}

void MaterialEditorView::removeAliasExport(const QString &name)
{
    if (name.isNull() || locked() || noValidSelection())
        return;

    executeInTransaction(__FUNCTION__, [this, name] {
        const QString id = m_selectedMaterial.validId();

        const QList<BindingProperty> bindingProps = rootModelNode().bindingProperties();
        for (const BindingProperty &property : bindingProps) {
            if (property.expression() == (id + "." + name)) {
                rootModelNode().removeProperty(property.name());
                break;
            }
        }
    });
}

bool MaterialEditorView::locked() const
{
    return m_locked;
}

void MaterialEditorView::currentTimelineChanged(const ModelNode &)
{
    if (m_qmlBackEnd)
        m_qmlBackEnd->contextObject()->setHasActiveTimeline(QmlTimeline::hasActiveTimeline(this));
}

void MaterialEditorView::refreshMetaInfos(const TypeIds &deletedTypeIds)
{
    m_propertyComponentGenerator.refreshMetaInfos(deletedTypeIds);
}

DynamicPropertiesModel *MaterialEditorView::dynamicPropertiesModel() const
{
    return m_dynamicPropertiesModel;
}

MaterialEditorView *MaterialEditorView::instance()
{
    static MaterialEditorView *s_instance = nullptr;

    if (s_instance)
        return s_instance;

    const auto views = QmlDesignerPlugin::instance()->viewManager().views();
    for (auto *view : views) {
        MaterialEditorView *myView = qobject_cast<MaterialEditorView *>(view);
        if (myView)
            s_instance =  myView;
    }

    QTC_ASSERT(s_instance, return nullptr);
    return s_instance;
}

void MaterialEditorView::timerEvent(QTimerEvent *timerEvent)
{
    if (m_timerId == timerEvent->timerId()) {
        if (m_selectedMaterialChanged) {
            m_selectedMaterialChanged = false;
            Utils3D::selectMaterial(m_newSelectedMaterial);
            m_newSelectedMaterial = {};
        } else {
            resetView();
        }
    }
}

void MaterialEditorView::asyncResetView()
{
    if (m_timerId)
        killTimer(m_timerId);
    m_timerId = startTimer(0);
}

void MaterialEditorView::resetView()
{
    if (!model())
        return;

    m_locked = true;

    if (m_timerId)
        killTimer(m_timerId);

    setupQmlBackend();

    if (m_qmlBackEnd) {
        m_qmlBackEnd->emitSelectionChanged();
        updatePossibleTypes();
    }

    QTimer::singleShot(0, this, &MaterialEditorView::requestPreviewRender);

    m_locked = false;

    if (m_timerId)
        m_timerId = 0;
}

void MaterialEditorView::handleToolBarAction(int action)
{
    QTC_ASSERT(m_hasQuick3DImport, return);

    switch (action) {
    case MaterialEditorContextObject::ApplyToSelected: {
        Utils3D::applyMaterialToModels(this, m_selectedMaterial, Utils3D::getSelectedModels(this));
        break;
    }

    case MaterialEditorContextObject::ApplyToSelectedAdd: {
        Utils3D::applyMaterialToModels(this, m_selectedMaterial, Utils3D::getSelectedModels(this), true);
        break;
    }

    case MaterialEditorContextObject::AddNewMaterial: {
        if (!model())
            break;
        ModelNode newMatNode;
        executeInTransaction(__FUNCTION__, [&] {
            ModelNode matLib = Utils3D::materialLibraryNode(this);
            if (!matLib.isValid())
                return;
#ifdef QDS_USE_PROJECTSTORAGE
            ModelNode newMatNode = createModelNode("PrincipledMaterial");
#else
            NodeMetaInfo metaInfo = model()->qtQuick3DPrincipledMaterialMetaInfo();
            newMatNode = createModelNode("QtQuick3D.PrincipledMaterial",
                                                   metaInfo.majorVersion(),
                                                   metaInfo.minorVersion());
#endif
            renameMaterial(newMatNode, "New Material");
            matLib.defaultNodeListProperty().reparentHere(newMatNode);
        });
        QTimer::singleShot(0, this, [newMatNode]() {
            Utils3D::selectMaterial(newMatNode);
        });
        break;
    }

    case MaterialEditorContextObject::DeleteCurrentMaterial: {
        if (m_selectedMaterial.isValid()) {
            executeInTransaction(__FUNCTION__, [&] {
                m_selectedMaterial.destroy();
            });
        }
        break;
    }

    case MaterialEditorContextObject::OpenMaterialBrowser: {
        QmlDesignerPlugin::instance()->mainWidget()->showDockWidget("MaterialBrowser", true);
        break;
    }
    }
}

void MaterialEditorView::handlePreviewEnvChanged(const QString &envAndValue)
{
    if (envAndValue.isEmpty() || m_initializingPreviewData)
        return;

    QTC_ASSERT(m_hasQuick3DImport, return);
    QTC_ASSERT(model(), return);
    QTC_ASSERT(model()->nodeInstanceView(), return);

    QStringList parts = envAndValue.split('=');
    QString env = parts[0];
    QString value;
    if (parts.size() > 1)
        value = parts[1];

    auto renderPreviews = [this](const QString &auxEnv, const QString &auxValue) {
        rootModelNode().setAuxiliaryData(materialPreviewEnvDocProperty, auxEnv);
        rootModelNode().setAuxiliaryData(materialPreviewEnvProperty, auxEnv);
        rootModelNode().setAuxiliaryData(materialPreviewEnvValueDocProperty, auxValue);
        rootModelNode().setAuxiliaryData(materialPreviewEnvValueProperty, auxValue);
        QTimer::singleShot(0, this, &MaterialEditorView::requestPreviewRender);
        emitCustomNotification("refresh_material_browser", {});
    };

    if (env == "Color") {
        auto oldColorPropVal = rootModelNode().auxiliaryData(materialPreviewColorDocProperty);
        QString oldColor = oldColorPropVal ? oldColorPropVal->toString() : "";

        if (value.isEmpty())
            value = oldColor;
        else
            rootModelNode().setAuxiliaryData(materialPreviewColorDocProperty, value);
    }

    renderPreviews(env, value);
}

void MaterialEditorView::handlePreviewModelChanged(const QString &modelStr)
{
    if (modelStr.isEmpty() || m_initializingPreviewData)
        return;

    QTC_ASSERT(m_hasQuick3DImport, return);
    QTC_ASSERT(model(), return);
    QTC_ASSERT(model()->nodeInstanceView(), return);

    rootModelNode().setAuxiliaryData(materialPreviewModelDocProperty, modelStr);
    rootModelNode().setAuxiliaryData(materialPreviewModelProperty, modelStr);

    QTimer::singleShot(0, this, &MaterialEditorView::requestPreviewRender);
    emitCustomNotification("refresh_material_browser", {});
}

void MaterialEditorView::handlePreviewSizeChanged(const QSizeF &size)
{
    if (m_previewSize == size.toSize())
        return;

    m_previewSize = size.toSize();
    requestPreviewRender();
}

MaterialEditorQmlBackend *MaterialEditorView::getQmlBackend(const QUrl &qmlFileUrl)
{
    auto qmlFileName = qmlFileUrl.toString();
    MaterialEditorQmlBackend *currentQmlBackend = m_qmlBackendHash.value(qmlFileName);

    if (!currentQmlBackend) {
        currentQmlBackend = new MaterialEditorQmlBackend(this);

        m_stackedWidget->addWidget(currentQmlBackend->widget());
        m_qmlBackendHash.insert(qmlFileName, currentQmlBackend);

        currentQmlBackend->setSource(qmlFileUrl);

        QObject *rootObj = currentQmlBackend->widget()->rootObject();
        connect(rootObj, SIGNAL(toolBarAction(int)), this, SLOT(handleToolBarAction(int)));
        connect(rootObj, SIGNAL(previewEnvChanged(QString)), this, SLOT(handlePreviewEnvChanged(QString)));
        connect(rootObj, SIGNAL(previewModelChanged(QString)), this, SLOT(handlePreviewModelChanged(QString)));
    }

    return currentQmlBackend;
}

void MaterialEditorView::setupQmlBackend()
{
    QUrl qmlPaneUrl = getPaneUrl();
    QUrl qmlSpecificsUrl;
    QString specificQmlData;

#ifdef QDS_USE_PROJECTSTORAGE
    auto selfAndPrototypes = m_selectedMaterial.metaInfo().selfAndPrototypes();
    bool isEditableComponent = m_selectedMaterial.isComponent()
                               && !QmlItemNode(m_selectedMaterial).isEffectItem();
    specificQmlData = m_propertyEditorComponentGenerator.create(selfAndPrototypes, isEditableComponent);
    qmlSpecificsUrl = getSpecificsUrl(selfAndPrototypes, model()->pathCache());
#else
    TypeName diffClassName;
    if (NodeMetaInfo metaInfo = m_selectedMaterial.metaInfo()) {
        diffClassName = metaInfo.typeName();
        for (const NodeMetaInfo &metaInfo : metaInfo.selfAndPrototypes()) {
           if (PropertyEditorQmlBackend::checkIfUrlExists(qmlSpecificsUrl))
               break;
            qmlSpecificsUrl = PropertyEditorQmlBackend::getQmlFileUrl(metaInfo.typeName() + "Specifics", metaInfo);
            diffClassName = metaInfo.typeName();
        }

        if (diffClassName != m_selectedMaterial.type()) {
            specificQmlData = PropertyEditorQmlBackend::templateGeneration(
                                metaInfo, model()->metaInfo(diffClassName), m_selectedMaterial);
        }
    }
#endif

    m_qmlBackEnd = getQmlBackend(qmlPaneUrl);
    setupCurrentQmlBackend(m_selectedMaterial, qmlSpecificsUrl, specificQmlData);

    setupWidget();

    m_qmlBackEnd->contextObject()->setHasQuick3DImport(m_hasQuick3DImport);
    m_qmlBackEnd->contextObject()->setHasMaterialLibrary(
        Utils3D::materialLibraryNode(this).isValid());
    m_qmlBackEnd->contextObject()->setIsQt6Project(externalDependencies().isQt6Project());

    m_dynamicPropertiesModel->setSelectedNode(m_selectedMaterial);

    initPreviewData();
    QString matType = QString::fromLatin1(m_selectedMaterial.type());
    m_qmlBackEnd->contextObject()->setCurrentType(matType);
}

void MaterialEditorView::commitVariantValueToModel(PropertyNameView propertyName, const QVariant &value)
{
    m_locked = true;
    executeInTransaction(__FUNCTION__, [&] {
        QmlObjectNode(m_selectedMaterial).setVariantProperty(propertyName, value);
    });
    m_locked = false;
}

void MaterialEditorView::commitAuxValueToModel(PropertyNameView propertyName, const QVariant &value)
{
    m_locked = true;

    PropertyNameView name = propertyName;
    name.chop(5);

    try {
        if (value.isValid())
            m_selectedMaterial.setAuxiliaryData(AuxiliaryDataType::Document, name, value);
        else
            m_selectedMaterial.removeAuxiliaryData(AuxiliaryDataType::Document, name);
    }
    catch (const Exception &e) {
        e.showException();
    }
    m_locked = false;
}

void MaterialEditorView::removePropertyFromModel(PropertyNameView propertyName)
{
    m_locked = true;
    executeInTransaction(__FUNCTION__, [&] {
        QmlObjectNode(m_selectedMaterial).removeProperty(propertyName);
    });
    m_locked = false;
}

bool MaterialEditorView::noValidSelection() const
{
    QTC_ASSERT(m_qmlBackEnd, return true);
    return !QmlObjectNode::isValidQmlObjectNode(m_selectedMaterial);
}

void MaterialEditorView::initPreviewData()
{
    if (model() && m_qmlBackEnd) {
        auto envPropVal = rootModelNode().auxiliaryData(materialPreviewEnvDocProperty);
        auto envValuePropVal = rootModelNode().auxiliaryData(materialPreviewEnvValueDocProperty);
        auto modelStrPropVal = rootModelNode().auxiliaryData(materialPreviewModelDocProperty);
        QString env = envPropVal ? envPropVal->toString() : "";
        QString envValue = envValuePropVal ? envValuePropVal->toString() : "";
        QString modelStr = modelStrPropVal ? modelStrPropVal->toString() : "";
        // Initialize corresponding instance aux values used by puppet
        QTimer::singleShot(0, this, [this, env, envValue, modelStr]() {
            if (model()) {
                rootModelNode().setAuxiliaryData(materialPreviewEnvProperty, env);
                rootModelNode().setAuxiliaryData(materialPreviewEnvValueProperty, envValue);
                rootModelNode().setAuxiliaryData(materialPreviewModelProperty, modelStr);
            }
        });

        if (!envValue.isEmpty() && env != "Basic") {
            env += '=';
            env += envValue;
        }
        if (env.isEmpty())
            env = "SkyBox=preview_studio";
        if (modelStr.isEmpty())
            modelStr = "#Sphere";
        m_initializingPreviewData = true;
        QMetaObject::invokeMethod(m_qmlBackEnd->widget()->rootObject(),
                                  "initPreviewData",
                                  Q_ARG(QVariant, env), Q_ARG(QVariant, modelStr));
        m_initializingPreviewData = false;
    }
}

void MaterialEditorView::updatePossibleTypes()
{
    QTC_ASSERT(model(), return);

    if (!m_qmlBackEnd)
        return;

    static const QStringList basicTypes{
        "CustomMaterial",
        "DefaultMaterial",
        "PrincipledMaterial",
        "SpecularGlossyMaterial",
    };

    const QString matType = m_selectedMaterial.simplifiedTypeName();

    if (basicTypes.contains(matType)) {
        m_qmlBackEnd->contextObject()->setPossibleTypes(basicTypes);
        return;
    }

    m_qmlBackEnd->contextObject()->setPossibleTypes({matType});
}

void MaterialEditorView::modelAttached(Model *model)
{
    AbstractView::modelAttached(model);

    if constexpr (useProjectStorage())
        m_propertyComponentGenerator.setModel(model);

    m_locked = true;

    m_hasQuick3DImport = model->hasImport("QtQuick3D");
    m_hasMaterialRoot = rootModelNode().metaInfo().isQtQuick3DMaterial();

    if (m_hasMaterialRoot)
        m_selectedMaterial = rootModelNode();
    else if (m_hasQuick3DImport)
        m_selectedMaterial = Utils3D::selectedMaterial(this);

    if (!m_setupCompleted) {
        reloadQml();
        m_setupCompleted = true;
    }
    resetView();

    selectedNodesChanged(selectedModelNodes(), {});

    m_locked = false;
}

void MaterialEditorView::modelAboutToBeDetached(Model *model)
{
    AbstractView::modelAboutToBeDetached(model);
    m_dynamicPropertiesModel->reset();
    if (m_qmlBackEnd) {
        if (auto transaction = m_qmlBackEnd->materialEditorTransaction())
            transaction->end();
        m_qmlBackEnd->contextObject()->setHasMaterialLibrary(false);
    }
    m_selectedMaterial = {};
}

void MaterialEditorView::propertiesRemoved(const QList<AbstractProperty> &propertyList)
{
    if (noValidSelection())
        return;

    bool changed = false;
    for (const AbstractProperty &property : propertyList) {
        ModelNode node(property.parentModelNode());

        if (node.isRootNode())
            m_qmlBackEnd->contextObject()->setHasAliasExport(QmlObjectNode(m_selectedMaterial).isAliasExported());

        if (node == m_selectedMaterial || QmlObjectNode(m_selectedMaterial).propertyChangeForCurrentState() == node) {
            m_locked = true;

            const PropertyName propertyName = property.name().toByteArray();
            PropertyName convertedpropertyName = propertyName;

            convertedpropertyName.replace('.', '_');

            PropertyEditorValue *value = m_qmlBackEnd->propertyValueForName(
                QString::fromUtf8(convertedpropertyName));

            if (value) {
                value->resetValue();
                m_qmlBackEnd
                    ->setValue(m_selectedMaterial,
                               propertyName,
                               QmlObjectNode(m_selectedMaterial).instanceValue(propertyName));
            }
            m_locked = false;
            changed = true;
        }

        dynamicPropertiesModel()->dispatchPropertyChanges(property);
    }
    if (changed)
        requestPreviewRender();
}

void MaterialEditorView::variantPropertiesChanged(const QList<VariantProperty> &propertyList, PropertyChangeFlags /*propertyChange*/)
{
    if (noValidSelection())
        return;

    bool changed = false;
    for (const VariantProperty &property : propertyList) {
        ModelNode node(property.parentModelNode());
        if (node == m_selectedMaterial || QmlObjectNode(m_selectedMaterial).propertyChangeForCurrentState() == node) {
            if (property.isDynamic())
                m_dynamicPropertiesModel->updateItem(property);
            if (m_selectedMaterial.property(property.name()).isBindingProperty())
                setValue(m_selectedMaterial, property.name(), QmlObjectNode(m_selectedMaterial).instanceValue(property.name()));
            else
                setValue(m_selectedMaterial, property.name(), QmlObjectNode(m_selectedMaterial).modelValue(property.name()));

            changed = true;
        }

        if (!changed && node.metaInfo().isQtQuick3DTexture()
            && m_selectedMaterial.bindingProperties().size() > 0) {
            // update preview when editing texture properties if the material has binding properties
            changed = true;
        }

        dynamicPropertiesModel()->dispatchPropertyChanges(property);
    }
    if (changed)
        requestPreviewRender();
}

void MaterialEditorView::bindingPropertiesChanged(const QList<BindingProperty> &propertyList, PropertyChangeFlags /*propertyChange*/)
{
    if (noValidSelection())
        return;

    bool changed = false;
    for (const BindingProperty &property : propertyList) {
        ModelNode node(property.parentModelNode());

        if (property.isAliasExport())
            m_qmlBackEnd->contextObject()->setHasAliasExport(QmlObjectNode(m_selectedMaterial).isAliasExported());

        if (node == m_selectedMaterial || QmlObjectNode(m_selectedMaterial).propertyChangeForCurrentState() == node) {
            if (property.isDynamic())
                m_dynamicPropertiesModel->updateItem(property);
            m_locked = true;
            QString exp = QmlObjectNode(m_selectedMaterial).bindingProperty(property.name()).expression();
            m_qmlBackEnd->setExpression(property.name(), exp);
            m_locked = false;
            changed = true;
        }

        dynamicPropertiesModel()->dispatchPropertyChanges(property);
    }
    if (changed)
        requestPreviewRender();
}

void MaterialEditorView::auxiliaryDataChanged(const ModelNode &node,
                                              AuxiliaryDataKeyView key,
                                              const QVariant &)
{
    if (!noValidSelection() && node.isSelected())
        m_qmlBackEnd->setValueforAuxiliaryProperties(m_selectedMaterial, key);

    if (!m_hasMaterialRoot) {
        if (key == Utils3D::matLibSelectedMaterialProperty) {
            if (ModelNode selNode = Utils3D::selectedMaterial(this)) {
                m_selectedMaterial = selNode;
                m_dynamicPropertiesModel->setSelectedNode(m_selectedMaterial);
                asyncResetView();
            }
        } else if (isPreviewAuxiliaryKey(key)) {
            QTimer::singleShot(0, this, &MaterialEditorView::initPreviewData);
        }
    }
}

void MaterialEditorView::propertiesAboutToBeRemoved(const QList<AbstractProperty> &propertyList)
{
    for (const auto &property : propertyList)
        m_dynamicPropertiesModel->removeItem(property);
}

// request render image for the selected material node
void MaterialEditorView::requestPreviewRender()
{
    if (model() && model()->nodeInstanceView() && m_selectedMaterial.isValid()) {
        static int requestId = 0;
        m_previewRequestId = QByteArray(MATERIAL_EDITOR_IMAGE_REQUEST_ID)
                             + QByteArray::number(++requestId);

        model()->sendCustomNotificationToNodeInstanceView(
            NodePreviewImage{m_selectedMaterial, {}, m_previewSize, m_previewRequestId});
    }
}

bool MaterialEditorView::hasWidget() const
{
    return true;
}

WidgetInfo MaterialEditorView::widgetInfo()
{
    return createWidgetInfo(m_stackedWidget,
                            "MaterialEditor",
                            WidgetInfo::RightPane,
                            tr("Material Editor"),
                            tr("Material Editor view"));
}

void MaterialEditorView::selectedNodesChanged([[maybe_unused]] const QList<ModelNode> &selectedNodeList,
                                              [[maybe_unused]] const QList<ModelNode> &lastSelectedNodeList)
{
    if (m_qmlBackEnd)
        m_qmlBackEnd->contextObject()->setHasModelSelection(!Utils3D::getSelectedModels(this).isEmpty());
}

void MaterialEditorView::currentStateChanged(const ModelNode &node)
{
    QmlModelState newQmlModelState(node);
    Q_ASSERT(newQmlModelState.isValid());

    resetView();
}

void MaterialEditorView::instancePropertyChanged(const QList<QPair<ModelNode, PropertyName> > &propertyList)
{
    if (!m_selectedMaterial.isValid() || !m_qmlBackEnd)
        return;

    m_locked = true;

    bool changed = false;
    for (const QPair<ModelNode, PropertyName> &propertyPair : propertyList) {
        const ModelNode modelNode = propertyPair.first;
        const QmlObjectNode qmlObjectNode(modelNode);
        const PropertyName propertyName = propertyPair.second;

        if (qmlObjectNode.isValid() && modelNode == m_selectedMaterial && qmlObjectNode.currentState().isValid()) {
            const AbstractProperty property = modelNode.property(propertyName);
            if (!modelNode.hasProperty(propertyName) || modelNode.property(property.name()).isBindingProperty())
                setValue(modelNode, property.name(), qmlObjectNode.instanceValue(property.name()));
            else
                setValue(modelNode, property.name(), qmlObjectNode.modelValue(property.name()));
            changed = true;
        }
    }
    if (changed)
        requestPreviewRender();

    m_locked = false;
}

void MaterialEditorView::nodeTypeChanged(const ModelNode &node, const TypeName &typeName, int, int)
{
     if (node == m_selectedMaterial) {
         m_qmlBackEnd->contextObject()->setCurrentType(QString::fromLatin1(typeName));
         resetView();
     }
}

void MaterialEditorView::rootNodeTypeChanged(const QString &type, int, int)
{
    if (rootModelNode() == m_selectedMaterial) {
        m_qmlBackEnd->contextObject()->setCurrentType(type);
        resetView();
    }
}

void MaterialEditorView::modelNodePreviewPixmapChanged(const ModelNode &node,
                                                       const QPixmap &pixmap,
                                                       const QByteArray &requestId)
{
    if (!m_qmlBackEnd || node != m_selectedMaterial || requestId != m_previewRequestId)
        return;

    m_qmlBackEnd->updateMaterialPreview(pixmap);
}

void MaterialEditorView::importsChanged([[maybe_unused]] const Imports &addedImports,
                                        [[maybe_unused]] const Imports &removedImports)
{
    m_hasQuick3DImport = model()->hasImport("QtQuick3D");
    if (m_qmlBackEnd)
        m_qmlBackEnd->contextObject()->setHasQuick3DImport(m_hasQuick3DImport);

    resetView();
}

void MaterialEditorView::renameMaterial(ModelNode &material, const QString &newName)
{
    QTC_ASSERT(material.isValid(), return);
    QmlObjectNode(material).setNameAndId(newName, "material");
}

void MaterialEditorView::duplicateMaterial(const ModelNode &material)
{
    QTC_ASSERT(material.isValid() && model(), return);

    TypeName matType = material.type();
    QmlObjectNode sourceMat(material);
    ModelNode duplicateMatNode;
    QList<AbstractProperty> dynamicProps;

    executeInTransaction(__FUNCTION__, [&] {
        ModelNode matLib = Utils3D::materialLibraryNode(this);
        QTC_ASSERT(matLib.isValid(), return);

        // create the duplicate material
#ifdef QDS_USE_PROJECTSTORAGE
        QmlObjectNode duplicateMat = createModelNode(matType);
#else
        NodeMetaInfo metaInfo = model()->metaInfo(matType);
        QmlObjectNode duplicateMat = createModelNode(matType, metaInfo.majorVersion(), metaInfo.minorVersion());
#endif
        duplicateMatNode = duplicateMat.modelNode();

        // generate and set a unique name
        QString newName = sourceMat.modelNode().variantProperty("objectName").value().toString();
        if (!newName.contains("copy", Qt::CaseInsensitive))
            newName.append(" copy");

        const QList<ModelNode> mats = matLib.directSubModelNodesOfType(model()->qtQuick3DMaterialMetaInfo());
        QStringList matNames;
        for (const ModelNode &mat : mats)
            matNames.append(mat.variantProperty("objectName").value().toString());

        newName = UniqueName::generate(newName, [&] (const QString &name) {
            return matNames.contains(name);
        });

        VariantProperty objNameProp = duplicateMatNode.variantProperty("objectName");
        objNameProp.setValue(newName);

        // generate and set an id
        duplicateMatNode.setIdWithoutRefactoring(model()->generateNewId(newName, "material"));

        // sync properties. Only the base state is duplicated.
        const QList<AbstractProperty> props = material.properties();
        for (const AbstractProperty &prop : props) {
            if (prop.name() == "objectName" || prop.name() == "data")
                continue;

            if (prop.isVariantProperty()) {
                if (prop.isDynamic()) {
                    dynamicProps.append(prop);
                } else {
                    VariantProperty variantProp = duplicateMatNode.variantProperty(prop.name());
                    variantProp.setValue(prop.toVariantProperty().value());
                }
            } else if (prop.isBindingProperty()) {
                if (prop.isDynamic()) {
                    dynamicProps.append(prop);
                } else {
                    BindingProperty bindingProp = duplicateMatNode.bindingProperty(prop.name());
                    bindingProp.setExpression(prop.toBindingProperty().expression());
                }
            }
        }

        matLib.defaultNodeListProperty().reparentHere(duplicateMat);
    });

    // For some reason, creating dynamic properties in the same transaction doesn't work, so
    // let's do it in separate transaction.
    // TODO: Fix the issue and merge transactions (QDS-8094)
    if (!dynamicProps.isEmpty()) {
        executeInTransaction(__FUNCTION__, [&] {
            for (const AbstractProperty &prop : std::as_const(dynamicProps)) {
                if (prop.isVariantProperty()) {
                    VariantProperty variantProp = duplicateMatNode.variantProperty(prop.name());
                    variantProp.setDynamicTypeNameAndValue(prop.dynamicTypeName(),
                                                           prop.toVariantProperty().value());
                } else if (prop.isBindingProperty()) {
                    BindingProperty bindingProp = duplicateMatNode.bindingProperty(prop.name());
                    bindingProp.setDynamicTypeNameAndExpression(prop.dynamicTypeName(),
                                                                prop.toBindingProperty().expression());
                }
            }
        });
    }
}

void MaterialEditorView::customNotification([[maybe_unused]] const AbstractView *view,
                                            const QString &identifier,
                                            const QList<ModelNode> &nodeList,
                                            const QList<QVariant> &data)
{
    if (identifier == "rename_material")
        renameMaterial(m_selectedMaterial, data.first().toString());
    else if (identifier == "add_new_material")
        handleToolBarAction(MaterialEditorContextObject::AddNewMaterial);
    else if (identifier == "duplicate_material")
        duplicateMaterial(nodeList.first());
}

void MaterialEditorView::nodeReparented(const ModelNode &node,
                                        [[maybe_unused]] const NodeAbstractProperty &newPropertyParent,
                                        [[maybe_unused]] const NodeAbstractProperty &oldPropertyParent,
                                        [[maybe_unused]] PropertyChangeFlags propertyChange)
{
    if (node.id() == Constants::MATERIAL_LIB_ID && m_qmlBackEnd && m_qmlBackEnd->contextObject()) {
        m_qmlBackEnd->contextObject()->setHasMaterialLibrary(true);
        asyncResetView();
    } else {
        if (!m_selectedMaterial && node.metaInfo().isQtQuick3DMaterial()
            && node.parentProperty().parentModelNode() == Utils3D::materialLibraryNode(this)) {
            ModelNode currentSelection = Utils3D::selectedMaterial(this);
            if (currentSelection) {
                m_selectedMaterial = currentSelection;
                asyncResetView();
            } else {
                QTimer::singleShot(0, this, [node]() {
                    Utils3D::selectMaterial(node);
                });
            }
        }

        if (m_qmlBackEnd && containsTexture(node))
            m_qmlBackEnd->refreshBackendModel();
    }
}

void MaterialEditorView::nodeIdChanged(const ModelNode &node,
                                       [[maybe_unused]] const QString &newId,
                                       [[maybe_unused]] const QString &oldId)
{
    if (m_qmlBackEnd && node.metaInfo().isQtQuick3DTexture())
        m_qmlBackEnd->refreshBackendModel();
}

void MaterialEditorView::nodeAboutToBeRemoved(const ModelNode &removedNode)
{
    if (removedNode.id() == Constants::MATERIAL_LIB_ID && m_qmlBackEnd && m_qmlBackEnd->contextObject()) {
        m_selectedMaterial = {};
        m_qmlBackEnd->contextObject()->setHasMaterialLibrary(false);
        asyncResetView();
    } else {
        if (removedNode == m_selectedMaterial) {
            ModelNode matLib = Utils3D::materialLibraryNode(this);
            QTC_ASSERT(matLib.isValid(), return);

            const QList<ModelNode> mats = matLib.directSubModelNodesOfType(
                model()->qtQuick3DMaterialMetaInfo());
            bool selectedNodeFound = false;
            m_newSelectedMaterial = {};
            for (const ModelNode &mat : mats) {
                if (selectedNodeFound) {
                    m_newSelectedMaterial = mat;
                    break;
                }
                if (m_selectedMaterial == mat)
                    selectedNodeFound = true;
                else
                    m_newSelectedMaterial = mat;
            }
            m_selectedMaterialChanged = true;
        }

        if (containsTexture(removedNode))
            m_textureAboutToBeRemoved = true;
    }
}

void MaterialEditorView::nodeRemoved([[maybe_unused]] const ModelNode &removedNode,
                                     [[maybe_unused]] const NodeAbstractProperty &parentProperty,
                                     [[maybe_unused]] PropertyChangeFlags propertyChange)
{
    if (m_qmlBackEnd && m_textureAboutToBeRemoved)
        m_qmlBackEnd->refreshBackendModel();

    m_textureAboutToBeRemoved = false;

    if (m_selectedMaterialChanged)
        asyncResetView();
}

void MaterialEditorView::highlightSupportedProperties(bool highlight)
{
    if (!m_selectedMaterial.isValid())
        return;

    DesignerPropertyMap &propMap = m_qmlBackEnd->backendValuesPropertyMap();
    const QStringList propNames = propMap.keys();
    NodeMetaInfo metaInfo = m_selectedMaterial.metaInfo();
    QTC_ASSERT(metaInfo.isValid(), return);

    for (const QString &propName : propNames) {
        if (metaInfo.property(propName.toUtf8()).propertyType().isQtQuick3DTexture()) {
            QObject *propEditorValObj = propMap.value(propName).value<QObject *>();
            PropertyEditorValue *propEditorVal = qobject_cast<PropertyEditorValue *>(propEditorValObj);
            propEditorVal->setHasActiveDrag(highlight);
        }
    }
}

void MaterialEditorView::dragStarted(QMimeData *mimeData)
{
    if (mimeData->hasFormat(Constants::MIME_TYPE_ASSETS)) {
        const QString assetPath = QString::fromUtf8(mimeData->data(Constants::MIME_TYPE_ASSETS)).split(',')[0];
        Asset asset(assetPath);

        if (!asset.isValidTextureSource()) // currently only image assets have dnd-supported properties
            return;

        highlightSupportedProperties();
    } else if (mimeData->hasFormat(Constants::MIME_TYPE_TEXTURE)
            || mimeData->hasFormat(Constants::MIME_TYPE_BUNDLE_TEXTURE)) {
        highlightSupportedProperties();
    }
}

void MaterialEditorView::dragEnded()
{
    highlightSupportedProperties(false);
}

// from model to material editor
void MaterialEditorView::setValue(const QmlObjectNode &qmlObjectNode,
                                  PropertyNameView name,
                                  const QVariant &value)
{
    m_locked = true;
    m_qmlBackEnd->setValue(qmlObjectNode, name, value);
    m_locked = false;
}

bool MaterialEditorView::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::FocusOut) {
        if (m_qmlBackEnd && m_qmlBackEnd->widget() == obj)
            QMetaObject::invokeMethod(m_qmlBackEnd->widget()->rootObject(), "closeContextMenu");
    }
    return AbstractView::eventFilter(obj, event);
}

void MaterialEditorView::reloadQml()
{
    m_qmlBackendHash.clear();
    while (QWidget *widget = m_stackedWidget->widget(0)) {
        m_stackedWidget->removeWidget(widget);
        delete widget;
    }
    m_qmlBackEnd = nullptr;

    resetView();
}

} // namespace QmlDesigner
