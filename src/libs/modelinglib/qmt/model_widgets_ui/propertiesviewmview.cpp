// Copyright (C) 2016 Jochen Becher
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "propertiesviewmview.h"

#include "classmembersedit.h"
#include "palettebox.h"

#include "qmt/model_ui/stereotypescontroller.h"
#include "qmt/model_controller/modelcontroller.h"

#include "qmt/model/melement.h"
#include "qmt/model/mobject.h"
#include "qmt/model/mpackage.h"
#include "qmt/model/mclass.h"
#include "qmt/model/mcomponent.h"
#include "qmt/model/mdiagram.h"
#include "qmt/model/mcanvasdiagram.h"
#include "qmt/model/mitem.h"
#include "qmt/model/mrelation.h"
#include "qmt/model/mdependency.h"
#include "qmt/model/minheritance.h"
#include "qmt/model/massociation.h"
#include "qmt/model/mconnection.h"

#include "qmt/diagram/delement.h"
#include "qmt/diagram/dobject.h"
#include "qmt/diagram/dpackage.h"
#include "qmt/diagram/dclass.h"
#include "qmt/diagram/dcomponent.h"
#include "qmt/diagram/ddiagram.h"
#include "qmt/diagram/ditem.h"
#include "qmt/diagram/drelation.h"
#include "qmt/diagram/dinheritance.h"
#include "qmt/diagram/ddependency.h"
#include "qmt/diagram/dassociation.h"
#include "qmt/diagram/dconnection.h"
#include "qmt/diagram/dannotation.h"
#include "qmt/diagram/dboundary.h"
#include "qmt/diagram/dswimlane.h"

// TODO move into better place
#include "qmt/diagram_scene/items/stereotypedisplayvisitor.h"
#include "qmt/stereotype/stereotypecontroller.h"
#include "qmt/stereotype/customrelation.h"
#include "qmt/style/relationvisuals.h"
#include "qmt/style/stylecontroller.h"
#include "qmt/style/style.h"
#include "qmt/style/objectvisuals.h"

#include "../../modelinglibtr.h"

#include <QCoreApplication>
#include <QWidget>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QPainter>

//#define SHOW_DEBUG_PROPERTIES

