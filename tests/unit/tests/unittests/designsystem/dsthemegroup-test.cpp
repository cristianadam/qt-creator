// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "../utils/googletest.h"

#include <designsystem/dsthemegroup.h>

using QmlDesigner::DSThemeGroup;
using QmlDesigner::ThemeProperty;

using ::testing::AllOf;
using ::testing::Not;

namespace QmlDesigner {
std::ostream &operator<<(std::ostream &out, const ThemeProperty &prop)
{
    out << "{name: " << prop.name.toStdString() << ", value: " << prop.value
        << ", isBinding: " << prop.isBinding << "}";

    return out;
}

void PrintTo(const ThemeProperty &prop, std::ostream *os)
{
    *os << prop;
}

std::ostream &operator<<(std::ostream &out, GroupType group)
{
    out << "ThemeGroup{ " << static_cast<int>(group) << ", " << GroupId(group) << "}";
    return out;
}

void PrintTo(GroupType group, std::ostream *os)
{
    *os << group;
}

} // namespace QmlDesigner

namespace {
constexpr const char P1[] = "prop1";
constexpr const char P2[] = "prop2";

constexpr QmlDesigner::ThemeId Theme1 = 0;
constexpr QmlDesigner::ThemeId Theme2 = 1;
} // namespace

MATCHER_P3(HasPropertyCount, themeId, themePropCount, totalPropsCount, "")
{
    const DSThemeGroup &group = arg;
    return group.count() == totalPropsCount && group.count(themeId) == themePropCount;
}

MATCHER_P2(HasThemeProperty, themeId, themeProp, "")
{
    const DSThemeGroup &group = arg;
    const std::optional<ThemeProperty> prop = group.propertyValue(themeId, themeProp.name);

    return prop && themeProp.name == prop->name && themeProp.value == prop->value
           && themeProp.isBinding == prop->isBinding;
}

class DesignGroupTest : public testing::TestWithParam<QmlDesigner::GroupType>
{
protected:
    DesignGroupTest()
        : group(groupType)
    {
    }

    QmlDesigner::GroupType groupType = GetParam();
    QmlDesigner::DSThemeGroup group;
};

INSTANTIATE_TEST_SUITE_P(DesignSystem,
                         DesignGroupTest,
                         testing::Values(QmlDesigner::GroupType::Colors,
                                         QmlDesigner::GroupType::Flags,
                                         QmlDesigner::GroupType::Numbers,
                                         QmlDesigner::GroupType::Strings));

TEST_P(DesignGroupTest, add_property)
{
    // arrange
    ThemeProperty testProp{P1, "test", false};

    // act
    group.addProperty(Theme1, testProp);

    //assert
    ASSERT_THAT(group, AllOf(HasPropertyCount(Theme1, 1u, 1u), HasThemeProperty(Theme1, testProp)));
}

TEST_P(DesignGroupTest, add_property_multiple_theme)
{
    // arrange
    ThemeProperty testPropP1{P1, "#aaccff", false};
    ThemeProperty testPropP2{P2, "#bbddee", false};

    // act
    group.addProperty(Theme1, testPropP1);
    group.addProperty(Theme2, testPropP2);

    //assert
    ASSERT_THAT(group,
                AllOf(HasPropertyCount(Theme1, 1u, 2u),
                      HasThemeProperty(Theme1, testPropP1),
                      HasPropertyCount(Theme2, 1u, 2u),
                      HasThemeProperty(Theme2, testPropP2)));
}

TEST_P(DesignGroupTest, add_empty_property_name)
{
    // arrange
    ThemeProperty testProp{"", "test", false};

    // act
    group.addProperty(Theme1, testProp);

    //assert
    ASSERT_THAT(group,
                AllOf(HasPropertyCount(Theme1, 0u, 0u), Not(HasThemeProperty(Theme1, testProp))));
}

TEST_P(DesignGroupTest, add_binding_property)
{
    // arrange
    ThemeProperty testProp{P1, "root.width", true};

    // act
    group.addProperty(Theme1, testProp);

    //assert
    ASSERT_THAT(group, AllOf(HasPropertyCount(Theme1, 1u, 1u), HasThemeProperty(Theme1, testProp)));
}

