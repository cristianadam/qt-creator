// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <cplusplus/CxxFrontendDocument.h>
#include <cplusplus/Overview.h>

#include <QList>
#include <QString>
#include <QStringList>

namespace CPlusPlus {

// Overview for the cxx-frontend symbol model.
//
// Everything in Qt Creator that shows a symbol -- the locator, the outline,
// completion, tooltips, the class view -- goes through Overview, which prints
// a CPlusPlus::FullySpecifiedType in Qt Creator's own style, with its own
// knobs: where the star binds, whether argument names are shown, whether a
// return type is. The cxx-frontend printer answers a narrower question and
// answers it in its own style, so this stands in the same relation to it that
// Overview does to the built-in front end.
//
// The parsing and the walk belong to CxxFrontendDocument; this is the
// settings and the name for the concern.
class CxxFrontendOverview
{
public:
    // The same knobs Overview has, so that the two can be asked the same
    // question. Not all of them are honoured yet; unsupported() says which.
    Overview settings;

    using Symbol = CxxFrontendDocument::Symbol;

    // Parses \a source and describes the symbols it declares, outermost
    // first, each scope's members after it.
    QList<Symbol> parse(const QString &source, const QString &fileName) const
    {
        return CxxFrontendDocument(source, fileName, settings).symbols();
    }

    // The Overview settings this printer does not honour yet. Asserted on in
    // tests/auto/cxxfrontend so the list cannot go stale.
    static QStringList unsupported();
};

} // namespace CPlusPlus