namespace qmt {

static int translateDirectionToIndex(MDependency::Direction direction)
{
    switch (direction) {
    case MDependency::AToB:
        return 0;
    case MDependency::BToA:
        return 1;
    case MDependency::Bidirectional:
        return 2;
    }
    return 0;
}

static bool isValidDirectionIndex(int index)
{
    return index >= 0 && index <= 2;
}

static MDependency::Direction translateIndexToDirection(int index)
{
    static const MDependency::Direction map[] = {
        MDependency::AToB, MDependency::BToA, MDependency::Bidirectional
    };
    QMT_ASSERT(isValidDirectionIndex(index), return MDependency::AToB);
    return map[index];
}

static int translateAssociationKindToIndex(MAssociationEnd::Kind kind)
{
    switch (kind) {
    case MAssociationEnd::Association:
        return 0;
    case MAssociationEnd::Aggregation:
        return 1;
    case MAssociationEnd::Composition:
        return 2;
    }
    return 0;
}

static bool isValidAssociationKindIndex(int index)
{
    return index >= 0 && index <= 2;
}

static MAssociationEnd::Kind translateIndexToAssociationKind(int index)
{
    static const MAssociationEnd::Kind map[] = {
        MAssociationEnd::Association, MAssociationEnd::Aggregation, MAssociationEnd::Composition
    };
    QMT_ASSERT(isValidAssociationKindIndex(index), return MAssociationEnd::Association);
    return map[index];
}

static int translateVisualPrimaryRoleToIndex(DObject::VisualPrimaryRole visualRole)
{
    switch (visualRole) {
    case DObject::PrimaryRoleNormal:
        return 0;
    case DObject::PrimaryRoleCustom1:
        return 1;
    case DObject::PrimaryRoleCustom2:
        return 2;
    case DObject::PrimaryRoleCustom3:
        return 3;
    case DObject::PrimaryRoleCustom4:
        return 4;
    case DObject::PrimaryRoleCustom5:
        return 5;
    case DObject::DeprecatedPrimaryRoleLighter:
    case DObject::DeprecatedPrimaryRoleDarker:
    case DObject::DeprecatedPrimaryRoleSoften:
    case DObject::DeprecatedPrimaryRoleOutline:
        return 0;
    }
    return 0;
}

static DObject::VisualPrimaryRole translateIndexToVisualPrimaryRole(int index)
{
    static const DObject::VisualPrimaryRole map[] = {
        DObject::PrimaryRoleNormal,
        DObject::PrimaryRoleCustom1, DObject::PrimaryRoleCustom2, DObject::PrimaryRoleCustom3,
        DObject::PrimaryRoleCustom4, DObject::PrimaryRoleCustom5
    };
    QMT_ASSERT(index >= 0 && index <= 5, return DObject::PrimaryRoleNormal);
    return map[index];
}

static int translateVisualSecondaryRoleToIndex(DObject::VisualSecondaryRole visualRole)
{
    switch (visualRole) {
    case DObject::SecondaryRoleNone:
        return 0;
    case DObject::SecondaryRoleLighter:
        return 1;
    case DObject::SecondaryRoleDarker:
        return 2;
    case DObject::SecondaryRoleSoften:
        return 3;
    case DObject::SecondaryRoleOutline:
        return 4;
    case DObject::SecondaryRoleFlat:
        return 5;
    }
    return 0;
}

static DObject::VisualSecondaryRole translateIndexToVisualSecondaryRole(int index)
{
    static const DObject::VisualSecondaryRole map[] = {
        DObject::SecondaryRoleNone,
        DObject::SecondaryRoleLighter, DObject::SecondaryRoleDarker,
        DObject::SecondaryRoleSoften, DObject::SecondaryRoleOutline,
        DObject::SecondaryRoleFlat
    };
    QMT_ASSERT(index >= 0 && index <= 5, return DObject::SecondaryRoleNone);
    return map[index];
}

static int translateStereotypeDisplayToIndex(DObject::StereotypeDisplay stereotypeDisplay)
{
    switch (stereotypeDisplay) {
    case DObject::StereotypeNone:
        return 1;
    case DObject::StereotypeLabel:
        return 2;
    case DObject::StereotypeDecoration:
        return 3;
    case DObject::StereotypeIcon:
        return 4;
    case DObject::StereotypeSmart:
        return 0;
    }
    return 0;
}

static DObject::StereotypeDisplay translateIndexToStereotypeDisplay(int index)
{
    static const DObject::StereotypeDisplay map[] = {
        DObject::StereotypeSmart,
        DObject::StereotypeNone,
        DObject::StereotypeLabel,
        DObject::StereotypeDecoration,
        DObject::StereotypeIcon
    };
    QMT_ASSERT(index >= 0 && index <= 4, return DObject::StereotypeSmart);
    return map[index];
}

static int translateTemplateDisplayToIndex(DClass::TemplateDisplay templateDisplay)
{
    switch (templateDisplay) {
    case DClass::TemplateSmart:
        return 0;
    case DClass::TemplateBox:
        return 1;
    case DClass::TemplateName:
        return 2;
    }
    return 0;
}

static DClass::TemplateDisplay translateIndexToTemplateDisplay(int index)
{
    static const DClass::TemplateDisplay map[] = {
        DClass::TemplateSmart,
        DClass::TemplateBox,
        DClass::TemplateName
    };
    QMT_ASSERT(index >= 0 && index <= 2, return DClass::TemplateSmart);
    return map[index];
}

static int translateRelationVisualPrimaryRoleToIndex(DRelation::VisualPrimaryRole visualRole)
{
    switch (visualRole) {
    case DRelation::PrimaryRoleNormal:
        return 0;
    case DRelation::PrimaryRoleCustom1:
        return 1;
    case DRelation::PrimaryRoleCustom2:
        return 2;
    case DRelation::PrimaryRoleCustom3:
        return 3;
    case DRelation::PrimaryRoleCustom4:
        return 4;
    case DRelation::PrimaryRoleCustom5:
        return 5;
    }
    return 0;
}

static DRelation::VisualPrimaryRole translateIndexToRelationVisualPrimaryRole(int index)
{
    static const DRelation::VisualPrimaryRole map[] = {
        DRelation::PrimaryRoleNormal,
        DRelation::PrimaryRoleCustom1, DRelation::PrimaryRoleCustom2, DRelation::PrimaryRoleCustom3,
        DRelation::PrimaryRoleCustom4, DRelation::PrimaryRoleCustom5
    };
    QMT_ASSERT(index >= 0 && index <= 5, return DRelation::PrimaryRoleNormal);
    return map[index];
}

static int translateRelationVisualSecondaryRoleToIndex(DRelation::VisualSecondaryRole visualRole)
{
    switch (visualRole) {
    case DRelation::SecondaryRoleNone:
        return 0;
    case DRelation::SecondaryRoleWarning:
        return 1;
    case DRelation::SecondaryRoleError:
        return 2;
    case DRelation::SecondaryRoleSoften:
        return 3;
    }
    return 0;
}

static DRelation::VisualSecondaryRole translateIndexToRelationVisualSecondaryRole(int index)
{
    static const DRelation::VisualSecondaryRole map[] = {
        DRelation::SecondaryRoleNone,
        DRelation::SecondaryRoleWarning, DRelation::SecondaryRoleError, DRelation::SecondaryRoleSoften
    };
    QMT_ASSERT(index >= 0 && index <= 5, return DRelation::SecondaryRoleNone);
    return map[index];
}

static int translateAnnotationVisualRoleToIndex(DAnnotation::VisualRole visualRole)
{
    switch (visualRole) {
    case DAnnotation::RoleNormal:
        return 0;
    case DAnnotation::RoleTitle:
        return 1;
    case DAnnotation::RoleSubtitle:
        return 2;
    case DAnnotation::RoleEmphasized:
        return 3;
    case DAnnotation::RoleSoften:
        return 4;
    case DAnnotation::RoleFootnote:
        return 5;
    }
    return 0;
}

static DAnnotation::VisualRole translateIndexToAnnotationVisualRole(int index)
{
    static const DAnnotation::VisualRole map[] = {
        DAnnotation::RoleNormal, DAnnotation::RoleTitle, DAnnotation::RoleSubtitle,
        DAnnotation::RoleEmphasized, DAnnotation::RoleSoften, DAnnotation::RoleFootnote
    };
    QMT_ASSERT(index >= 0 && index <= 5, return DAnnotation::RoleNormal);
    return map[index];
}

PropertiesView::MView::MView(PropertiesView *propertiesView)
    : m_propertiesView(propertiesView),
      m_stereotypesController(new StereotypesController(this))
{
}

PropertiesView::MView::~MView()
{
}

void PropertiesView::MView::update(const QList<MElement *> &modelElements)
{
    QMT_ASSERT(modelElements.size() > 0, return);

    m_modelElements = modelElements;
    m_diagramElements.clear();
    m_diagram = nullptr;
    modelElements.at(0)->accept(this);
}

void PropertiesView::MView::update(const QList<DElement *> &diagramElements, MDiagram *diagram)
{
    QMT_ASSERT(diagramElements.size() > 0, return);
    QMT_ASSERT(diagram, return);

    m_diagramElements = diagramElements;
    m_diagram = diagram;
    m_modelElements.clear();
    for (DElement *delement : diagramElements) {
        bool appendedMelement = false;
        if (delement->modelUid().isValid()) {
            MElement *melement = m_propertiesView->modelController()->findElement(delement->modelUid());
            if (melement) {
                m_modelElements.append(melement);
                appendedMelement = true;
            }
        }
        if (!appendedMelement) {
            // ensure that indices within m_diagramElements match indices into m_modelElements
            m_modelElements.append(nullptr);
        }
    }
    diagramElements.at(0)->accept(this);
}

void PropertiesView::MView::edit()
{
    if (m_elementNameLineEdit) {
        m_elementNameLineEdit->setFocus();
        m_elementNameLineEdit->selectAll();
    }
}

void PropertiesView::MView::visitMElement(const MElement *element)
{
    Q_UNUSED(element)

    prepare();
    if (!m_stereotypeComboBox) {
        m_stereotypeComboBox = new QComboBox(m_topWidget);
        m_stereotypeComboBox->setEditable(true);
        m_stereotypeComboBox->setInsertPolicy(QComboBox::NoInsert);
        addRow(Tr::tr("Stereotypes:"), m_stereotypeComboBox, "stereotypes");
        m_stereotypeComboBox->addItems(m_propertiesView->stereotypeController()->knownStereotypes(m_stereotypeElement));
        connect(m_stereotypeComboBox->lineEdit(), &QLineEdit::textEdited,
                this, &PropertiesView::MView::onStereotypesChanged);
        connect(m_stereotypeComboBox, &QComboBox::textActivated,
                this, &PropertiesView::MView::onStereotypesChanged);
    }
    if (!m_stereotypeComboBox->hasFocus()) {
        QList<QString> stereotypeList;
        if (haveSameValue(m_modelElements, &MElement::stereotypes, &stereotypeList)) {
            QString stereotypes = m_stereotypesController->toString(stereotypeList);
            m_stereotypeComboBox->setEnabled(true);
            if (stereotypes != m_stereotypeComboBox->currentText())
                m_stereotypeComboBox->setCurrentText(stereotypes);
        } else {
            m_stereotypeComboBox->clear();
            m_stereotypeComboBox->setEnabled(false);
        }
    }
#ifdef SHOW_DEBUG_PROPERTIES
    if (!m_reverseEngineeredLabel) {
        m_reverseEngineeredLabel = new QLabel(m_topWidget);
        addRow(Tr::tr("Reverse engineered:"), m_reverseEngineeredLabel, "reverse engineered");
    }
    QString text = element->flags().testFlag(MElement::ReverseEngineered) ? Tr::tr("Yes") : Tr::tr("No");
    m_reverseEngineeredLabel->setText(text);
#endif
}

void PropertiesView::MView::visitMObject(const MObject *object)
{
    visitMElement(object);
    QList<MObject *> selection = filter<MObject>(m_modelElements);
    bool isSingleSelection = selection.size() == 1;
    if (!m_elementNameLineEdit) {
        m_elementNameLineEdit = new QLineEdit(m_topWidget);
        addRow(Tr::tr("Name:"), m_elementNameLineEdit, "name");
        connect(m_elementNameLineEdit, &QLineEdit::textChanged,
                this, &PropertiesView::MView::onObjectNameChanged);
    }
    if (isSingleSelection) {
        if (object->name() != m_elementNameLineEdit->text() && !m_elementNameLineEdit->hasFocus())
            m_elementNameLineEdit->setText(object->name());
    } else {
        m_elementNameLineEdit->clear();
    }
    if (m_elementNameLineEdit->isEnabled() != isSingleSelection)
        m_elementNameLineEdit->setEnabled(isSingleSelection);

#ifdef SHOW_DEBUG_PROPERTIES
    if (!m_childrenLabel) {
        m_childrenLabel = new QLabel(m_topWidget);
        addRow(Tr::tr("Children:"), m_childrenLabel, "children");
    }
    m_childrenLabel->setText(QString::number(object->children().size()));
    if (!m_relationsLabel) {
        m_relationsLabel = new QLabel(m_topWidget);
        addRow(Tr::tr("Relations:"), m_relationsLabel, "relations");
    }
    m_relationsLabel->setText(QString::number(object->relations().size()));
#endif
}

void PropertiesView::MView::visitMPackage(const MPackage *package)
{
    if (m_modelElements.size() == 1 && !package->owner())
        setTitle<MPackage>(m_modelElements, Tr::tr("Model"), Tr::tr("Models"));
    else
        setTitle<MPackage>(m_modelElements, Tr::tr("Package"), Tr::tr("Packages"));
    visitMObject(package);
    visitMObjectBehind(package);
}

void PropertiesView::MView::visitMClass(const MClass *klass)
{
    setTitle<MClass>(m_modelElements, Tr::tr("Class"), Tr::tr("Classes"));
    visitMObject(klass);
    QList<MClass *> selection = filter<MClass>(m_modelElements);
    bool isSingleSelection = selection.size() == 1;
    if (!m_namespaceLineEdit) {
        m_namespaceLineEdit = new QLineEdit(m_topWidget);
        addRow(Tr::tr("Namespace:"), m_namespaceLineEdit, "namespace");
        connect(m_namespaceLineEdit, &QLineEdit::textEdited,
                this, &PropertiesView::MView::onNamespaceChanged);
    }
    if (!m_namespaceLineEdit->hasFocus()) {
        QString umlNamespace;
        if (haveSameValue(m_modelElements, &MClass::umlNamespace, &umlNamespace)) {
            m_namespaceLineEdit->setEnabled(true);
            if (umlNamespace != m_namespaceLineEdit->text())
                m_namespaceLineEdit->setText(umlNamespace);
        } else {
            m_namespaceLineEdit->clear();
            m_namespaceLineEdit->setEnabled(false);
        }
    }
    if (!m_templateParametersLineEdit) {
        m_templateParametersLineEdit = new QLineEdit(m_topWidget);
        addRow(Tr::tr("Template:"), m_templateParametersLineEdit, "template");
        connect(m_templateParametersLineEdit, &QLineEdit::textChanged,
                this, &PropertiesView::MView::onTemplateParametersChanged);
    }
    if (isSingleSelection) {
        if (!m_templateParametersLineEdit->hasFocus()) {
            QList<QString> templateParameters = splitTemplateParameters(m_templateParametersLineEdit->text());
            if (klass->templateParameters() != templateParameters)
                m_templateParametersLineEdit->setText(formatTemplateParameters(klass->templateParameters()));
        }
    } else {
        m_templateParametersLineEdit->clear();
    }
    if (m_templateParametersLineEdit->isEnabled() != isSingleSelection)
        m_templateParametersLineEdit->setEnabled(isSingleSelection);
    if (!m_classMembersStatusLabel) {
        QMT_CHECK(!m_classMembersParseButton);
        m_classMembersStatusLabel = new QLabel(m_topWidget);
        m_classMembersParseButton = new QPushButton(Tr::tr("Clean Up"), m_topWidget);
        auto layout = new QHBoxLayout();
        layout->addWidget(m_classMembersStatusLabel);
        layout->addWidget(m_classMembersParseButton);
        layout->setStretch(0, 1);
        layout->setStretch(1, 0);
        addRow("", layout, "members status");
        connect(m_classMembersParseButton, &QAbstractButton::clicked,
                this, &PropertiesView::MView::onParseClassMembers);
    }
    if (m_classMembersParseButton->isEnabled() != isSingleSelection)
        m_classMembersParseButton->setEnabled(isSingleSelection);
    if (!m_classMembersEdit) {
        m_classMembersEdit = new ClassMembersEdit(m_topWidget);
        m_classMembersEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
        addRow(Tr::tr("Members:"), m_classMembersEdit, "members");
        connect(m_classMembersEdit, &ClassMembersEdit::membersChanged,
                this, &PropertiesView::MView::onClassMembersChanged);
        connect(m_classMembersEdit, &ClassMembersEdit::statusChanged,
                this, &PropertiesView::MView::onClassMembersStatusChanged);
    }
    if (isSingleSelection) {
        if (klass->members() != m_classMembersEdit->members() && !m_classMembersEdit->hasFocus())
            m_classMembersEdit->setMembers(klass->members());
    } else {
        m_classMembersEdit->clear();
    }
    if (m_classMembersEdit->isEnabled() != isSingleSelection)
        m_classMembersEdit->setEnabled(isSingleSelection);
    visitMObjectBehind(klass);
}

void PropertiesView::MView::visitMComponent(const MComponent *component)
{
    setTitle<MComponent>(m_modelElements, Tr::tr("Component"), Tr::tr("Components"));
    visitMObject(component);
    visitMObjectBehind(component);
}

void PropertiesView::MView::visitMDiagram(const MDiagram *diagram)
{
    setTitle<MDiagram>(m_modelElements, Tr::tr("Diagram"), Tr::tr("Diagrams"));
    visitMObject(diagram);
#ifdef SHOW_DEBUG_PROPERTIES
    if (!m_diagramsLabel) {
        m_diagramsLabel = new QLabel(m_topWidget);
        addRow(Tr::tr("Elements:"), m_diagramsLabel, "elements");
    }
    m_diagramsLabel->setText(QString::number(diagram->diagramElements().size()));
#endif
}

void PropertiesView::MView::visitMCanvasDiagram(const MCanvasDiagram *diagram)
{
    setTitle<MCanvasDiagram>(m_modelElements, Tr::tr("Canvas Diagram"), Tr::tr("Canvas Diagrams"));
    visitMDiagram(diagram);
    visitMDiagramBehind(diagram);
}

void PropertiesView::MView::visitMItem(const MItem *item)
{
    setTitle<MItem>(item, m_modelElements, Tr::tr("Item"), Tr::tr("Items"));
    visitMObject(item);
    QList<MItem *> selection = filter<MItem>(m_modelElements);
    bool isSingleSelection = selection.size() == 1;
    if (item->isVarietyEditable()) {
        if (!m_itemVarietyEdit) {
            m_itemVarietyEdit = new QLineEdit(m_topWidget);
            addRow(Tr::tr("Variety:"), m_itemVarietyEdit, "variety");
            connect(m_itemVarietyEdit, &QLineEdit::textChanged,
                    this, &PropertiesView::MView::onItemVarietyChanged);
        }
        if (isSingleSelection) {
            if (item->variety() != m_itemVarietyEdit->text() && !m_itemVarietyEdit->hasFocus())
                m_itemVarietyEdit->setText(item->variety());
        } else {
            m_itemVarietyEdit->clear();
        }
        if (m_itemVarietyEdit->isEnabled() != isSingleSelection)
            m_itemVarietyEdit->setEnabled(isSingleSelection);
    }
    visitMObjectBehind(item);
}

void PropertiesView::MView::visitMRelation(const MRelation *relation)
{
    visitMElement(relation);
    QList<MRelation *> selection = filter<MRelation>(m_modelElements);
    bool isSingleSelection = selection.size() == 1;
    if (!m_elementNameLineEdit) {
        m_elementNameLineEdit = new QLineEdit(m_topWidget);
        addRow(Tr::tr("Name:"), m_elementNameLineEdit, "name");
        connect(m_elementNameLineEdit, &QLineEdit::textChanged,
                this, &PropertiesView::MView::onRelationNameChanged);
    }
    if (isSingleSelection) {
        if (relation->name() != m_elementNameLineEdit->text() && !m_elementNameLineEdit->hasFocus())
            m_elementNameLineEdit->setText(relation->name());
    } else {
        m_elementNameLineEdit->clear();
    }
    if (m_elementNameLineEdit->isEnabled() != isSingleSelection)
        m_elementNameLineEdit->setEnabled(isSingleSelection);
    MObject *endAObject = m_propertiesView->modelController()->findObject(relation->endAUid());
    QMT_ASSERT(endAObject, return);
    setEndAName(Tr::tr("End A: %1").arg(endAObject->name()));
    MObject *endBObject = m_propertiesView->modelController()->findObject(relation->endBUid());
    QMT_ASSERT(endBObject, return);
    setEndBName(Tr::tr("End B: %1").arg(endBObject->name()));
}

void PropertiesView::MView::visitMDependency(const MDependency *dependency)
{
    setTitle<MDependency>(m_modelElements, Tr::tr("Dependency"), Tr::tr("Dependencies"));
    visitMRelation(dependency);
    QList<MDependency *> selection = filter<MDependency>(m_modelElements);
    bool isSingleSelection = selection.size() == 1;
    if (!m_directionSelector) {
        m_directionSelector = new QComboBox(m_topWidget);
        m_directionSelector->addItems(QStringList({ "->", "<-", "<->" }));
        addRow(Tr::tr("Direction:"), m_directionSelector, "direction");
        connect(m_directionSelector, &QComboBox::activated,
                this, &PropertiesView::MView::onDependencyDirectionChanged);
    }
    if (isSingleSelection) {
        if ((!isValidDirectionIndex(m_directionSelector->currentIndex())
             || dependency->direction() != translateIndexToDirection(m_directionSelector->currentIndex()))
                && !m_directionSelector->hasFocus()) {
            m_directionSelector->setCurrentIndex(translateDirectionToIndex(dependency->direction()));
        }
    } else {
        m_directionSelector->setCurrentIndex(-1);
    }
    if (m_directionSelector->isEnabled() != isSingleSelection)
        m_directionSelector->setEnabled(isSingleSelection);
}

void PropertiesView::MView::visitMInheritance(const MInheritance *inheritance)
{
    setTitle<MInheritance>(m_modelElements, Tr::tr("Inheritance"), Tr::tr("Inheritances"));
    MObject *derivedClass = m_propertiesView->modelController()->findObject(inheritance->derived());
    QMT_ASSERT(derivedClass, return);
    setEndAName(Tr::tr("Derived class: %1").arg(derivedClass->name()));
    MObject *baseClass = m_propertiesView->modelController()->findObject(inheritance->base());
    QMT_ASSERT(baseClass, return);
    setEndBName(Tr::tr("Base class: %1").arg(baseClass->name()));
    visitMRelation(inheritance);
}

void PropertiesView::MView::visitMAssociation(const MAssociation *association)
{
    setTitle<MAssociation>(m_modelElements, Tr::tr("Association"), Tr::tr("Associations"));
    visitMRelation(association);
    QList<MAssociation *> selection = filter<MAssociation>(m_modelElements);
    bool isSingleSelection = selection.size() == 1;
    if (!m_endALabel) {
        m_endALabel = new QLabel("<b>" + m_endAName + "</b>");
        addRow(m_endALabel, "end a");
    }
    if (!m_endAEndName) {
        m_endAEndName = new QLineEdit(m_topWidget);
        addRow(Tr::tr("Role:"), m_endAEndName, "role a");
        connect(m_endAEndName, &QLineEdit::textChanged,
                this, &PropertiesView::MView::onAssociationEndANameChanged);
    }
    if (isSingleSelection) {
        if (association->endA().name() != m_endAEndName->text() && !m_endAEndName->hasFocus())
            m_endAEndName->setText(association->endA().name());
    } else {
        m_endAEndName->clear();
    }
    if (m_endAEndName->isEnabled() != isSingleSelection)
        m_endAEndName->setEnabled(isSingleSelection);
    if (!m_endACardinality) {
        m_endACardinality = new QLineEdit(m_topWidget);
        addRow(Tr::tr("Cardinality:"), m_endACardinality, "cardinality a");
        connect(m_endACardinality, &QLineEdit::textChanged,
                this, &PropertiesView::MView::onAssociationEndACardinalityChanged);
    }
    if (isSingleSelection) {
        if (association->endA().cardinality() != m_endACardinality->text() && !m_endACardinality->hasFocus())
            m_endACardinality->setText(association->endA().cardinality());
    } else {
        m_endACardinality->clear();
    }
    if (m_endACardinality->isEnabled() != isSingleSelection)
        m_endACardinality->setEnabled(isSingleSelection);
    if (!m_endANavigable) {
        m_endANavigable = new QCheckBox(Tr::tr("Navigable"), m_topWidget);
        addRow(QString(), m_endANavigable, "navigable a");
        connect(m_endANavigable, &QAbstractButton::clicked,
                this, &PropertiesView::MView::onAssociationEndANavigableChanged);
    }
    if (isSingleSelection) {
        if (association->endA().isNavigable() != m_endANavigable->isChecked())
            m_endANavigable->setChecked(association->endA().isNavigable());
    } else {
        m_endANavigable->setChecked(false);
    }
    if (m_endANavigable->isEnabled() != isSingleSelection)
        m_endANavigable->setEnabled(isSingleSelection);
    if (!m_endAKind) {
        m_endAKind = new QComboBox(m_topWidget);
        m_endAKind->addItems({ Tr::tr("Association"), Tr::tr("Aggregation"), Tr::tr("Composition") });
        addRow(Tr::tr("Relationship:"), m_endAKind, "relationship a");
        connect(m_endAKind, &QComboBox::activated,
                this, &PropertiesView::MView::onAssociationEndAKindChanged);
    }
    if (isSingleSelection) {
        if ((!isValidAssociationKindIndex(m_endAKind->currentIndex())
             || association->endA().kind() != translateIndexToAssociationKind(m_endAKind->currentIndex()))
                && !m_endAKind->hasFocus()) {
            m_endAKind->setCurrentIndex(translateAssociationKindToIndex(association->endA().kind()));
        }
    } else {
        m_endAKind->setCurrentIndex(-1);
    }
    if (m_endAKind->isEnabled() != isSingleSelection)
        m_endAKind->setEnabled(isSingleSelection);

    if (!m_endBLabel) {
        m_endBLabel = new QLabel("<b>" + m_endBName + "</b>");
        addRow(m_endBLabel, "end b");
    }
    if (!m_endBEndName) {
        m_endBEndName = new QLineEdit(m_topWidget);
        addRow(Tr::tr("Role:"), m_endBEndName, "role b");
        connect(m_endBEndName, &QLineEdit::textChanged,
                this, &PropertiesView::MView::onAssociationEndBNameChanged);
    }
    if (isSingleSelection) {
        if (association->endB().name() != m_endBEndName->text() && !m_endBEndName->hasFocus())
            m_endBEndName->setText(association->endB().name());
    } else {
        m_endBEndName->clear();
    }
    if (m_endBEndName->isEnabled() != isSingleSelection)
        m_endBEndName->setEnabled(isSingleSelection);
    if (!m_endBCardinality) {
        m_endBCardinality = new QLineEdit(m_topWidget);
        addRow(Tr::tr("Cardinality:"), m_endBCardinality, "cardinality b");
        connect(m_endBCardinality, &QLineEdit::textChanged,
                this, &PropertiesView::MView::onAssociationEndBCardinalityChanged);
    }
    if (isSingleSelection) {
        if (association->endB().cardinality() != m_endBCardinality->text() && !m_endBCardinality->hasFocus())
            m_endBCardinality->setText(association->endB().cardinality());
    } else {
        m_endBCardinality->clear();
    }
    if (m_endBCardinality->isEnabled() != isSingleSelection)
        m_endBCardinality->setEnabled(isSingleSelection);
    if (!m_endBNavigable) {
        m_endBNavigable = new QCheckBox(Tr::tr("Navigable"), m_topWidget);
        addRow(QString(), m_endBNavigable, "navigable b");
        connect(m_endBNavigable, &QAbstractButton::clicked,
                this, &PropertiesView::MView::onAssociationEndBNavigableChanged);
    }
    if (isSingleSelection) {
        if (association->endB().isNavigable() != m_endBNavigable->isChecked())
            m_endBNavigable->setChecked(association->endB().isNavigable());
    } else {
        m_endBNavigable->setChecked(false);
    }
    if (m_endBNavigable->isEnabled() != isSingleSelection)
        m_endBNavigable->setEnabled(isSingleSelection);
    if (!m_endBKind) {
        m_endBKind = new QComboBox(m_topWidget);
        m_endBKind->addItems({ Tr::tr("Association"), Tr::tr("Aggregation"), Tr::tr("Composition") });
        addRow(Tr::tr("Relationship:"), m_endBKind, "relationship b");
        connect(m_endBKind, &QComboBox::activated,
                this, &PropertiesView::MView::onAssociationEndBKindChanged);
    }
    if (isSingleSelection) {
        if ((!isValidAssociationKindIndex(m_endBKind->currentIndex())
             || association->endB().kind() != translateIndexToAssociationKind(m_endBKind->currentIndex()))
                && !m_endBKind->hasFocus()) {
            m_endBKind->setCurrentIndex(translateAssociationKindToIndex(association->endB().kind()));
        }
    } else {
        m_endBKind->setCurrentIndex(-1);
    }
    if (m_endBKind->isEnabled() != isSingleSelection)
        m_endBKind->setEnabled(isSingleSelection);
}

void PropertiesView::MView::visitMConnection(const MConnection *connection)
{
    setTitle<MConnection>(connection, m_modelElements, Tr::tr("Connection"), Tr::tr("Connections"));
    visitMRelation(connection);
    QList<MConnection *> selection = filter<MConnection>(m_modelElements);
    const bool isSingleSelection = selection.size() == 1;
    if (!m_endALabel) {
        m_endALabel = new QLabel("<b>" + m_endAName + "</b>");
        addRow(m_endALabel, "end a");
    }
    if (!m_endAEndName) {
        m_endAEndName = new QLineEdit(m_topWidget);
        addRow(Tr::tr("Role:"), m_endAEndName, "role a");
        connect(m_endAEndName, &QLineEdit::textChanged,
                this, &PropertiesView::MView::onConnectionEndANameChanged);
    }
    if (isSingleSelection) {
        if (connection->endA().name() != m_endAEndName->text() && !m_endAEndName->hasFocus())
            m_endAEndName->setText(connection->endA().name());
    } else {
        m_endAEndName->clear();
    }
    if (m_endAEndName->isEnabled() != isSingleSelection)
        m_endAEndName->setEnabled(isSingleSelection);
    if (!m_endACardinality) {
        m_endACardinality = new QLineEdit(m_topWidget);
        addRow(Tr::tr("Cardinality:"), m_endACardinality, "cardinality a");
        connect(m_endACardinality, &QLineEdit::textChanged,
                this, &PropertiesView::MView::onConnectionEndACardinalityChanged);
    }
    if (isSingleSelection) {
        if (connection->endA().cardinality() != m_endACardinality->text() && !m_endACardinality->hasFocus())
            m_endACardinality->setText(connection->endA().cardinality());
    } else {
        m_endACardinality->clear();
    }
    if (m_endACardinality->isEnabled() != isSingleSelection)
        m_endACardinality->setEnabled(isSingleSelection);
    if (!m_endANavigable) {
        m_endANavigable = new QCheckBox(Tr::tr("Navigable"), m_topWidget);
        addRow(QString(), m_endANavigable, "navigable a");
        connect(m_endANavigable, &QAbstractButton::clicked,
                this, &PropertiesView::MView::onConnectionEndANavigableChanged);
    }
    if (isSingleSelection) {
        if (connection->endA().isNavigable() != m_endANavigable->isChecked())
            m_endANavigable->setChecked(connection->endA().isNavigable());
    } else {
        m_endANavigable->setChecked(false);
    }
    if (m_endANavigable->isEnabled() != isSingleSelection)
        m_endANavigable->setEnabled(isSingleSelection);

    if (!m_endBLabel) {
        m_endBLabel = new QLabel("<b>" + m_endBName + "</b>");
        addRow(m_endBLabel, "end b");
    }
    if (!m_endBEndName) {
        m_endBEndName = new QLineEdit(m_topWidget);
        addRow(Tr::tr("Role:"), m_endBEndName, "role b");
        connect(m_endBEndName, &QLineEdit::textChanged,
                this, &PropertiesView::MView::onConnectionEndBNameChanged);
    }
    if (isSingleSelection) {
        if (connection->endB().name() != m_endBEndName->text() && !m_endBEndName->hasFocus())
            m_endBEndName->setText(connection->endB().name());
    } else {
        m_endBEndName->clear();
    }
    if (m_endBEndName->isEnabled() != isSingleSelection)
        m_endBEndName->setEnabled(isSingleSelection);
    if (!m_endBCardinality) {
        m_endBCardinality = new QLineEdit(m_topWidget);
        addRow(Tr::tr("Cardinality:"), m_endBCardinality, "cardinality b");
        connect(m_endBCardinality, &QLineEdit::textChanged,
                this, &PropertiesView::MView::onConnectionEndBCardinalityChanged);
    }
    if (isSingleSelection) {
        if (connection->endB().cardinality() != m_endBCardinality->text() && !m_endBCardinality->hasFocus())
            m_endBCardinality->setText(connection->endB().cardinality());
    } else {
        m_endBCardinality->clear();
    }
    if (m_endBCardinality->isEnabled() != isSingleSelection)
        m_endBCardinality->setEnabled(isSingleSelection);
    if (!m_endBNavigable) {
        m_endBNavigable = new QCheckBox(Tr::tr("Navigable"), m_topWidget);
        addRow(QString(), m_endBNavigable, "navigable b");
        connect(m_endBNavigable, &QAbstractButton::clicked,
                this, &PropertiesView::MView::onConnectionEndBNavigableChanged);
    }
    if (isSingleSelection) {
        if (connection->endB().isNavigable() != m_endBNavigable->isChecked())
            m_endBNavigable->setChecked(connection->endB().isNavigable());
    } else {
        m_endBNavigable->setChecked(false);
    }
    if (m_endBNavigable->isEnabled() != isSingleSelection)
        m_endBNavigable->setEnabled(isSingleSelection);
}

void PropertiesView::MView::visitDElement(const DElement *element)
{
    Q_UNUSED(element)

    if (m_modelElements.size() > 0 && m_modelElements.at(0)) {
        m_propertiesTitle.clear();
        m_modelElements.at(0)->accept(this);
#ifdef SHOW_DEBUG_PROPERTIES
        if (!m_separatorLine) {
            m_separatorLine = new QFrame(m_topWidget);
            m_separatorLine->setFrameShape(QFrame::StyledPanel);
            m_separatorLine->setLineWidth(2);
            m_separatorLine->setMinimumHeight(2);
            addRow(m_separatorLine, "separator");
        }
#endif
    } else {
        prepare();
    }
}

void PropertiesView::MView::visitDObject(const DObject *object)
{
    visitDElement(object);
    visitDObjectBefore(object);
#ifdef SHOW_DEBUG_PROPERTIES
    if (!m_posRectLabel) {
        m_posRectLabel = new QLabel(m_topWidget);
        addRow(Tr::tr("Position and size:"), m_posRectLabel, "position and size");
    }
    m_posRectLabel->setText(QString("(%1,%2):(%3,%4)-(%5,%6)")
                             .arg(object->pos().x())
                             .arg(object->pos().y())
                             .arg(object->rect().left())
                             .arg(object->rect().top())
                             .arg(object->rect().right())
                             .arg(object->rect().bottom()));
#endif
    if (!m_autoSizedCheckbox) {
        m_autoSizedCheckbox = new QCheckBox(Tr::tr("Auto sized"), m_topWidget);
        addRow(QString(), m_autoSizedCheckbox, "auto size");
        connect(m_autoSizedCheckbox, &QAbstractButton::clicked,
                this, &PropertiesView::MView::onAutoSizedChanged);
    }
    if (!m_autoSizedCheckbox->hasFocus()) {
        bool autoSized;
        if (haveSameValue(m_diagramElements, &DObject::isAutoSized, &autoSized))
            m_autoSizedCheckbox->setChecked(autoSized);
        else
            m_autoSizedCheckbox->setChecked(false);
    }
    if (!m_visualPrimaryRoleSelector) {
        m_visualPrimaryRoleSelector = new PaletteBox(m_topWidget);
        setPrimaryRolePalette(m_styleElementType, DObject::PrimaryRoleCustom1, QColor());
        setPrimaryRolePalette(m_styleElementType, DObject::PrimaryRoleCustom2, QColor());
        setPrimaryRolePalette(m_styleElementType, DObject::PrimaryRoleCustom3, QColor());
        setPrimaryRolePalette(m_styleElementType, DObject::PrimaryRoleCustom4, QColor());
        setPrimaryRolePalette(m_styleElementType, DObject::PrimaryRoleCustom5, QColor());
        addRow(Tr::tr("Color:"), m_visualPrimaryRoleSelector, "color");
        connect(m_visualPrimaryRoleSelector, &PaletteBox::activated,
                this, &PropertiesView::MView::onVisualPrimaryRoleChanged);
    }
    if (!m_visualPrimaryRoleSelector->hasFocus()) {
        StereotypeDisplayVisitor stereotypeDisplayVisitor;
        stereotypeDisplayVisitor.setModelController(m_propertiesView->modelController());
        stereotypeDisplayVisitor.setStereotypeController(m_propertiesView->stereotypeController());
        object->accept(&stereotypeDisplayVisitor);
        QString shapeId = stereotypeDisplayVisitor.shapeIconId();
        QColor baseColor;
        if (!shapeId.isEmpty()) {
            StereotypeIcon stereotypeIcon = m_propertiesView->stereotypeController()->findStereotypeIcon(shapeId);
            baseColor = stereotypeIcon.baseColor();
        }
        setPrimaryRolePalette(m_styleElementType, DObject::PrimaryRoleNormal, baseColor);
        DObject::VisualPrimaryRole visualPrimaryRole;
        if (haveSameValue(m_diagramElements, &DObject::visualPrimaryRole, &visualPrimaryRole))
            m_visualPrimaryRoleSelector->setCurrentIndex(translateVisualPrimaryRoleToIndex(visualPrimaryRole));
        else
            m_visualPrimaryRoleSelector->setCurrentIndex(-1);
    }
    if (!m_visualSecondaryRoleSelector) {
        m_visualSecondaryRoleSelector = new QComboBox(m_topWidget);
        m_visualSecondaryRoleSelector->addItems({ Tr::tr("Normal"), Tr::tr("Lighter"), Tr::tr("Darker"),
                                                  Tr::tr("Soften"), Tr::tr("Outline"), Tr::tr("Flat") });
        addRow(Tr::tr("Role:"), m_visualSecondaryRoleSelector, "role");
        connect(m_visualSecondaryRoleSelector, &QComboBox::activated,
                this, &PropertiesView::MView::onVisualSecondaryRoleChanged);
    }
    if (!m_visualSecondaryRoleSelector->hasFocus()) {
        DObject::VisualSecondaryRole visualSecondaryRole;
        if (haveSameValue(m_diagramElements, &DObject::visualSecondaryRole, &visualSecondaryRole))
            m_visualSecondaryRoleSelector->setCurrentIndex(translateVisualSecondaryRoleToIndex(visualSecondaryRole));
        else
            m_visualSecondaryRoleSelector->setCurrentIndex(-1);
    }
    if (!m_visualEmphasizedCheckbox) {
        m_visualEmphasizedCheckbox = new QCheckBox(Tr::tr("Emphasized"), m_topWidget);
        addRow(QString(), m_visualEmphasizedCheckbox, "emphasized");
        connect(m_visualEmphasizedCheckbox, &QAbstractButton::clicked,
                this, &PropertiesView::MView::onVisualEmphasizedChanged);
    }
    if (!m_visualEmphasizedCheckbox->hasFocus()) {
        bool emphasized;
        if (haveSameValue(m_diagramElements, &DObject::isVisualEmphasized, &emphasized))
            m_visualEmphasizedCheckbox->setChecked(emphasized);
        else
            m_visualEmphasizedCheckbox->setChecked(false);
    }
    if (!m_stereotypeDisplaySelector) {
        m_stereotypeDisplaySelector = new QComboBox(m_topWidget);
        m_stereotypeDisplaySelector->addItems({ Tr::tr("Smart"), Tr::tr("None"), Tr::tr("Label"),
                                                Tr::tr("Decoration"), Tr::tr("Icon") });
        addRow(Tr::tr("Stereotype display:"), m_stereotypeDisplaySelector, "stereotype display");
        connect(m_stereotypeDisplaySelector, &QComboBox::activated,
                this, &PropertiesView::MView::onStereotypeDisplayChanged);
    }
    if (!m_stereotypeDisplaySelector->hasFocus()) {
        DObject::StereotypeDisplay stereotypeDisplay;
        if (haveSameValue(m_diagramElements, &DObject::stereotypeDisplay, &stereotypeDisplay))
            m_stereotypeDisplaySelector->setCurrentIndex(translateStereotypeDisplayToIndex(stereotypeDisplay));
        else
            m_stereotypeDisplaySelector->setCurrentIndex(-1);
    }
#ifdef SHOW_DEBUG_PROPERTIES
    if (!m_depthLabel) {
        m_depthLabel = new QLabel(m_topWidget);
        addRow(Tr::tr("Depth:"), m_depthLabel, "depth");
    }
    m_depthLabel->setText(QString::number(object->depth()));
#endif
}

void PropertiesView::MView::visitDPackage(const DPackage *package)
{
    setTitle<DPackage>(m_diagramElements, Tr::tr("Package"), Tr::tr("Packages"));
    setStereotypeIconElement(StereotypeIcon::ElementPackage);
    setStyleElementType(StyleEngine::TypePackage);
    visitDObject(package);
}

void PropertiesView::MView::visitDClass(const DClass *klass)
{
    setTitle<DClass>(m_diagramElements, Tr::tr("Class"), Tr::tr("Classes"));
    setStereotypeIconElement(StereotypeIcon::ElementClass);
    setStyleElementType(StyleEngine::TypeClass);
    visitDObject(klass);
    if (!m_templateDisplaySelector) {
        m_templateDisplaySelector = new QComboBox(m_topWidget);
        m_templateDisplaySelector->addItems({ Tr::tr("Smart"), Tr::tr("Box"), Tr::tr("Angle Brackets") });
        addRow(Tr::tr("Template display:"), m_templateDisplaySelector, "template display");
        connect(m_templateDisplaySelector, &QComboBox::activated,
                this, &PropertiesView::MView::onTemplateDisplayChanged);
    }
    if (!m_templateDisplaySelector->hasFocus()) {
        DClass::TemplateDisplay templateDisplay;
        if (haveSameValue(m_diagramElements, &DClass::templateDisplay, &templateDisplay))
            m_templateDisplaySelector->setCurrentIndex(translateTemplateDisplayToIndex(templateDisplay));
        else
            m_templateDisplaySelector->setCurrentIndex(-1);
    }
    if (!m_showAllMembersCheckbox) {
        m_showAllMembersCheckbox = new QCheckBox(Tr::tr("Show members"), m_topWidget);
        addRow(QString(), m_showAllMembersCheckbox, "show members");
        connect(m_showAllMembersCheckbox, &QAbstractButton::clicked,
                this, &PropertiesView::MView::onShowAllMembersChanged);
    }
    if (!m_showAllMembersCheckbox->hasFocus()) {
        bool showAllMembers;
        if (haveSameValue(m_diagramElements, &DClass::showAllMembers, &showAllMembers))
            m_showAllMembersCheckbox->setChecked(showAllMembers);
        else
            m_showAllMembersCheckbox->setChecked(false);
    }
}

void PropertiesView::MView::visitDComponent(const DComponent *component)
{
    setTitle<DComponent>(m_diagramElements, Tr::tr("Component"), Tr::tr("Components"));
    setStereotypeIconElement(StereotypeIcon::ElementComponent);
    setStyleElementType(StyleEngine::TypeComponent);
    visitDObject(component);
    if (!m_plainShapeCheckbox) {
        m_plainShapeCheckbox = new QCheckBox(Tr::tr("Plain shape"), m_topWidget);
        addRow(QString(), m_plainShapeCheckbox, "plain shape");
        connect(m_plainShapeCheckbox, &QAbstractButton::clicked,
                this, &PropertiesView::MView::onPlainShapeChanged);
    }
    if (!m_plainShapeCheckbox->hasFocus()) {
        bool plainShape;
        if (haveSameValue(m_diagramElements, &DComponent::isPlainShape, &plainShape))
            m_plainShapeCheckbox->setChecked(plainShape);
        else
            m_plainShapeCheckbox->setChecked(false);
    }
}

void PropertiesView::MView::visitDDiagram(const DDiagram *diagram)
{
    setTitle<DDiagram>(m_diagramElements, Tr::tr("Diagram"), Tr::tr("Diagrams"));
    setStyleElementType(StyleEngine::TypeOther);
    visitDObject(diagram);
}

void PropertiesView::MView::visitDItem(const DItem *item)
{
    setTitle<DItem>(m_diagramElements, Tr::tr("Item"), Tr::tr("Items"));
    setStereotypeIconElement(StereotypeIcon::ElementItem);
    setStyleElementType(StyleEngine::TypeItem);
    visitDObject(item);
    QList<DItem *> selection = filter<DItem>(m_diagramElements);
    bool isSingleSelection = selection.size() == 1;
    if (item->isShapeEditable()) {
        if (!m_itemShapeEdit) {
            m_itemShapeEdit = new QLineEdit(m_topWidget);
            addRow(Tr::tr("Shape:"), m_itemShapeEdit, "shape");
            connect(m_itemShapeEdit, &QLineEdit::textChanged,
                    this, &PropertiesView::MView::onItemShapeChanged);
        }
        if (isSingleSelection) {
            if (item->shape() != m_itemShapeEdit->text() && !m_itemShapeEdit->hasFocus())
                m_itemShapeEdit->setText(item->shape());
        } else {
            m_itemShapeEdit->clear();
        }
        if (m_itemShapeEdit->isEnabled() != isSingleSelection)
            m_itemShapeEdit->setEnabled(isSingleSelection);
    }
}

void PropertiesView::MView::visitDRelation(const DRelation *relation)
{
    visitDElement(relation);
    if (!m_relationVisualPrimaryRoleSelector) {
        m_relationVisualPrimaryRoleSelector = new PaletteBox(m_topWidget);
        setRelationPrimaryRolePalette(m_styleElementType, DRelation::PrimaryRoleNormal);
        setRelationPrimaryRolePalette(m_styleElementType, DRelation::PrimaryRoleCustom1);
        setRelationPrimaryRolePalette(m_styleElementType, DRelation::PrimaryRoleCustom2);
        setRelationPrimaryRolePalette(m_styleElementType, DRelation::PrimaryRoleCustom3);
        setRelationPrimaryRolePalette(m_styleElementType, DRelation::PrimaryRoleCustom4);
        setRelationPrimaryRolePalette(m_styleElementType, DRelation::PrimaryRoleCustom5);
        addRow(Tr::tr("Color:"), m_relationVisualPrimaryRoleSelector, "color");
        connect(m_relationVisualPrimaryRoleSelector, &PaletteBox::activated,
                this, &PropertiesView::MView::onRelationVisualPrimaryRoleChanged);
    }
    if (!m_relationVisualPrimaryRoleSelector->hasFocus()) {
        DRelation::VisualPrimaryRole visualPrimaryRole;
        if (haveSameValue(m_diagramElements, &DRelation::visualPrimaryRole, &visualPrimaryRole))
            m_relationVisualPrimaryRoleSelector->setCurrentIndex(translateRelationVisualPrimaryRoleToIndex(visualPrimaryRole));
        else
            m_relationVisualPrimaryRoleSelector->setCurrentIndex(-1);
    }
    if (!m_relationVisualSecondaryRoleSelector) {
        m_relationVisualSecondaryRoleSelector = new QComboBox(m_topWidget);
        m_relationVisualSecondaryRoleSelector->addItems({ Tr::tr("Normal"), Tr::tr("Warning"), Tr::tr("Error"), Tr::tr("Soften") });
        addRow(Tr::tr("Role:"), m_relationVisualSecondaryRoleSelector, "role");
        connect(m_relationVisualSecondaryRoleSelector, QOverload<int>::of(&QComboBox::activated),
                this, &PropertiesView::MView::onRelationVisualSecondaryRoleChanged);
    }
    if (!m_relationVisualSecondaryRoleSelector->hasFocus()) {
        DRelation::VisualSecondaryRole visualSecondaryRole;
        if (haveSameValue(m_diagramElements, &DRelation::visualSecondaryRole, &visualSecondaryRole))
            m_relationVisualSecondaryRoleSelector->setCurrentIndex(translateRelationVisualSecondaryRoleToIndex(visualSecondaryRole));
        else
            m_relationVisualSecondaryRoleSelector->setCurrentIndex(-1);
    }
    if (!m_relationVisualEmphasizedCheckbox) {
        m_relationVisualEmphasizedCheckbox = new QCheckBox(Tr::tr("Emphasized"), m_topWidget);
        addRow(QString(), m_relationVisualEmphasizedCheckbox, "emphasized");
        connect(m_relationVisualEmphasizedCheckbox, &QAbstractButton::clicked,
                this, &PropertiesView::MView::onRelationVisualEmphasizedChanged);
    }
    if (!m_relationVisualEmphasizedCheckbox->hasFocus()) {
        bool emphasized;
        if (haveSameValue(m_diagramElements, &DRelation::isVisualEmphasized, &emphasized))
            m_relationVisualEmphasizedCheckbox->setChecked(emphasized);
        else
            m_relationVisualEmphasizedCheckbox->setChecked(false);
    }
#ifdef SHOW_DEBUG_PROPERTIES
    if (!m_pointsLabel) {
        m_pointsLabel = new QLabel(m_topWidget);
        addRow(Tr::tr("Intermediate points:"), m_pointsLabel, "intermediate points");
    }
    QString points;
    for (const auto &point : relation->intermediatePoints()) {
        if (!points.isEmpty())
            points.append(", ");
        points.append(QString("(%1,%2)").arg(point.pos().x()).arg(point.pos().y()));
    }
    if (points.isEmpty())
        points = Tr::tr("none");
    m_pointsLabel->setText(points);
#endif
}

void PropertiesView::MView::visitDInheritance(const DInheritance *inheritance)
{
    setTitle<DInheritance>(m_diagramElements, Tr::tr("Inheritance"), Tr::tr("Inheritances"));
    visitDRelation(inheritance);
}

void PropertiesView::MView::visitDDependency(const DDependency *dependency)
{
    setTitle<DDependency>(m_diagramElements, Tr::tr("Dependency"), Tr::tr("Dependencies"));
    visitDRelation(dependency);
}

void PropertiesView::MView::visitDAssociation(const DAssociation *association)
{
    setTitle<DAssociation>(m_diagramElements, Tr::tr("Association"), Tr::tr("Associations"));
    visitDRelation(association);
}

void PropertiesView::MView::visitDConnection(const DConnection *connection)
{
    setTitle<DConnection>(m_diagramElements, Tr::tr("Connection"), Tr::tr("Connections"));
    visitDRelation(connection);
}

void PropertiesView::MView::visitDAnnotation(const DAnnotation *annotation)
{
    setTitle<DAnnotation>(m_diagramElements, Tr::tr("Annotation"), Tr::tr("Annotations"));
    visitDElement(annotation);
    if (!m_annotationAutoWidthCheckbox) {
        m_annotationAutoWidthCheckbox = new QCheckBox(Tr::tr("Auto width"), m_topWidget);
        addRow(QString(), m_annotationAutoWidthCheckbox, "auto width");
        connect(m_annotationAutoWidthCheckbox, &QAbstractButton::clicked,
                this, &PropertiesView::MView::onAutoWidthChanged);
    }
    if (!m_annotationAutoWidthCheckbox->hasFocus()) {
        bool autoSized;
        if (haveSameValue(m_diagramElements, &DAnnotation::isAutoSized, &autoSized))
            m_annotationAutoWidthCheckbox->setChecked(autoSized);
        else
            m_annotationAutoWidthCheckbox->setChecked(false);
    }
    if (!m_annotationVisualRoleSelector) {
        m_annotationVisualRoleSelector = new QComboBox(m_topWidget);
        m_annotationVisualRoleSelector->addItems(QStringList({ Tr::tr("Normal"), Tr::tr("Title"),
                                                               Tr::tr("Subtitle"), Tr::tr("Emphasized"),
                                                               Tr::tr("Soften"), Tr::tr("Footnote") }));
        addRow(Tr::tr("Role:"), m_annotationVisualRoleSelector, "visual role");
        connect(m_annotationVisualRoleSelector, &QComboBox::activated,
                this, &PropertiesView::MView::onAnnotationVisualRoleChanged);
    }
    if (!m_annotationVisualRoleSelector->hasFocus()) {
        DAnnotation::VisualRole visualRole;
        if (haveSameValue(m_diagramElements, &DAnnotation::visualRole, &visualRole))
            m_annotationVisualRoleSelector->setCurrentIndex(translateAnnotationVisualRoleToIndex(visualRole));
        else
            m_annotationVisualRoleSelector->setCurrentIndex(-1);
    }
}

void PropertiesView::MView::visitDBoundary(const DBoundary *boundary)
{
    setTitle<DBoundary>(m_diagramElements, Tr::tr("Boundary"), Tr::tr("Boundaries"));
    visitDElement(boundary);
}

void PropertiesView::MView::visitDSwimlane(const DSwimlane *swimlane)
{
    setTitle<DSwimlane>(m_diagramElements, Tr::tr("Swimlane"), Tr::tr("Swimlanes"));
    visitDElement(swimlane);
}

void PropertiesView::MView::visitMElementBehind(const MElement *element)
{
    Q_UNUSED(element)
}

void PropertiesView::MView::visitMObjectBehind(const MObject *object)
{
    visitMElementBehind(object);
}

void PropertiesView::MView::visitMDiagramBehind(const MDiagram *diagram)
{
    visitMObjectBehind(diagram);
}

void PropertiesView::MView::visitDObjectBefore(const DObject *object)
{
    Q_UNUSED(object)
}

void PropertiesView::MView::onStereotypesChanged(const QString &stereotypes)
{
    QList<QString> set = m_stereotypesController->fromString(stereotypes);
    assignModelElement<MElement, QList<QString>>(m_modelElements, SelectionMulti, set,
                                                  &MElement::stereotypes, &MElement::setStereotypes);
}

void PropertiesView::MView::onObjectNameChanged(const QString &name)
{
    assignModelElement<MObject, QString>(m_modelElements, SelectionSingle, name, &MObject::name, &MObject::setName);
}

void PropertiesView::MView::onNamespaceChanged(const QString &umlNamespace)
{
    assignModelElement<MClass, QString>(m_modelElements, SelectionMulti, umlNamespace,
                                        &MClass::umlNamespace, &MClass::setUmlNamespace);
}

void PropertiesView::MView::onTemplateParametersChanged(const QString &templateParameters)
{
    QList<QString> templateParametersList = splitTemplateParameters(templateParameters);
    assignModelElement<MClass, QList<QString>>(m_modelElements, SelectionSingle, templateParametersList,
                                                &MClass::templateParameters, &MClass::setTemplateParameters);
}

void PropertiesView::MView::onClassMembersStatusChanged(bool valid)
{
    if (valid)
        m_classMembersStatusLabel->clear();
    else
        m_classMembersStatusLabel->setText("<font color=red>" + Tr::tr("Invalid syntax.") + "</font>");
}

void PropertiesView::MView::onParseClassMembers()
{
    m_classMembersEdit->reparse();
}

void PropertiesView::MView::onClassMembersChanged(const QList<MClassMember> &classMembers)
{
    QSet<Uid> showMembers;
    if (!classMembers.isEmpty()) {
        for (MElement *element : std::as_const(m_modelElements)) {
            MClass *klass = dynamic_cast<MClass *>(element);
            if (klass && klass->members().isEmpty())
                showMembers.insert(klass->uid());
        }
    }
    assignModelElement<MClass, QList<MClassMember>>(m_modelElements, SelectionSingle, classMembers,
                                                     &MClass::members, &MClass::setMembers);
    for (DElement *element : std::as_const(m_diagramElements)) {
        if (showMembers.contains(element->modelUid())) {
            assignModelElement<DClass, bool>(QList<DElement *>({element}), SelectionSingle, true,
                                             &DClass::showAllMembers, &DClass::setShowAllMembers);
        }
    }
}

void PropertiesView::MView::onItemVarietyChanged(const QString &variety)
{
    assignModelElement<MItem, QString>(m_modelElements, SelectionSingle, variety, &MItem::variety, &MItem::setVariety);
}

void PropertiesView::MView::onRelationNameChanged(const QString &name)
{
    assignModelElement<MRelation, QString>(m_modelElements, SelectionSingle, name,
                                           &MRelation::name, &MRelation::setName);
}

void PropertiesView::MView::onDependencyDirectionChanged(int directionIndex)
{
    MDependency::Direction direction = translateIndexToDirection(directionIndex);
    assignModelElement<MDependency, MDependency::Direction>(
                m_modelElements, SelectionSingle, direction, &MDependency::direction, &MDependency::setDirection);
}

void PropertiesView::MView::onAssociationEndANameChanged(const QString &name)
{
    assignEmbeddedModelElement<MAssociation, MAssociationEnd, QString>(
                m_modelElements, SelectionSingle, name, &MAssociation::endA, &MAssociation::setEndA,
                &MAssociationEnd::name, &MAssociationEnd::setName);
}

void PropertiesView::MView::onAssociationEndACardinalityChanged(const QString &cardinality)
{
    assignEmbeddedModelElement<MAssociation, MAssociationEnd, QString>(
                m_modelElements, SelectionSingle, cardinality, &MAssociation::endA, &MAssociation::setEndA,
                &MAssociationEnd::cardinality, &MAssociationEnd::setCardinality);
}

void PropertiesView::MView::onAssociationEndANavigableChanged(bool navigable)
{
    assignEmbeddedModelElement<MAssociation, MAssociationEnd, bool>(
                m_modelElements, SelectionSingle, navigable, &MAssociation::endA, &MAssociation::setEndA,
                &MAssociationEnd::isNavigable, &MAssociationEnd::setNavigable);
}

void PropertiesView::MView::onAssociationEndAKindChanged(int kindIndex)
{
    MAssociationEnd::Kind kind = translateIndexToAssociationKind(kindIndex);
    assignEmbeddedModelElement<MAssociation, MAssociationEnd, MAssociationEnd::Kind>(
                m_modelElements, SelectionSingle, kind, &MAssociation::endA, &MAssociation::setEndA,
                &MAssociationEnd::kind, &MAssociationEnd::setKind);
}

void PropertiesView::MView::onAssociationEndBNameChanged(const QString &name)
{
    assignEmbeddedModelElement<MAssociation, MAssociationEnd, QString>(
                m_modelElements, SelectionSingle, name, &MAssociation::endB, &MAssociation::setEndB,
                &MAssociationEnd::name, &MAssociationEnd::setName);
}

void PropertiesView::MView::onAssociationEndBCardinalityChanged(const QString &cardinality)
{
    assignEmbeddedModelElement<MAssociation, MAssociationEnd, QString>(
                m_modelElements, SelectionSingle, cardinality, &MAssociation::endB, &MAssociation::setEndB,
                &MAssociationEnd::cardinality, &MAssociationEnd::setCardinality);
}

void PropertiesView::MView::onAssociationEndBNavigableChanged(bool navigable)
{
    assignEmbeddedModelElement<MAssociation, MAssociationEnd, bool>(
                m_modelElements, SelectionSingle, navigable, &MAssociation::endB, &MAssociation::setEndB,
                &MAssociationEnd::isNavigable, &MAssociationEnd::setNavigable);
}

void PropertiesView::MView::onAssociationEndBKindChanged(int kindIndex)
{
    MAssociationEnd::Kind kind = translateIndexToAssociationKind(kindIndex);
    assignEmbeddedModelElement<MAssociation, MAssociationEnd, MAssociationEnd::Kind>(
                m_modelElements, SelectionSingle, kind, &MAssociation::endB, &MAssociation::setEndB,
                &MAssociationEnd::kind, &MAssociationEnd::setKind);
}

void PropertiesView::MView::onConnectionEndANameChanged(const QString &name)
{
    assignEmbeddedModelElement<MConnection, MConnectionEnd, QString>(
                m_modelElements, SelectionSingle, name, &MConnection::endA, &MConnection::setEndA,
                &MConnectionEnd::name, &MConnectionEnd::setName);
}

void PropertiesView::MView::onConnectionEndACardinalityChanged(const QString &cardinality)
{
    assignEmbeddedModelElement<MConnection, MConnectionEnd, QString>(
                m_modelElements, SelectionSingle, cardinality, &MConnection::endA, &MConnection::setEndA,
                &MConnectionEnd::cardinality, &MConnectionEnd::setCardinality);
}

void PropertiesView::MView::onConnectionEndANavigableChanged(bool navigable)
{
    assignEmbeddedModelElement<MConnection, MConnectionEnd, bool>(
                m_modelElements, SelectionSingle, navigable, &MConnection::endA, &MConnection::setEndA,
                &MConnectionEnd::isNavigable, &MConnectionEnd::setNavigable);
}

void PropertiesView::MView::onConnectionEndBNameChanged(const QString &name)
{
    assignEmbeddedModelElement<MConnection, MConnectionEnd, QString>(
                m_modelElements, SelectionSingle, name, &MConnection::endB, &MConnection::setEndB,
                &MConnectionEnd::name, &MConnectionEnd::setName);
}

void PropertiesView::MView::onConnectionEndBCardinalityChanged(const QString &cardinality)
{
    assignEmbeddedModelElement<MConnection, MConnectionEnd, QString>(
                m_modelElements, SelectionSingle, cardinality, &MConnection::endB, &MConnection::setEndB,
                &MConnectionEnd::cardinality, &MConnectionEnd::setCardinality);
}

void PropertiesView::MView::onConnectionEndBNavigableChanged(bool navigable)
{
    assignEmbeddedModelElement<MConnection, MConnectionEnd, bool>(
                m_modelElements, SelectionSingle, navigable, &MConnection::endB, &MConnection::setEndB,
                &MConnectionEnd::isNavigable, &MConnectionEnd::setNavigable);
}

void PropertiesView::MView::onAutoSizedChanged(bool autoSized)
{
    assignModelElement<DObject, bool>(m_diagramElements, SelectionMulti, autoSized,
                                      &DObject::isAutoSized, &DObject::setAutoSized);
}

void PropertiesView::MView::onVisualPrimaryRoleChanged(int visualRoleIndex)
{
    DObject::VisualPrimaryRole visualRole = translateIndexToVisualPrimaryRole(visualRoleIndex);
    assignModelElement<DObject, DObject::VisualPrimaryRole>(
                m_diagramElements, SelectionMulti, visualRole,
                &DObject::visualPrimaryRole, &DObject::setVisualPrimaryRole);
}

void PropertiesView::MView::onVisualSecondaryRoleChanged(int visualRoleIndex)
{
    DObject::VisualSecondaryRole visualRole = translateIndexToVisualSecondaryRole(visualRoleIndex);
    assignModelElement<DObject, DObject::VisualSecondaryRole>(
                m_diagramElements, SelectionMulti, visualRole,
                &DObject::visualSecondaryRole, &DObject::setVisualSecondaryRole);
}

void PropertiesView::MView::onVisualEmphasizedChanged(bool visualEmphasized)
{
    assignModelElement<DObject, bool>(m_diagramElements, SelectionMulti, visualEmphasized,
                                      &DObject::isVisualEmphasized, &DObject::setVisualEmphasized);
}

void PropertiesView::MView::onStereotypeDisplayChanged(int stereotypeDisplayIndex)
{
    DObject::StereotypeDisplay stereotypeDisplay = translateIndexToStereotypeDisplay(stereotypeDisplayIndex);
    assignModelElement<DObject, DObject::StereotypeDisplay>(
                m_diagramElements, SelectionMulti, stereotypeDisplay,
                &DObject::stereotypeDisplay, &DObject::setStereotypeDisplay);
}

void PropertiesView::MView::onTemplateDisplayChanged(int templateDisplayIndex)
{
    DClass::TemplateDisplay templateDisplay = translateIndexToTemplateDisplay(templateDisplayIndex);
    assignModelElement<DClass, DClass::TemplateDisplay>(
                m_diagramElements, SelectionMulti, templateDisplay,
                &DClass::templateDisplay, &DClass::setTemplateDisplay);
}

void PropertiesView::MView::onShowAllMembersChanged(bool showAllMembers)
{
    assignModelElement<DClass, bool>(m_diagramElements, SelectionMulti, showAllMembers,
                                     &DClass::showAllMembers, &DClass::setShowAllMembers);
}

void PropertiesView::MView::onPlainShapeChanged(bool plainShape)
{
    assignModelElement<DComponent, bool>(m_diagramElements, SelectionMulti, plainShape,
                                         &DComponent::isPlainShape, &DComponent::setPlainShape);
}

void PropertiesView::MView::onItemShapeChanged(const QString &shape)
{
    assignModelElement<DItem, QString>(m_diagramElements, SelectionSingle, shape, &DItem::shape, &DItem::setShape);
}

void PropertiesView::MView::onAutoWidthChanged(bool autoWidthed)
{
    assignModelElement<DAnnotation, bool>(m_diagramElements, SelectionMulti, autoWidthed,
                                          &DAnnotation::isAutoSized, &DAnnotation::setAutoSized);
}

void PropertiesView::MView::onAnnotationVisualRoleChanged(int visualRoleIndex)
{
    DAnnotation::VisualRole visualRole = translateIndexToAnnotationVisualRole((visualRoleIndex));
    assignModelElement<DAnnotation, DAnnotation::VisualRole>(
        m_diagramElements, SelectionMulti, visualRole, &DAnnotation::visualRole, &DAnnotation::setVisualRole);
}


void PropertiesView::MView::onRelationVisualPrimaryRoleChanged(int visualRoleIndex)
{
    DRelation::VisualPrimaryRole visualRole = translateIndexToRelationVisualPrimaryRole(visualRoleIndex);
    assignModelElement<DRelation, DRelation::VisualPrimaryRole>(
        m_diagramElements, SelectionMulti, visualRole,
        &DRelation::visualPrimaryRole, &DRelation::setVisualPrimaryRole);
}

void PropertiesView::MView::onRelationVisualSecondaryRoleChanged(int visualRoleIndex)
{
    DRelation::VisualSecondaryRole visualRole = translateIndexToRelationVisualSecondaryRole(visualRoleIndex);
    assignModelElement<DRelation, DRelation::VisualSecondaryRole>(
        m_diagramElements, SelectionMulti, visualRole,
        &DRelation::visualSecondaryRole, &DRelation::setVisualSecondaryRole);
}

void PropertiesView::MView::onRelationVisualEmphasizedChanged(bool visualEmphasized)
{
    assignModelElement<DRelation, bool>(m_diagramElements, SelectionMulti, visualEmphasized,
                                      &DRelation::isVisualEmphasized, &DRelation::setVisualEmphasized);
}

void PropertiesView::MView::onRelationColorChanged(const QColor &color)
{
    assignModelElement<DRelation, QColor>(m_diagramElements, SelectionMulti, color,
                                          &DRelation::color, &DRelation::setColor);
}

void PropertiesView::MView::onRelationThicknessChanged(qreal thickness)
{
    assignModelElement<DRelation, qreal>(m_diagramElements, SelectionMulti, thickness,
                                         &DRelation::thickness, &DRelation::setThickness);
}

void PropertiesView::MView::prepare()
{
    QMT_CHECK(!m_propertiesTitle.isEmpty());
    if (!m_topWidget) {
        m_topWidget = new QWidget();
        m_topLayout = new QFormLayout(m_topWidget);
        m_topWidget->setLayout(m_topLayout);
    }
    if (!m_classNameLabel) {
        m_classNameLabel = new QLabel();
        m_topLayout->addRow(m_classNameLabel);
        m_rowToId.append("title");
    }
    QString title("<b>" + m_propertiesTitle + "</b>");
    if (m_classNameLabel->text() != title)
        m_classNameLabel->setText(title);
}

void PropertiesView::MView::addRow(const QString &label, QLayout *layout, const char *id)
{
    m_topLayout->addRow(label, layout);
    m_rowToId.append(id);
}

void PropertiesView::MView::addRow(const QString &label, QWidget *widget, const char *id)
{
    m_topLayout->addRow(label, widget);
    m_rowToId.append(id);
}

void PropertiesView::MView::addRow(QWidget *widget, const char *id)
{
    m_topLayout->addRow(widget);
    m_rowToId.append(id);
}

void PropertiesView::MView::insertRow(const char *before_id, const QString &label, QLayout *layout, const char *id)
{
    for (int i = m_rowToId.size() - 1; i >= 0; --i) {
        if (strcmp(m_rowToId.at(i), before_id) == 0) {
            m_topLayout->insertRow(i, label, layout);
            m_rowToId.insert(i, id);
            return;
        }
    }
    addRow(label, layout, id);
}

void PropertiesView::MView::insertRow(const char *before_id, const QString &label, QWidget *widget, const char *id)
{
    for (int i = m_rowToId.size() - 1; i >= 0; --i) {
        if (strcmp(m_rowToId.at(i), before_id) == 0) {
            m_topLayout->insertRow(i, label, widget);
            m_rowToId.insert(i, id);
            return;
        }
    }
    addRow(label, widget, id);
}

void PropertiesView::MView::insertRow(const char *before_id, QWidget *widget, const char *id)
{
    for (int i = m_rowToId.size() - 1; i >= 0; --i) {
        if (strcmp(m_rowToId.at(i), before_id) == 0) {
            m_topLayout->insertRow(i, widget);
            m_rowToId.insert(i, id);
            return;
        }
    }
    addRow(widget, id);
}

template<typename T, typename V>
void PropertiesView::MView::setTitle(const QList<V *> &elements,
                                     const QString &singularTitle, const QString &pluralTitle)
{
    QList<T *> filtered = filter<T>(elements);
    if (filtered.size() == elements.size()) {
        if (elements.size() == 1)
            m_propertiesTitle = singularTitle;
        else
            m_propertiesTitle = pluralTitle;
    } else {
        m_propertiesTitle = Tr::tr("Multi-Selection");
    }
}

template<typename T, typename V>
void PropertiesView::MView::setTitle(const MItem *item, const QList<V *> &elements,
                                     const QString &singularTitle, const QString &pluralTitle)
{
    if (!m_propertiesTitle.isEmpty())
        return;
    QList<T *> filtered = filter<T>(elements);
    if (filtered.size() == elements.size()) {
        if (elements.size() == 1) {
            if (item && !item->isVarietyEditable()) {
                QString stereotypeIconId = m_propertiesView->stereotypeController()
                        ->findStereotypeIconId(StereotypeIcon::ElementItem, {item->variety()});
                if (!stereotypeIconId.isEmpty()) {
                    StereotypeIcon stereotypeIcon = m_propertiesView->stereotypeController()->findStereotypeIcon(stereotypeIconId);
                    m_propertiesTitle = stereotypeIcon.title();
                }
            }
            if (m_propertiesTitle.isEmpty())
                m_propertiesTitle = singularTitle;
        } else {
            m_propertiesTitle = pluralTitle;
        }
    } else {
        m_propertiesTitle = Tr::tr("Multi-Selection");
    }
}

template<typename T, typename V>
void PropertiesView::MView::setTitle(const MConnection *connection, const QList<V *> &elements,
                                     const QString &singularTitle, const QString &pluralTitle)
{
    if (!m_propertiesTitle.isEmpty())
        return;
    QList<T *> filtered = filter<T>(elements);
    if (filtered.size() == elements.size()) {
        if (elements.size() == 1) {
            if (connection) {
                CustomRelation customRelation = m_propertiesView->stereotypeController()
                        ->findCustomRelation(connection->customRelationId());
                if (!customRelation.isNull()) {
                    m_propertiesTitle = customRelation.title();
                    if (m_propertiesTitle.isEmpty())
                        m_propertiesTitle = connection->customRelationId();
                }
            }
            if (m_propertiesTitle.isEmpty())
                m_propertiesTitle = singularTitle;
        } else {
            m_propertiesTitle = pluralTitle;
        }
    } else {
        m_propertiesTitle = Tr::tr("Multi-Selection");
    }
}

void PropertiesView::MView::setStereotypeIconElement(StereotypeIcon::Element stereotypeElement)
{
    if (m_stereotypeElement == StereotypeIcon::ElementAny)
        m_stereotypeElement = stereotypeElement;
}

void PropertiesView::MView::setStyleElementType(StyleEngine::ElementType elementType)
{
    if (m_styleElementType == StyleEngine::TypeOther)
        m_styleElementType = elementType;
}

void PropertiesView::MView::setPrimaryRolePalette(StyleEngine::ElementType elementType,
                                                  DObject::VisualPrimaryRole visualPrimaryRole, const QColor &baseColor)
{
    int index = translateVisualPrimaryRoleToIndex(visualPrimaryRole);
    const Style *style = m_propertiesView->styleController()->adaptObjectStyle(
        elementType, ObjectVisuals(visualPrimaryRole, DObject::SecondaryRoleNone, false, baseColor, 0));
    m_visualPrimaryRoleSelector->setBrush(index, style->fillBrush());
    m_visualPrimaryRoleSelector->setLinePen(index, style->linePen());
}

void PropertiesView::MView::setEndAName(const QString &endAName)
{
    if (m_endAName.isEmpty())
        m_endAName = endAName;
}

void PropertiesView::MView::setEndBName(const QString &endBName)
{
    if (m_endBName.isEmpty())
        m_endBName = endBName;
}

QList<QString> PropertiesView::MView::splitTemplateParameters(const QString &templateParameters)
{
    QList<QString> templateParametersList;
    const QStringList parameters = templateParameters.split(QLatin1Char(','));
    for (const QString &parameter : parameters) {
        const QString &p = parameter.trimmed();
        if (!p.isEmpty())
            templateParametersList.append(p);
    }
    return templateParametersList;
}

QString PropertiesView::MView::formatTemplateParameters(const QList<QString> &templateParametersList)
{
    QString templateParamters;
    bool first = true;
    for (const QString &parameter : templateParametersList) {
        if (!first)
            templateParamters += ", ";
        templateParamters += parameter;
        first = false;
    }
    return templateParamters;
}

void PropertiesView::MView::setRelationPrimaryRolePalette(StyleEngine::ElementType elementType,
                                                          DRelation::VisualPrimaryRole visualPrimaryRole)
{
    int index = translateRelationVisualPrimaryRoleToIndex(visualPrimaryRole);
    const Style *style = m_propertiesView->styleController()->adaptRelationStyle(
        elementType, RelationVisuals(DObject::PrimaryRoleNormal, visualPrimaryRole,
                                     DRelation::SecondaryRoleNone, false));
    m_relationVisualPrimaryRoleSelector->setBrush(index, style->fillBrush());
    m_relationVisualPrimaryRoleSelector->setLinePen(index, style->linePen());
}

} // namespace qmt
