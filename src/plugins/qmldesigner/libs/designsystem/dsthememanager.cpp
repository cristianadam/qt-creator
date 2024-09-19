// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "dsthememanager.h"

#include "dsconstants.h"
#include "dsthemegroup.h"
#include "variantproperty.h"

#include <model.h>
#include <nodeproperty.h>
#include <utils/qtcassert.h>

#include <QLoggingCategory>
#include <QVariant>

namespace {
Q_LOGGING_CATEGORY(dsLog, "qtc.designer.designSystem", QtInfoMsg)

std::optional<QmlDesigner::GroupType> typeToGroupType(const QmlDesigner::TypeName type)
{
    if (type == "color")
        return QmlDesigner::GroupType::Colors;
    if (type == "bool")
        return QmlDesigner::GroupType::Flags;
    if (type == "real")
        return QmlDesigner::GroupType::Numbers;
    if (type == "string")
        return QmlDesigner::GroupType::Strings;

    return {};
}
}

namespace QmlDesigner {

DSThemeManager::DSThemeManager()
{}

DSThemeManager::~DSThemeManager() {}

std::optional<ThemeId> DSThemeManager::addTheme(const ThemeName &themeName)
{
    if (themeName.trimmed().isEmpty() || themeId(themeName)) {
        qCDebug(dsLog) << "Can not add new Theme. Duplicate theme name";
        return {};
    }

    const ThemeId newThemeId = m_themes.empty() ? 1 : m_themes.rbegin()->first + 1;
    if (!m_themes.try_emplace(newThemeId, themeName).second)
        return {};

    // Copy the new theme properties from an old theme(first one).
    if (m_themes.size() > 1)
        duplicateTheme(m_themes.begin()->first, newThemeId);

    return newThemeId;
}

std::optional<ThemeId> DSThemeManager::themeId(const ThemeName &themeName) const
{
    for (auto &[id, name] : m_themes) {
        if (themeName == name)
            return id;
    }
    return {};
}

size_t DSThemeManager::themeCount() const
{
    return m_themes.size();
}

void DSThemeManager::removeTheme(ThemeId id)
{
    if (!m_themes.contains(id))
        return;

    for (auto &[gt, group] : m_groups)
        group->removeTheme(id);

    m_themes.erase(id);
}

void DSThemeManager::duplicateTheme(ThemeId from, ThemeId to)
{
    for (auto &[gt, group] : m_groups)
        group->duplicateValues(from, to);
}

std::optional<ThemeProperty> DSThemeManager::property(ThemeId themeId,
                                                      GroupType gType,
                                                      const PropertyName &name) const
{
    if (m_themes.contains(themeId)) {
        auto groupItr = m_groups.find(gType);
        if (groupItr != m_groups.end())
            return groupItr->second->propertyValue(themeId, name);
    }

    qCDebug(dsLog) << "Error fetching property: {" << themeId << GroupId(gType) << name << "}";
    return {};
}

bool DSThemeManager::addProperty(GroupType gType, const ThemeProperty &p)
{
    if (!m_themes.size()) {
        qCDebug(dsLog) << "Can not add proprty. Themes empty";
        return false;
    }

    // A property is added to all themes.
    DSThemeGroup *dsGroup = propertyGroup(gType);
    QTC_ASSERT(dsGroup, return false);

    bool success = true;
    for (auto itr = m_themes.begin(); itr != m_themes.end(); ++itr)
        success &= dsGroup->addProperty(itr->first, p);

    return success;
}

void DSThemeManager::removeProperty(GroupType gType, const PropertyName &name)
{
    // A property is removed from all themes.
    DSThemeGroup *dsGroup = propertyGroup(gType);
    QTC_ASSERT(dsGroup, return);
    dsGroup->removeProperty(name);
}

void DSThemeManager::updateProperty(ThemeId id, GroupType gType, const ThemeProperty &p)
{
    updateProperty(id, gType, p, p.name);
}

void DSThemeManager::updateProperty(ThemeId id,
                                    GroupType gType,
                                    const ThemeProperty &p,
                                    const PropertyName &newName)
{
    if (!m_themes.contains(id))
        return;

    DSThemeGroup *dsGroup = propertyGroup(gType);
    QTC_ASSERT(dsGroup, return);

    dsGroup->updateProperty(id, newName, p);
}

void DSThemeManager::decorate(ModelNode rootNode, const QByteArray &nodeType, bool isMCU) const
{
    if (!m_themes.size())
        return;

    auto p = rootNode.bindingProperty("currentTheme");
    p.setDynamicTypeNameAndExpression(nodeType, QString::fromLatin1(m_themes.begin()->second));
    if (!isMCU)
        addGroupAliases(rootNode);
    auto model = rootNode.model();

    for (auto &[themeId, themeName] : m_themes) {
        auto themeNode = model->createModelNode(nodeType);
        auto themeProperty = model->rootModelNode().nodeProperty(themeName);
        themeProperty.setDynamicTypeNameAndsetModelNode(nodeType, themeNode);

        // Add property groups
        for (auto &[gt, group] : m_groups)
            group->decorate(themeId, themeNode, !isMCU);
    }
}

void DSThemeManager::decorateThemeInterface(ModelNode rootNode) const
{
    if (!m_themes.size())
        return;

    for (auto &[gt, group] : m_groups)
        group->decorateComponent(rootNode);
}

DSThemeGroup *DSThemeManager::propertyGroup(GroupType type)
{
    auto itr = m_groups.find(type);
    if (itr == m_groups.end())
        itr = m_groups.try_emplace(type, std::make_unique<DSThemeGroup>(type)).first;

    return itr->second.get();
}

void DSThemeManager::addGroupAliases(ModelNode rootNode) const
{
    QSet<PropertyName> groupNames;
    for (auto &[groupType, group] : m_groups) {
        if (group->count())
            groupNames.insert(GroupId(groupType));
    }

    for (const auto &name : groupNames) {
        auto p = rootNode.bindingProperty(name);
        auto binding = QString("currentTheme.%1").arg(QString::fromLatin1(name));
        p.setDynamicTypeNameAndExpression("QtObject", binding);
    }
}

std::optional<QString> DSThemeManager::load(ModelNode rootModelNode)
{
    using PropMap = std::map<PropertyName, AbstractProperty>;
    using ThemeProps = std::map<ThemeId, PropMap>;

    QList<NodeProperty> themes = rootModelNode.nodeProperties();
    if (themes.isEmpty())
        return tr("No themes objects in the collection.");

    auto getAllProps = [](const ModelNode &n) -> PropMap {
        PropMap props;
        auto nodesUnderTheme = n.allSubModelNodesAndThisNode();
        for (auto &n : nodesUnderTheme) {
            for (const AbstractProperty &p : n.properties()) {
                if (!props.insert({p.name().toByteArray(), p}).second)
                    qCDebug(dsLog) << "Duplicate Property, Skipping" << n << p;
            }
        }
        return props;
    };

    ThemeProps themeProps;
    for (auto themeNodeProp : themes) {
        ModelNode themeNode = themeNodeProp.modelNode();
        if (auto themeId = addTheme(themeNodeProp.name().toByteArray()))
            themeProps.insert({*themeId, getAllProps(themeNode)});
    }

    auto themeItr = themeProps.begin();
    // Add default properties
    const PropMap &baseProps = themeItr->second;
    for (auto &[propName, p] : baseProps) {
        GroupType gt;
        if (auto themeProp = findPropertyType(p, gt))
            addProperty(gt, *themeProp);
        else
            continue;

        // Update values for rest of the themes.
        for (auto otherTheme = std::next(themeItr); otherTheme != themeProps.end(); ++otherTheme) {
            const PropMap &props = otherTheme->second;
            if (!props.contains(propName)) {
                qCDebug(dsLog) << "Can't find expected prop" << propName << "in theme"
                               << otherTheme->first;
                continue;
            }

            GroupType otherGroup;
            auto otherThemeProp = findPropertyType(props.at(propName), otherGroup);
            if (otherThemeProp && otherGroup == gt) {
                updateProperty(otherTheme->first, gt, *otherThemeProp);
            } else {
                qCDebug(dsLog) << "Incompatible property" << propName << " found in theme"
                               << otherTheme->first;
            }
        }
    }

    return {};
}

std::optional<ThemeProperty> DSThemeManager::findPropertyType(const AbstractProperty &p,
                                                              GroupType &gt) const
{
    if (auto group = typeToGroupType(p.dynamicTypeName())) {
        gt = *group;
        PropertyName pName = p.name().toByteArray();
        if (p.isVariantProperty()) {
            auto variantProp = p.toVariantProperty();
            QVariant val = variantProp.value();
            return ThemeProperty{pName, val};
        } else if (p.isBindingProperty()) {
            auto binding = p.toBindingProperty();
            QVariant val = binding.expression();
            return ThemeProperty{pName, val, true};
        } else
            qDebug() << "Property type not supported for design system" << pName;
    } else {
        qDebug() << "Can't find suitable group for the property" << p.name();
    }
    return {};
}
}
