// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "copilotplugin.h"

#include "copilotclient.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/icore.h>
#include <coreplugin/imode.h>
#include <coreplugin/modemanager.h>

#include <QAction>
#include <QDebug>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>

namespace CoPilot {
namespace Internal {

/*!  A mode with a push button based on BaseMode.  */

/*! Constructs the Hello World plugin. Normally plugins don't do anything in
    their constructor except for initializing their member variables. The
    actual work is done later, in the initialize() and extensionsInitialized()
    functions.
*/
CoPilotPlugin::CoPilotPlugin() {}

/*! Plugins are responsible for deleting objects they created on the heap, and
    to unregister objects from the plugin manager that they registered there.
*/
CoPilotPlugin::~CoPilotPlugin() {}

/*! Initializes the plugin. Returns true on success.
    Plugins want to register objects with the plugin manager here.

    \a errorMessage can be used to pass an error message to the plugin system,
       if there was any.
*/
bool CoPilotPlugin::initialize(const QStringList &arguments, QString *errorMessage)
{
    Q_UNUSED(arguments);
    Q_UNUSED(errorMessage);

    new CoPilotClient();

    /*    // Create a unique context for our own view, that will be used for the
    // menu entry later.
    Core::Context context("CoPilot.MainView");

    // Create an action to be triggered by a menu entry
    auto CoPilotAction = new QAction(tr("Say \"&Hello World!\""), this);
    connect(CoPilotAction, &QAction::triggered, this, &CoPilotPlugin::sayCoPilot);

    // Register the action with the action manager
    Core::Command *command =
            Core::ActionManager::registerAction(
                    CoPilotAction, "CoPilot.CoPilotAction", context);

    // Create our own menu to place in the Tools menu
    Core::ActionContainer *CoPilotMenu =
            Core::ActionManager::createMenu("CoPilot.CoPilotMenu");
    QMenu *menu = CoPilotMenu->menu();
    menu->setTitle(tr("&Hello World"));
    menu->setEnabled(true);

    // Add the Hello World action command to the menu
    CoPilotMenu->addAction(command);

    // Request the Tools menu and add the Hello World menu to it
    Core::ActionContainer *toolsMenu =
            Core::ActionManager::actionContainer(Core::Constants::M_TOOLS);
    toolsMenu->addMenu(CoPilotMenu);

    // Add a mode with a push button based on BaseMode.
    m_helloMode = new HelloMode;
*/
    return true;
}

/*! Notification that all extensions that this plugin depends on have been
    initialized. The dependencies are defined in the plugins .json(.in) file.

    Normally this function is used for things that rely on other plugins to have
    added objects to the plugin manager, that implement interfaces that we're
    interested in. These objects can now be requested through the
    PluginManager.

    The CoPilotPlugin doesn't need things from other plugins, so it does
    nothing here.
*/
void CoPilotPlugin::extensionsInitialized() {}

/*
void CoPilotPlugin::sayCoPilot()
{
    // When passing nullptr for the parent, the message box becomes an
    // application-global modal dialog box
    QMessageBox::information(
            nullptr, tr("Hello World!"), tr("Hello World! Beautiful day today, isn't it?"));
}*/

} // namespace Internal
} // namespace CoPilot
