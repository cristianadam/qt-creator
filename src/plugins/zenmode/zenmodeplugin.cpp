#include "zenmodepluginconstants.h"
#include "zenmodeplugin.h"
#include "zenmodeplugintr.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>

#include <QAction>
#include <QMenu>

using namespace Core;

namespace ZenModePlugin::Internal {


const Utils::Id LEFT_SIDEBAR_COMMAND_ID{"QtCreator.ToggleLeftSidebar"};
const Utils::Id RIGHT_SIDEBAR_COMMAND_ID{"QtCreator.ToggleRightSidebar"};
const Utils::Id OUTPUT_PANE_COMMAND_ID{"QtCreator.Pane.GeneralMessages"};

ZenModePluginCore::~ZenModePluginCore()
{ }

void ZenModePluginCore::initialize()
{
    ActionContainer *menu = ActionManager::createMenu(Constants::MENU_ID);
    menu->menu()->setTitle(Tr::tr("Zen Mode"));
    ActionManager::actionContainer(Core::Constants::M_TOOLS)->addMenu(menu);

    ActionBuilder(this, Constants::DISTRACTION_FREE_ACTION_ID)
        .addToContainer(Constants::MENU_ID)
        .setText(Tr::tr("Toogle Distraction Free Mode"))
        .setDefaultKeySequence(Tr::tr("Shift+Escape"))
        .addOnTriggered(this, &ZenModePluginCore::toggleDistractionFreeMode);
}

void ZenModePluginCore::extensionsInitialized()
{ }

bool ZenModePluginCore::delayedInitialize()
{
    getActions();
    return true;
}

ZenModePluginCore::ShutdownFlag ZenModePluginCore::aboutToShutdown()
{
    restoreSidebars();
    return SynchronousShutdown;
}

void ZenModePluginCore::getActions()
{
    if (const Core::Command* cmd = Core::ActionManager::command(OUTPUT_PANE_COMMAND_ID))
    {
        m_outputPaneAction = cmd->action();
    } else {
        qWarning() << "ZenModePlugin - fail to get" <<  OUTPUT_PANE_COMMAND_ID.toString() << "action";
    }

    if (const Core::Command* cmd = Core::ActionManager::command(LEFT_SIDEBAR_COMMAND_ID)) {
        m_toggleLeftSidebarAction = cmd->action();
    } else {
        qWarning() << "ZenModePlugin - fail to get" <<  LEFT_SIDEBAR_COMMAND_ID.toString() << "action";
    }

    if (const Core::Command* cmd = Core::ActionManager::command(RIGHT_SIDEBAR_COMMAND_ID))
    {
        m_toggleRightSidebarAction = cmd->action();
    } else {
        qWarning() << "ZenModePlugin - fail to get" <<  RIGHT_SIDEBAR_COMMAND_ID.toString() << "action";
    }
}

void ZenModePluginCore::hideOutputPanes()
{
    if (m_outputPaneAction)
    {
        m_outputPaneAction->trigger();
        m_outputPaneAction->trigger();
    }
}

void ZenModePluginCore::hideSidebars()
{
    if (m_toggleLeftSidebarAction)
    {
        m_prevLeftSidebarState = m_toggleLeftSidebarAction->isChecked();
       if (m_prevLeftSidebarState)
       {
           m_toggleLeftSidebarAction->trigger();
       }
    }

    if (m_toggleRightSidebarAction)
    {
        m_prevRightSidebarState = m_toggleRightSidebarAction->isChecked();
        if(m_prevRightSidebarState)
        {
            m_toggleRightSidebarAction->trigger();
        }
    }
}

void ZenModePluginCore::restoreSidebars()
{
    if (m_toggleLeftSidebarAction &&
        !m_toggleLeftSidebarAction->isChecked() &&
        m_prevLeftSidebarState)
    {
        m_prevLeftSidebarState = false;
        m_toggleLeftSidebarAction->trigger();
    }
    if (m_toggleRightSidebarAction &&
        !m_toggleRightSidebarAction->isChecked() &&
        m_prevRightSidebarState )
    {
        m_prevRightSidebarState = false;
        m_toggleRightSidebarAction->trigger();
    }
}

void ZenModePluginCore::toggleDistractionFreeMode()
{
    m_distractionFreeModeActive = !m_distractionFreeModeActive;
    if (m_distractionFreeModeActive)
    {
        hideOutputPanes();
        hideSidebars();
    } else {
        restoreSidebars();
    }
}
} // namespace ZenModePlugin::Internal

#include <zenmodeplugin.moc>
