// Copyright  (C) 2016 AudioCodes Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <vcsbase/vcsbaseeditor.h>

#include <QRegularExpression>

namespace ClearCase {
namespace Internal {

class ClearCaseEditorWidget : public VcsBase::VcsBaseEditorWidget
{
    Q_OBJECT

public:
    ClearCaseEditorWidget();

private:
    QString changeUnderCursor(const QTextCursor &) const override;
    VcsBase::BaseAnnotationHighlighter *createAnnotationHighlighter(
            const QSet<QString> &changes) const override;

    const QRegularExpression m_versionNumberPattern;
};

} // namespace Internal
} // namespace ClearCase
