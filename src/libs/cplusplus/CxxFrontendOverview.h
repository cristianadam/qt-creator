// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <cplusplus/Overview.h>

#include <QString>
#include <QStringList>

#include <memory>

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
// It takes what the cxx-frontend parser produced. The caller does not see
// those types, and so does not need C++23: parse() takes source text and
// hands back what the symbols look like.
class CxxFrontendOverview
{
public:
    CxxFrontendOverview();
    ~CxxFrontendOverview();

    // The same knobs Overview has, so that the two can be asked the same
    // question. Not all of them are honoured yet; unsupported() says which.
    Overview settings;

    struct Symbol
    {
        QString name;          // as prettyName would print it
        QString type;          // as prettyType would print it, with the name
        QStringList qualified; // the enclosing scopes, outermost first
        int line = 0;
        int column = 0;
    };

    // Parses \a source and describes the symbols it declares, outermost
    // first, each scope's members after it. Empty if the source did not
    // parse.
    QList<Symbol> parse(const QString &source, const QString &fileName) const;

    // The Overview settings this printer does not honour yet. Asserted on in
    // tests/auto/cxxfrontend so the list cannot go stale.
    static QStringList unsupported();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace CPlusPlus
