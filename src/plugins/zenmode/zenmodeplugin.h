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
    bool delayedInitialize() override;

private:
    void getActions();

    void toggleDistractionFreeMode();

    void hideOutputPanes();
    void hideSidebars();
    void restoreSidebars();

private:
    bool m_distractionFreeModeActive{0};

    QPointer<QAction> m_outputPaneAction;
    QPointer<QAction> m_toggleLeftSidebarAction;
    QPointer<QAction> m_toggleRightSidebarAction;
    bool m_prevLeftSidebarState{false};
    bool m_prevRightSidebarState{false};
};

} // namespace ZenModePlugin::Internal

#endif // ZENMODEPLUGINPLUGIN_H
