// Copyright  (C) 2020 Alexis Jeandet.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include "exewrappers/mesontools.h"
#include "toolssettingspage.h"

#include <utils/treemodel.h>

#include <QCoreApplication>
#include <QQueue>

namespace MesonProjectManager {
namespace Internal {

class ToolTreeItem;

class ToolsModel final : public Utils::TreeModel<Utils::TreeItem, Utils::TreeItem, ToolTreeItem>
{
    Q_DECLARE_TR_FUNCTIONS(MesonProjectManager::Internal::ToolsSettingsPage)
public:
    ToolsModel();
    ToolTreeItem *mesoneToolTreeItem(const QModelIndex &index) const;
    void updateItem(const Utils::Id &itemId, const QString &name, const Utils::FilePath &exe);
    void addMesonTool();
    void removeMesonTool(ToolTreeItem *item);
    ToolTreeItem *cloneMesonTool(ToolTreeItem *item);
    void apply();

private:
    void addMesonTool(const MesonTools::Tool_t &);
    QString uniqueName(const QString &baseName);
    Utils::TreeItem *autoDetectedGroup() const;
    Utils::TreeItem *manualGroup() const;
    QQueue<Utils::Id> m_itemsToRemove;
};

} // namespace Internal
} // namespace MesonProjectManager
