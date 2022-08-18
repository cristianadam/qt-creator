// Copyright  (C) 2016 Brian McGillion
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <vcsbase/baseannotationhighlighter.h>
#include <QRegularExpression>

namespace Mercurial {
namespace Internal {

class MercurialAnnotationHighlighter : public VcsBase::BaseAnnotationHighlighter
{
public:
    explicit MercurialAnnotationHighlighter(const ChangeNumbers &changeNumbers,
                                            QTextDocument *document = nullptr);

private:
    QString changeNumber(const QString &block) const override;
    const QRegularExpression changeset;
};

} //namespace Internal
}// namespace Mercurial
