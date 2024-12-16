// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmlformatsettingswidget.h"
#include "qmljstoolstr.h"

#include <texteditor/snippets/snippeteditor.h>
#include <utils/layoutbuilder.h>

#include <QVBoxLayout>

namespace QmlJSTools {

// TODO: Fetch this from qmlformat --write-defaults
static const char *defaultQmlFormatIniText =
    "[General]\n"
    "FunctionsSpacing=false\n"
    "IndentWidth=4\n"
    "MaxColumnWidth=-1\n"
    "NewlineType=native\n"
    "NormalizeOrder=false\n"
    "ObjectsSpacing=false\n"
    "SortImports=false\n"
    "UseTabs=false\n"
;

QmlFormatSettingsWidget::QmlFormatSettingsWidget(QWidget *parent)
    : QWidget(parent)
{
    m_qmlformatConfigTextEdit = new TextEditor::SnippetEditorWidget;
    QSizePolicy sp(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
    sp.setHorizontalStretch(1);
    m_qmlformatConfigTextEdit->setSizePolicy(sp);
    m_qmlformatConfigTextEdit->setPlainText(QLatin1StringView(defaultQmlFormatIniText));

    using namespace Layouting;
    // clang-format off
    Column {
        Group {
            title(Tr::tr("Global qmlformat Configuration")),
            Column {
                m_qmlformatConfigTextEdit,
            },
        },
        noMargin
    }.attachTo(this);
    // clang-format on
}

} // namespace QmlJSTools
