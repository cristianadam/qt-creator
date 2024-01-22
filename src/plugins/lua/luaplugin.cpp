// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "luaapiregistry.h"
#include "luapluginloader.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/icore.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <utils/algorithm.h>

#include <QAction>
#include <QDebug>
#include <QMenu>

namespace Lua::Internal {

void registerUiBindings();
void registerAspectBindings();
void registerLayoutingBindings();
void registerSettingsBindings();

class LuaPlugin : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "Lua.json")

public:
    LuaPlugin() = default;
    ~LuaPlugin() override = default;

    void initialize() final
    {
        LuaApiRegistry::registerUtils();
        LuaApiRegistry::registerFetch();
        LuaApiRegistry::registerWait();
        LuaApiRegistry::registerProcess();

        registerUiBindings();
        registerAspectBindings();
        registerLayoutingBindings();
        registerSettingsBindings();
    }

    bool delayedInitialize() final
    {
        LuaPluginLoader::instance().scan(
            Utils::transform(ExtensionSystem::PluginManager::pluginPaths(),
                             [](const QString &path) -> QString { return path + "/lua-plugins/"; }));

        return true;
    }
};

} // namespace Lua::Internal

#include "luaplugin.moc"
