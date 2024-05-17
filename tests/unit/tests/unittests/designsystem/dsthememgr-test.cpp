// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "../utils/googletest.h"

#include <bindingproperty.h>
#include <model.h>
#include <nodeproperty.h>
#include <projectstoragemock.h>
#include <sourcepathcachemock.h>

#include <designsystem/dsthememanager.h>

namespace {
constexpr const char P1[] = "prop1";
constexpr const char P2[] = "prop2";

constexpr const char DarkTheme[] = "dark";
constexpr const char LightTheme[] = "light";
} // namespace

using QmlDesigner::DSThemeManager;
using QmlDesigner::GroupType;
using QmlDesigner::Import;
using QmlDesigner::ModelNode;
using QmlDesigner::ThemeProperty;

using ::testing::AllOf;
using ::testing::Not;

MATCHER_P3(HasProperty, themeId, group, themeProp, "")
{
    const DSThemeManager &mgr = arg;
    const std::optional<ThemeProperty> prop = mgr.property(themeId, group, themeProp.name);

    return prop && themeProp.name == prop->name && themeProp.value == prop->value
           && themeProp.isBinding == prop->isBinding;
}

class DesignSystemManagerTest : public testing::TestWithParam<QmlDesigner::GroupType>
{
protected:
    QmlDesigner::GroupType groupType = GetParam();
    DSThemeManager mgr;
};

INSTANTIATE_TEST_SUITE_P(DesignSystem,
                         DesignSystemManagerTest,
                         testing::Values(QmlDesigner::GroupType::Colors,
                                         QmlDesigner::GroupType::Flags,
                                         QmlDesigner::GroupType::Numbers,
                                         QmlDesigner::GroupType::Strings));

TEST(DesignSystemManagerTest, add_theme)
{
    // arrange
    DSThemeManager mgr;

    // act
    const auto themeId = mgr.addTheme(DarkTheme);

    // assert
    ASSERT_THAT(themeId, Optional(A<QmlDesigner::ThemeId>()));
}

TEST(DesignSystemManagerTest, add_theme_empty_name)
{
    // arrange
    DSThemeManager mgr;

    // act
    const auto themeId = mgr.addTheme("");

    // assert
    ASSERT_THAT(themeId, Eq(std::nullopt));
}

TEST(DesignSystemManagerTest, get_theme_id)
{
    // arrange
    DSThemeManager mgr;

    // act
    const auto themeId = mgr.addTheme(DarkTheme);

    // assert
    ASSERT_THAT(mgr.themeId(DarkTheme), Optional(themeId));
}

TEST(DesignSystemManagerTest, remove_theme)
{
    // arrange
    DSThemeManager mgr;
    const auto themeId = mgr.addTheme(DarkTheme);

    // act
    mgr.removeTheme(*themeId);

    // assert
    ASSERT_THAT(mgr, Property(&DSThemeManager::themeCount, 0));
}

TEST(DesignSystemManagerTest, remove_theme_with_properties)
{
    // arrange
    DSThemeManager mgr;
    const auto themeId = mgr.addTheme(DarkTheme);
    ThemeProperty testProp{P1, "#aaccbb", false};
    mgr.addProperty(GroupType::Colors, testProp);

    // act
    mgr.removeTheme(*themeId);

    // assert
    ASSERT_THAT(mgr,
                AllOf(Property(&DSThemeManager::themeCount, 0),
                      Not(HasProperty(*themeId, GroupType::Colors, testProp))));
}

TEST_P(DesignSystemManagerTest, add_property_without_theme)
{
    // arrange
    ThemeProperty testProp{P1, "test", false};

    // act
    mgr.addProperty(groupType, testProp);

    //assert
    ASSERT_THAT(mgr, Property(&DSThemeManager::themeCount, 0));
}

TEST_P(DesignSystemManagerTest, add_property)
{
    // arrange
    ThemeProperty testProp{P1, "test", false};
    const auto themeId = mgr.addTheme(DarkTheme);

    // act
    mgr.addProperty(groupType, testProp);

    // assert
    ASSERT_THAT(mgr,
                AllOf(Property(&DSThemeManager::themeCount, 1),
                      HasProperty(*themeId, groupType, testProp)));
}

