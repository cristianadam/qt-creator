// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qmljstools_global.h"
#include <QWidget>


namespace TextEditor {
    class SnippetEditorWidget;
}
namespace QmlJSTools {

class QMLJSTOOLS_EXPORT QmlFormatSettingsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit QmlFormatSettingsWidget(QWidget *parent = nullptr);

private:
    TextEditor::SnippetEditorWidget *m_qmlformatConfigTextEdit;
};

} // namespace QmlJSTools
