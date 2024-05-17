// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "../utils/googletest.h"

#include <bindingproperty.h>
#include <model.h>
#include <nodeproperty.h>
#include <projectstoragemock.h>
#include <sourcepathcachemock.h>
#include <variantproperty.h>

#include <designsystem/dsthemegroup.h>
#include <designsystem/dsthememanager.h>

using QmlDesigner::DSThemeManager;
using QmlDesigner::GroupType;
using QmlDesigner::Import;
using QmlDesigner::ModelNode;
using QmlDesigner::ThemeProperty;

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Not;

namespace {
constexpr const char *P1 = "prop1";
constexpr const char *DarkTheme = "dark";
constexpr QmlDesigner::ThemeId Theme1 = 1;
}

MATCHER_P2(HasNodeProperty, name, typeName, "")
{
    ModelNode n = arg;
    return n.hasNodeProperty(name) && n.nodeProperty(name).modelNode().isValid()
           && n.nodeProperty(name).modelNode().type() == typeName;
}

MATCHER_P2(HasBindingProperty, name, value, "")
{
    ModelNode n = arg;
    return n.hasBindingProperty(name) && n.bindingProperty(name).expression() == value;
}

MATCHER_P2(HasVariantProperty, name, value, "")
{
    ModelNode n = arg;
    return n.hasVariantProperty(name) && n.variantProperty(name).value() == value;
}

MATCHER_P2(HasGroupVariantProperty, groupName, themeProp, "")
{
    ModelNode n = arg;

    ModelNode groupNode = n.nodeProperty(groupName).modelNode();

    return groupNode.isValid() && groupNode.hasVariantProperty(themeProp.name)
           && groupNode.variantProperty(themeProp.name).value() == themeProp.value;
}

MATCHER_P2(HasGroupBindingProperty, groupName, themeProp, "")
{
    ModelNode n = arg;

    ModelNode groupNode = n.nodeProperty(groupName).modelNode();

    return groupNode.isValid() && groupNode.hasBindingProperty(themeProp.name)
           && groupNode.bindingProperty(themeProp.name).expression() == themeProp.value.toString();
}

class DesignSystemQmlTest : public testing::TestWithParam<QmlDesigner::GroupType>
{
protected:
    DesignSystemQmlTest()
        : group(groupType)
    {}

    const QmlDesigner::GroupType groupType = GetParam();
    const QmlDesigner::PropertyName groupName = GroupId(groupType);
    QmlDesigner::DSThemeGroup group;
    NiceMock<SourcePathCacheMockWithPaths> pathCacheMock{"/path/model.qml"};
    NiceMock<ProjectStorageMockWithQtQuick> projectStorageMock{pathCacheMock.sourceId, "/path"};
    QmlDesigner::Model model{{projectStorageMock, pathCacheMock},
                             "QtObject",
                             {Import::createLibraryImport("QML"),
                              Import::createLibraryImport("QtQuick")},
                             QUrl::fromLocalFile(pathCacheMock.path.toQString())};
};

INSTANTIATE_TEST_SUITE_P(DesignSystem,
                         DesignSystemQmlTest,
                         testing::Values(QmlDesigner::GroupType::Colors,
                                         QmlDesigner::GroupType::Flags,
                                         QmlDesigner::GroupType::Numbers,
                                         QmlDesigner::GroupType::Strings));

TEST_P(DesignSystemQmlTest, test_group_aliases)
{
    // arrange
    ThemeProperty testProp{P1, "test", false};
    DSThemeManager mgr;
    mgr.addTheme(DarkTheme);
    mgr.addProperty(groupType, testProp);
    ModelNode rootNode = model.rootModelNode();
    QString binding = QString("currentTheme.%1").arg(QString::fromLatin1(groupName));

    // act
    mgr.decorate(rootNode);

    // assert
    ASSERT_THAT(rootNode,
                AllOf(Property(&ModelNode::type, Eq("QtObject")),
                      HasBindingProperty(groupName, binding),
                      HasBindingProperty("currentTheme", DarkTheme),
                      HasNodeProperty(DarkTheme, "QtObject")));
}

TEST_P(DesignSystemQmlTest, test_empty_group_aliases)
{
    // arrange
    DSThemeManager mgr;
    ModelNode rootNode = model.rootModelNode();
    QString binding = QString("currentTheme.%1").arg(QString::fromLatin1(groupName));

    // act
    mgr.decorate(rootNode);

    // assert
    ASSERT_THAT(rootNode,
                AllOf(Property(&ModelNode::type, Eq("QtObject")),
                      Not(HasBindingProperty(groupName, binding)),
                      Not(HasBindingProperty("currentTheme", DarkTheme)),
                      Not(HasNodeProperty(DarkTheme, "QtObject"))));
}

TEST_P(DesignSystemQmlTest, test_group_binding_property)
{
    // arrange
    ThemeProperty testProp{P1, "width", true};
    group.addProperty(Theme1, testProp);
    ModelNode rootNode = model.rootModelNode();

    // act
    group.decorate(Theme1, rootNode);

    // assert
    ASSERT_THAT(rootNode,
                AllOf(HasNodeProperty(groupName, "QtObject"),
                      HasGroupBindingProperty(groupName, testProp)));
}

TEST_P(DesignSystemQmlTest, test_group_binding_property_mcu)
{
    // arrange
    ThemeProperty testProp{P1, "width", true};
    group.addProperty(Theme1, testProp);
    ModelNode rootNode = model.rootModelNode();

    // act
    group.decorate(Theme1, rootNode, false);

    // assert
    ASSERT_THAT(rootNode,
                AllOf(Not(HasNodeProperty(groupName, "QtObject")),
                      HasBindingProperty(testProp.name, testProp.value)));
}

TEST_P(DesignSystemQmlTest, test_group_variant_property)
{
    // arrange
    ThemeProperty testProp{P1, 5, false};
    group.addProperty(Theme1, testProp);
    ModelNode rootNode = model.rootModelNode();

    // act
    group.decorate(Theme1, rootNode);

    // assert
    ASSERT_THAT(rootNode,
                AllOf(HasNodeProperty(groupName, "QtObject"),
                      HasGroupVariantProperty(groupName, testProp)));
}

TEST_P(DesignSystemQmlTest, test_group_variant_property_mcu)
{
    // arrange
    ThemeProperty testProp{P1, 5, false};
    group.addProperty(Theme1, testProp);
    ModelNode rootNode = model.rootModelNode();

    // act
    group.decorate(Theme1, rootNode, false);

    // assert
    ASSERT_THAT(rootNode,
                AllOf(Not(HasNodeProperty(groupName, "QtObject")),
                      HasVariantProperty(testProp.name, testProp.value)));
}

TEST_P(DesignSystemQmlTest, test_group_no_property)
{
    // arrange
    ModelNode rootNode = model.rootModelNode();

    // act
    group.decorate(Theme1, rootNode);

    // assert
    ASSERT_THAT(rootNode, Not(HasNodeProperty(groupName, "QtObject")));
}
