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

ZenModePluginCore::ShutdownFlag ZenModePluginCore::aboutToShutdown()
{
    return SynchronousShutdown;
}

void ZenModePluginCore::toggleDistractionFreeMode()
{
    m_distractionFreeModeActive = !m_distractionFreeModeActive;
}

} // namespace ZenModePlugin::Internal

#include <zenmodeplugin.moc>
