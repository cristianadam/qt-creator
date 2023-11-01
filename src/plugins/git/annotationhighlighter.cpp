// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "annotationhighlighter.h"

namespace Git {
namespace Internal {

GitAnnotationHighlighter::GitAnnotationHighlighter(
    const QRegularExpression &annotationSeparatorPattern,
    const QRegularExpression &annotationEntryPattern,
    QTextDocument *document)
    : VcsBase::BaseAnnotationHighlighter(annotationSeparatorPattern,
                                         annotationEntryPattern,
                                         document)
{}

QString GitAnnotationHighlighter::changeNumber(const QString &block) const
{
    const int pos = block.indexOf(m_blank, 4);
    return pos > 1 ? block.left(pos) : QString();
}

} // namespace Internal
} // namespace Git
