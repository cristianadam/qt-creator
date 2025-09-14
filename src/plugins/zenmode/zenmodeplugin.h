#ifndef ZENMODEPLUGINPLUGIN_H
#define ZENMODEPLUGINPLUGIN_H

#include <extensionsystem/iplugin.h>

#include <QAction>
#include <QMenu>

namespace ZenModePlugin::Internal {

class ZenModePluginCore final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "ZenModePlugin.json")

public:
    ZenModePluginCore() = default;

    ~ZenModePluginCore() final;

    void initialize() final;
    void extensionsInitialized() final;
    ShutdownFlag aboutToShutdown() final;

private:
    void toggleDistractionFreeMode();

private:
    bool m_distractionFreeModeActive{0};
};

} // namespace ZenModePlugin::Internal

#endif // ZENMODEPLUGINPLUGIN_H
