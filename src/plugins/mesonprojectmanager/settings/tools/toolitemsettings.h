// Copyright (C) 2020 Alexis Jeandet.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/id.h>
#include <utils/fileutils.h>
#include <utils/pathchooser.h>

#include <optional>

namespace MesonProjectManager::Internal {

class ToolTreeItem;

class ToolItemSettings : public QWidget
{
    Q_OBJECT

public:
    explicit ToolItemSettings(QWidget *parent = nullptr);
    ~ToolItemSettings();
    void load(ToolTreeItem *item);
    void store();

signals:
    void applyChanges(Utils::Id itemId, const QString &name, const Utils::FilePath &exe);

private:
    std::optional<Utils::Id> m_currentId{std::nullopt};
    QLineEdit *m_mesonNameLineEdit;
    Utils::PathChooser *m_mesonPathChooser;
};

} // MesonProjectManager::Internal
