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
const ZenModePluginCore::ModeStyle MODES_STATE_ON_ACTIVE_ZENMODE{ZenModePluginCore::ModeStyle::Hidden};

const std::vector<Utils::Id> TOGGLE_MODES_STATES_COMMANDS = {
    "QtCreator.Modes.Hidden",
    "QtCreator.Modes.IconsOnly",
    "QtCreator.Modes.IconsAndText"
};

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
    restoreModeSidebar();
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

    m_toggleModesStatesActions.resize(3);
    for (int i = 0; i < TOGGLE_MODES_STATES_COMMANDS.size(); i++)
    {
        if (const Core::Command* cmd = Core::ActionManager::command(TOGGLE_MODES_STATES_COMMANDS[i]))
        {
            m_toggleModesStatesActions[i] = cmd->action();
            if (cmd->action()->isChecked())
            {
                m_prevModesSidebarState = (ModeStyle)i;
            }
        }
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

void ZenModePluginCore::hideModeSidebar()
{
    for (int i = 0; i < m_toggleModesStatesActions.size(); i++)
    {
        auto action = m_toggleModesStatesActions[i];
        if (action && action->isChecked())
        {
            m_prevModesSidebarState = (ModeStyle)i;
        }
    }
    auto action = m_toggleModesStatesActions[MODES_STATE_ON_ACTIVE_ZENMODE];
    if (action && !action->isChecked())
    {
        action->trigger();
    }
}

void ZenModePluginCore::restoreModeSidebar()
{
    if (m_prevModesSidebarState >= ModeStyle::Hidden && m_prevModesSidebarState <= ModeStyle::IconsAndText)
    {
        auto action = m_toggleModesStatesActions[m_prevModesSidebarState];
        if (action && !action->isChecked()) {
            action->trigger();
        }
    }
}

void ZenModePluginCore::setSidebarsModesVisibility(bool _visible)
{
    if (_visible)
    {
        hideOutputPanes();
        hideSidebars();
        hideModeSidebar();
    } else {
        restoreSidebars();
        restoreModeSidebar();
    }
}

void ZenModePluginCore::toggleDistractionFreeMode()
{
    m_distractionFreeModeActive = !m_distractionFreeModeActive;

    setSidebarsModesVisibility(m_distractionFreeModeActive);
}
} // namespace ZenModePlugin::Internal

#include <zenmodeplugin.moc>