TEST_P(DesignSystemManagerTest, add_invalid_property)
{
    // arrange
    ThemeProperty testProp{P1, {}, false};
    const auto themeId = mgr.addTheme(DarkTheme);

    // act
    mgr.addProperty(groupType, testProp);

    // assert
    ASSERT_THAT(mgr,
                AllOf(Property(&DSThemeManager::themeCount, 1),
                      Not(HasProperty(*themeId, groupType, testProp))));
}

TEST_P(DesignSystemManagerTest, add_property_multiple_themes)
{
    // arrange
    ThemeProperty testProp{P1, "test", false};
    const auto themeIdDark = mgr.addTheme(DarkTheme);
    const auto themeIdLight = mgr.addTheme(LightTheme);

    // act
    mgr.addProperty(groupType, testProp);

    // assert
    ASSERT_THAT(mgr,
                AllOf(Property(&DSThemeManager::themeCount, 2),
                      HasProperty(*themeIdDark, groupType, testProp),
                      HasProperty(*themeIdLight, groupType, testProp)));
}

TEST_P(DesignSystemManagerTest, update_property_value)
{
    // arrange
    ThemeProperty testProp{P1, "test", false};
    ThemeProperty testPropUpdated{P1, "foo", false};
    const auto themeId = mgr.addTheme(DarkTheme);
    mgr.addProperty(groupType, testProp);

    // act
    mgr.updateProperty(*themeId, groupType, testPropUpdated);

    // assert
    ASSERT_THAT(mgr,
                AllOf(Property(&DSThemeManager::themeCount, 1),
                      HasProperty(*themeId, groupType, testPropUpdated)));
}

TEST_P(DesignSystemManagerTest, update_property_name)
{
    // arrange
    ThemeProperty testProp{P1, "test", false};
    ThemeProperty testPropUpdated{P2, "test", false};
    const auto themeId = mgr.addTheme(DarkTheme);
    mgr.addProperty(groupType, testProp);

    // act
    mgr.updateProperty(*themeId, groupType, testProp, testPropUpdated.name);

    // assert
    ASSERT_THAT(mgr,
                AllOf(Property(&DSThemeManager::themeCount, 1),
                      HasProperty(*themeId, groupType, testPropUpdated)));
}

TEST_P(DesignSystemManagerTest, update_property_invalid)
{
    // arrange
    ThemeProperty testProp{P1, "test", false};
    ThemeProperty testPropUpdated{P1, {}, false};
    const auto themeId = mgr.addTheme(DarkTheme);
    mgr.addProperty(groupType, testProp);

    // act
    mgr.updateProperty(*themeId, groupType, testProp, testPropUpdated.name);

    // assert
    ASSERT_THAT(mgr,
                AllOf(Property(&DSThemeManager::themeCount, 1),
                      HasProperty(*themeId, groupType, testProp)));
}

TEST_P(DesignSystemManagerTest, remove_property)
{
    // arrange
    ThemeProperty testProp{P1, "test", false};
    const auto themeId = mgr.addTheme(DarkTheme);
    mgr.addProperty(groupType, testProp);

    // act
    mgr.removeProperty(groupType, P1);

    // assert
    ASSERT_THAT(mgr,
                AllOf(Property(&DSThemeManager::themeCount, 1),
                      Not(HasProperty(*themeId, groupType, testProp))));
}

TEST_P(DesignSystemManagerTest, remove_absent_property)
{
    // arrange
    ThemeProperty testProp{P1, "test", false};
    const auto themeId = mgr.addTheme(DarkTheme);
    mgr.addProperty(groupType, testProp);

    // act
    mgr.removeProperty(groupType, P2);

    // assert
    ASSERT_THAT(mgr,
                AllOf(Property(&DSThemeManager::themeCount, 1),
                      HasProperty(*themeId, groupType, testProp)));
}
