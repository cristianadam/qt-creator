// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "luaapiregistry.h"

#include <coreplugin/dialogs/ioptionspage.h>

using namespace Utils;

namespace Lua::Internal {

void registerSettingsBindings()
{
    class OptionsPage : public Core::IOptionsPage
    {
    public:
        OptionsPage(const sol::table &options)
        {
            setId(Id::fromString(options.get<QString>("id")));
            setDisplayName(options.get<QString>("displayName"));
            setCategory(Id::fromString(options.get<QString>("categoryId")));
            setDisplayCategory(options.get<QString>("displayCategory"));
            setCategoryIconPath(FilePath::fromUserInput(options.get<QString>("categoryIconPath")));
            AspectContainer *container = options.get<AspectContainer *>("aspectContainer");
            setSettingsProvider([container]() { return container; });
        }
    };

    LuaApiRegistry::createFunction(
        [](const sol::table &options) {
            std::unique_ptr<OptionsPage> page = std::make_unique<OptionsPage>(options);
            return page;
        },
        "createOptionsPage");

    /*

class CopilotSettingsPage : public Core::IOptionsPage
{
public:
    CopilotSettingsPage()
    {
        setId(Constants::COPILOT_GENERAL_OPTIONS_ID);
        setDisplayName("Copilot");
        setCategory(Constants::COPILOT_GENERAL_OPTIONS_CATEGORY);
        setDisplayCategory(Constants::COPILOT_GENERAL_OPTIONS_DISPLAY_CATEGORY);
        setCategoryIconPath(":/copilot/images/settingscategory_copilot.png");
        setSettingsProvider([] { return &settings(); });
    }
};

*/
}

} // namespace Lua::Internal