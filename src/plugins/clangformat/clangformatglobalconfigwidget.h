// Copyright  (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <cppeditor/cppcodestylesettingspage.h>

#include <memory>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLabel;
QT_END_NAMESPACE

namespace ProjectExplorer { class Project; }

namespace ClangFormat {

class ClangFormatGlobalConfigWidget : public CppEditor::CppCodeStyleWidget
{
    Q_OBJECT

public:
    explicit ClangFormatGlobalConfigWidget(ProjectExplorer::Project *project = nullptr,
                                           QWidget *parent = nullptr);
    ~ClangFormatGlobalConfigWidget() override;
    void apply() override;

private:
    void initCheckBoxes();
    void initIndentationOrFormattingCombobox();

    QLabel *m_formattingModeLabel;
    QComboBox *m_indentingOrFormatting;
    QCheckBox *m_formatWhileTyping;
    QCheckBox *m_formatOnSave;
};

} // namespace ClangFormat
