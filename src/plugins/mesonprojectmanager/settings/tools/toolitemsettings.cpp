// Copyright (C) 2020 Alexis Jeandet.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "toolitemsettings.h"

#include "tooltreeitem.h"

#include <utils/layoutbuilder.h>

#include <QLineEdit>

using namespace Utils;

namespace MesonProjectManager::Internal {

ToolItemSettings::ToolItemSettings(QWidget *parent)
    : QWidget(parent)
{
    resize(409, 70);

    m_mesonNameLineEdit = new QLineEdit(this);

    m_mesonPathChooser = new PathChooser(this);
    m_mesonPathChooser->setExpectedKind(PathChooser::ExistingCommand);
    m_mesonPathChooser->setHistoryCompleter(QLatin1String("Meson.Command.History"));

    using namespace Layouting;

    Form {
        tr("Name:"), m_mesonNameLineEdit,
        tr("Path:"), m_mesonPathChooser
    }.attachTo(this);

    connect(m_mesonPathChooser, &PathChooser::rawPathChanged, this, &ToolItemSettings::store);
    connect(m_mesonNameLineEdit, &QLineEdit::textChanged, this, &ToolItemSettings::store);
}

void ToolItemSettings::load(ToolTreeItem *item)
{
    if (item) {
        m_currentId = std::nullopt;
        m_mesonNameLineEdit->setDisabled(item->isAutoDetected());
        m_mesonNameLineEdit->setText(item->name());
        m_mesonPathChooser->setDisabled(item->isAutoDetected());
        m_mesonPathChooser->setFilePath(item->executable());
        m_currentId = item->id();
    } else {
        m_currentId = std::nullopt;
    }
}

void ToolItemSettings::store()
{
    if (m_currentId)
        emit applyChanges(*m_currentId,
                          m_mesonNameLineEdit->text(),
                          m_mesonPathChooser->filePath());
}

} // MesonProjectManager::Internal
