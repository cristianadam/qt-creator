// Copyright  (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "clangformatglobalconfigwidget.h"

#include "clangformatconstants.h"
#include "clangformatsettings.h"

#include <projectexplorer/project.h>

#include <utils/layoutbuilder.h>

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QWidget>

#include <sstream>

using namespace ProjectExplorer;
using namespace Utils;

namespace ClangFormat {

ClangFormatGlobalConfigWidget::ClangFormatGlobalConfigWidget(ProjectExplorer::Project *project,
                                                             QWidget *parent)
    : CppCodeStyleWidget(parent)
{
    resize(489, 305);

    m_formattingModeLabel = new QLabel(tr("Formatting mode:"));
    m_indentingOrFormatting = new QComboBox(this);
    m_formatWhileTyping = new QCheckBox(tr("Format while typing"));
    m_formatOnSave = new QCheckBox(tr("Format edited code on file save"));

    using namespace Layouting;

    Group globalSettingsGroupBox {
        title(tr("ClangFormat global setting:")),
            Column {
            Row { m_formattingModeLabel, m_indentingOrFormatting, st },
                m_formatWhileTyping,
                m_formatOnSave
        }
    };

    Column {
        globalSettingsGroupBox
    }.attachTo(this);

    initCheckBoxes();
    initIndentationOrFormattingCombobox();

    if (project) {
        globalSettingsGroupBox.widget->hide();
        return;
    }
    globalSettingsGroupBox.widget->show();
}

ClangFormatGlobalConfigWidget::~ClangFormatGlobalConfigWidget() = default;

void ClangFormatGlobalConfigWidget::initCheckBoxes()
{
    auto setEnableCheckBoxes = [this](int index) {
        bool isFormatting = index == static_cast<int>(ClangFormatSettings::Mode::Formatting);

        m_formatOnSave->setEnabled(isFormatting);
        m_formatWhileTyping->setEnabled(isFormatting);
    };
    setEnableCheckBoxes(m_indentingOrFormatting->currentIndex());
    connect(m_indentingOrFormatting, &QComboBox::currentIndexChanged,
            this, setEnableCheckBoxes);

    m_formatOnSave->setChecked(ClangFormatSettings::instance().formatOnSave());
    m_formatWhileTyping->setChecked(ClangFormatSettings::instance().formatWhileTyping());
}

void ClangFormatGlobalConfigWidget::initIndentationOrFormattingCombobox()
{
    m_indentingOrFormatting->insertItem(static_cast<int>(ClangFormatSettings::Mode::Indenting),
                                        tr("Indenting only"));
    m_indentingOrFormatting->insertItem(static_cast<int>(ClangFormatSettings::Mode::Formatting),
                                        tr("Full formatting"));
    m_indentingOrFormatting->insertItem(static_cast<int>(ClangFormatSettings::Mode::Disable),
                                        tr("Disable"));

    m_indentingOrFormatting->setCurrentIndex(
        static_cast<int>(ClangFormatSettings::instance().mode()));
}


void ClangFormatGlobalConfigWidget::apply()
{
    ClangFormatSettings &settings = ClangFormatSettings::instance();
    settings.setFormatOnSave(m_formatOnSave->isChecked());
    settings.setFormatWhileTyping(m_formatWhileTyping->isChecked());
    settings.setMode(static_cast<ClangFormatSettings::Mode>(m_indentingOrFormatting->currentIndex()));
    settings.write();
}

} // namespace ClangFormat
