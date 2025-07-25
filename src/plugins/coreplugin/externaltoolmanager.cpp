// Copyright (C) 2016 Digia Plc and/or its subsidiary(-ies).
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "externaltoolmanager.h"

#include "externaltool.h"
#include "coreconstants.h"
#include "coreplugintr.h"
#include "icore.h"
#include "messagemanager.h"
#include "actionmanager/actionmanager.h"
#include "actionmanager/actioncontainer.h"
#include "actionmanager/command.h"

#include <utils/qtcassert.h>

#include <QAction>
#include <QDebug>
#include <QMenu>

using namespace Core::Internal;
using namespace Utils;

namespace Core {

const char kSpecialUncategorizedSetting[] = "SpecialEmptyCategoryForUncategorizedTools";

struct ExternalToolManagerPrivate
{
    QMap<QString, ExternalTool *> m_tools;
    QMap<QString, QList<ExternalTool *> > m_categoryMap;
    QMap<QString, QAction *> m_actions;
    QMap<QString, ActionContainer *> m_containers;
    QAction *m_configureSeparator;
    QAction *m_configureAction;
};

static ExternalToolManager *m_instance = nullptr;
static ExternalToolManagerPrivate *d = nullptr;

static void writeSettings();

static void readSettings(const QMap<QString, ExternalTool *> &tools,
                         QMap<QString, QList<ExternalTool*> > *categoryPriorityMap);

static void parseDirectory(const FilePath &directory,
                         QMap<QString, QMultiMap<int, ExternalTool*> > *categoryMenus,
                         QMap<QString, ExternalTool *> *tools,
                         bool isPreset = false);

ExternalToolManager::ExternalToolManager()
    : QObject(ICore::instance())
{
    m_instance = this;
    d = new ExternalToolManagerPrivate;

    d->m_configureSeparator = new QAction(this);
    d->m_configureSeparator->setSeparator(true);
    d->m_configureAction = new QAction(ICore::msgShowOptionsDialog(), this);
    connect(d->m_configureAction, &QAction::triggered, this, [] {
        ICore::showOptionsDialog(Constants::SETTINGS_ID_TOOLS);
    });

    // add the external tools menu
    ActionContainer *mexternaltools = ActionManager::createMenu(Id(Constants::M_TOOLS_EXTERNAL));
    mexternaltools->menu()->setTitle(Tr::tr("&External"));
    ActionContainer *mtools = ActionManager::actionContainer(Constants::M_TOOLS);
    mtools->addMenu(mexternaltools, Constants::G_DEFAULT_THREE);

    QMap<QString, QMultiMap<int, ExternalTool*> > categoryPriorityMap;
    QMap<QString, ExternalTool *> tools;
    parseDirectory(ICore::userResourcePath("externaltools"),
                   &categoryPriorityMap,
                   &tools);
    parseDirectory(ICore::resourcePath("externaltools"),
                   &categoryPriorityMap,
                   &tools,
                   true);

    QMap<QString, QList<ExternalTool *> > categoryMap;
    for (auto it = categoryPriorityMap.cbegin(), end = categoryPriorityMap.cend(); it != end; ++it)
        categoryMap.insert(it.key(), it.value().values());

    // read renamed categories and custom order
    readSettings(tools, &categoryMap);
    setToolsByCategory(categoryMap);
}

ExternalToolManager::~ExternalToolManager()
{
    writeSettings();
    // TODO kill running tools
    qDeleteAll(d->m_tools);
    delete d;
}

ExternalToolManager *ExternalToolManager::instance()
{
    return m_instance;
}

static void parseDirectory(const FilePath &directory,
                           QMap<QString, QMultiMap<int, ExternalTool *>> *categoryMenus,
                           QMap<QString, ExternalTool *> *tools,
                           bool isPreset)
{
    QTC_ASSERT(categoryMenus, return);
    QTC_ASSERT(tools, return);
    const FilePaths filePaths = directory.dirEntries({{"*.xml"}, QDir::Files | QDir::Readable});
    for (const FilePath &filePath : filePaths) {
        Result<ExternalTool *> res =
            ExternalTool::createFromFile(filePath, ICore::userInterfaceLanguage());
        if (!res) {
            qWarning() << Tr::tr("Error while parsing external tool %1: %2")
                .arg(filePath.toUserOutput(), res.error());
            continue;
        }
        ExternalTool *tool = res.value();
        if (tools->contains(tool->id())) {
            if (isPreset) {
                // preset that was changed
                ExternalTool *other = tools->value(tool->id());
                other->setPreset(std::shared_ptr<ExternalTool>(tool));
            } else {
                qWarning() << Tr::tr("Error: External tool in %1 has duplicate id")
                    .arg(filePath.toUserOutput());
                delete tool;
            }
            continue;
        }
        if (isPreset) {
            // preset that wasn't changed --> save original values
            tool->setPreset(std::shared_ptr<ExternalTool>(new ExternalTool(tool)));
        }
        tools->insert(tool->id(), tool);
        (*categoryMenus)[tool->displayCategory()].insert(tool->order(), tool);
    }
}

QMap<QString, QList<ExternalTool *> > ExternalToolManager::toolsByCategory()
{
    return d->m_categoryMap;
}

QMap<QString, ExternalTool *> ExternalToolManager::toolsById()
{
    return d->m_tools;
}

void ExternalToolManager::setToolsByCategory(const QMap<QString, QList<ExternalTool *> > &tools)
{
    // clear menu
    ActionContainer *mexternaltools = ActionManager::actionContainer(Id(Constants::M_TOOLS_EXTERNAL));
    mexternaltools->clear();

    // delete old tools and create list of new ones
    QMap<QString, ExternalTool *> newTools;
    QMap<QString, QAction *> newActions;
    for (auto it = tools.cbegin(), end = tools.cend(); it != end; ++it) {
        const QList<ExternalTool *> values = it.value();
        for (ExternalTool *tool : values) {
            const QString id = tool->id();
            if (d->m_tools.value(id) == tool) {
                newActions.insert(id, d->m_actions.value(id));
                // remove from list to prevent deletion
                d->m_tools.remove(id);
                d->m_actions.remove(id);
            }
            newTools.insert(id, tool);
        }
    }
    qDeleteAll(d->m_tools);
    const Id externalToolsPrefix = "Tools.External.";
    for (auto remainingActions = d->m_actions.cbegin(), end = d->m_actions.cend();
            remainingActions != end; ++remainingActions) {
        ActionManager::unregisterAction(remainingActions.value(),
            externalToolsPrefix.withSuffix(remainingActions.key()));
        delete remainingActions.value();
    }
    d->m_actions.clear();
    // assign the new stuff
    d->m_tools = newTools;
    d->m_actions = newActions;
    d->m_categoryMap = tools;
    // create menu structure and remove no-longer used containers
    // add all the category menus, QMap is nicely sorted
    QMap<QString, ActionContainer *> newContainers;
    for (auto it = tools.cbegin(), end = tools.cend(); it != end; ++it) {
        ActionContainer *container = nullptr;
        const QString &containerName = it.key();
        if (containerName.isEmpty()) { // no displayCategory, so put into external tools menu directly
            container = mexternaltools;
        } else {
            if (d->m_containers.contains(containerName))
                container = d->m_containers.take(containerName); // remove to avoid deletion below
            else
                container = ActionManager::createMenu(Id("Tools.External.Category.").withSuffix(containerName));
            newContainers.insert(containerName, container);
            mexternaltools->addMenu(container, Constants::G_DEFAULT_ONE);
            container->menu()->setTitle(containerName);
        }
        const QList<ExternalTool *> values = it.value();
        for (ExternalTool *tool : values) {
            const QString &toolId = tool->id();
            // tool action and command
            QAction *action = nullptr;
            Command *command = nullptr;
            if (d->m_actions.contains(toolId)) {
                action = d->m_actions.value(toolId);
                command = ActionManager::command(externalToolsPrefix.withSuffix(toolId));
            } else {
                ActionBuilder external(m_instance, externalToolsPrefix.withSuffix(toolId));
                external.setCommandAttribute(Command::CA_UpdateText);
                external.addOnTriggered(tool, [tool] {
                    auto runner = new ExternalToolRunner(tool);
                    if (runner->hasError())
                        MessageManager::writeFlashing(runner->errorString());
                });
                action = external.contextAction();
                d->m_actions.insert(toolId, action);
                command = external.command();
            }
            action->setText(tool->displayName());
            action->setToolTip(tool->description());
            action->setWhatsThis(tool->description());
            container->addAction(command, Constants::G_DEFAULT_TWO);
        }
    }

    // delete the unused containers
    qDeleteAll(d->m_containers);
    // remember the new containers
    d->m_containers = newContainers;

    // (re)add the configure menu item
    mexternaltools->menu()->addAction(d->m_configureSeparator);
    mexternaltools->menu()->addAction(d->m_configureAction);
}

static void readSettings(const QMap<QString, ExternalTool *> &tools,
                         QMap<QString, QList<ExternalTool *> > *categoryMap)
{
    QtcSettings *settings = ICore::settings();
    settings->beginGroup("ExternalTools");

    if (categoryMap) {
        settings->beginGroup("OverrideCategories");
        const QStringList settingsCategories = settings->childGroups();
        for (const QString &settingsCategory : settingsCategories) {
            QString displayCategory = settingsCategory;
            if (displayCategory == QLatin1String(kSpecialUncategorizedSetting))
                displayCategory = QLatin1String("");
            int count = settings->beginReadArray(settingsCategory);
            for (int i = 0; i < count; ++i) {
                settings->setArrayIndex(i);
                const QString &toolId = settings->value("Tool").toString();
                if (tools.contains(toolId)) {
                    ExternalTool *tool = tools.value(toolId);
                    // remove from old category
                    (*categoryMap)[tool->displayCategory()].removeAll(tool);
                    if (categoryMap->value(tool->displayCategory()).isEmpty())
                        categoryMap->remove(tool->displayCategory());
                    // add to new category
                    (*categoryMap)[displayCategory].append(tool);
                }
            }
            settings->endArray();
        }
        settings->endGroup();
    }

    settings->endGroup();
}

static void writeSettings()
{
    QtcSettings *settings = ICore::settings();
    settings->beginGroup("ExternalTools");
    settings->remove("");

    settings->beginGroup("OverrideCategories");
    for (auto it = d->m_categoryMap.cbegin(), end = d->m_categoryMap.cend(); it != end; ++it) {
        QString category = it.key();
        if (category.isEmpty())
            category = QLatin1String(kSpecialUncategorizedSetting);
        settings->beginWriteArray(category, it.value().count());
        int i = 0;
        const QList<ExternalTool *> values = it.value();
        for (const ExternalTool *tool : values) {
            settings->setArrayIndex(i);
            settings->setValue("Tool", tool->id());
            ++i;
        }
        settings->endArray();
    }
    settings->endGroup();

    settings->endGroup();
}

void ExternalToolManager::emitReplaceSelectionRequested(const QString &output)
{
    emit m_instance->replaceSelectionRequested(output);
}

} // namespace Core