TEST_P(DesignGroupTest, add_duplicate_properties)
{
    // arrange
    ThemeProperty testPropA{P1, "#aaccff", false};
    ThemeProperty testPropB{P1, "#bbddee", false};
    group.addProperty(Theme1, testPropA);

    // act
    group.addProperty(Theme1, testPropB);

    //assert
    ASSERT_THAT(group,
                AllOf(HasPropertyCount(Theme1, 1u, 1u),
                      HasThemeProperty(Theme1, testPropA),
                      Not(HasThemeProperty(Theme1, testPropB))));
}

TEST_P(DesignGroupTest, remove_property)
{
    // arrange
    ThemeProperty testProp{P1, "#aaccff", false};
    group.addProperty(Theme1, testProp);

    // act
    group.removeProperty(P1);

    //assert
    ASSERT_THAT(group,
                AllOf(HasPropertyCount(Theme1, 0u, 0u), Not(HasThemeProperty(Theme1, testProp))));
}

TEST_P(DesignGroupTest, remove_absent_property)
{
    // arrange
    ThemeProperty testPropP1{P1, "#aaccff", false};
    group.addProperty(Theme1, testPropP1);

    // act
    group.removeProperty(P2);

    //assert
    ASSERT_THAT(group, AllOf(HasPropertyCount(Theme1, 1u, 1u), HasThemeProperty(Theme1, testPropP1)));
}

TEST_P(DesignGroupTest, remove_theme_no_leftover)
{
    // arrange
    ThemeProperty testPropP1{P1, "#aaccff", false};
    group.addProperty(Theme1, testPropP1);
    ThemeProperty testPropP2{P2, "#bbddee", false};
    group.addProperty(Theme1, testPropP2);

    // act
    group.removeTheme(Theme1);

    //assert
    ASSERT_THAT(group,
                AllOf(HasPropertyCount(Theme1, 0u, 0u),
                      Not(HasThemeProperty(Theme1, testPropP1)),
                      Not(HasThemeProperty(Theme1, testPropP2))));
}

TEST_P(DesignGroupTest, remove_theme_with_leftover)
{
    // arrange
    ThemeProperty testPropP1{P1, "#aaccff", false};
    group.addProperty(Theme1, testPropP1);
    ThemeProperty testPropP2{P2, "#bbddee", false};
    group.addProperty(Theme2, testPropP2);

    // act
    group.removeTheme(Theme1);

    //assert
    ASSERT_THAT(group,
                AllOf(HasPropertyCount(Theme1, 0u, 1u),
                      Not(HasThemeProperty(Theme1, testPropP1)),
                      HasThemeProperty(Theme2, testPropP2)));
}

TEST_P(DesignGroupTest, remove_absent_theme)
{
    // arrange
    ThemeProperty testPropP1{P1, "#aaccff", false};
    group.addProperty(Theme1, testPropP1);

    // act
    group.removeTheme(Theme2);

    //assert
    ASSERT_THAT(group, AllOf(HasPropertyCount(Theme1, 1u, 1u), HasThemeProperty(Theme1, testPropP1)));
}

TEST_P(DesignGroupTest, duplicate_theme)
{
    // arrange
    ThemeProperty testPropP1{P1, "#aaccff", false};
    group.addProperty(Theme1, testPropP1);
    ThemeProperty testPropP2{P2, "#bbddee", false};
    group.addProperty(Theme1, testPropP2);

    // act
    group.duplicateValues(Theme1, Theme2);

    //assert
    ASSERT_THAT(group,
                AllOf(HasPropertyCount(Theme2, 2u, 2u),
                      HasThemeProperty(Theme2, testPropP1),
                      HasThemeProperty(Theme2, testPropP2)));
}

TEST_P(DesignGroupTest, duplicate_absent_theme)
{
    // arrange
    ThemeProperty testPropP1{P1, "#aaccff", false};
    group.addProperty(Theme1, testPropP1);

    // act
    group.duplicateValues(Theme2, Theme1);

    //assert
    ASSERT_THAT(group, AllOf(HasPropertyCount(Theme1, 1u, 1u), HasThemeProperty(Theme1, testPropP1)));
}
