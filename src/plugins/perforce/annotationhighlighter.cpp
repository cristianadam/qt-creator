// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "annotationhighlighter.h"

namespace Perforce::Internal {

PerforceAnnotationHighlighter::PerforceAnnotationHighlighter(
    const QRegularExpression &annotationSeparatorPattern,
    const QRegularExpression &annotationEntryPattern,
    QTextDocument *document)
    : VcsBase::BaseAnnotationHighlighter(annotationSeparatorPattern,
                                         annotationEntryPattern,
                                         document)
{ }

QString PerforceAnnotationHighlighter::changeNumber(const QString &block) const
{
    const int pos = block.indexOf(QLatin1Char(':'));
    return pos > 1 ? block.left(pos) : QString();
}

} // Perforce::Internal
