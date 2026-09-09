// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QByteArray>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace CPlusPlus {

// Preprocessing through the cxx-frontend engine.
//
// Not yet a replacement for CPlusPlus::Preprocessor, and the header says so
// rather than the call sites finding out: CppSourceProcessor wants a running
// commentary as well as an output -- which macro was used at which offset,
// which blocks #if skipped, what the include guard was -- and the cxx-frontend
// preprocessor reports none of it. What it does have is the resolution of
// includes, which it hands back to the caller rather than reading files
// itself, and that is the part Qt Creator has always had to own.
//
// So this covers the output only, and is here to be measured against the
// built-in engine while the rest is being added upstream. gaps() says what is
// still missing; tests/auto/cxxfrontend asserts on it.
//
// Deliberately no cxx/ header is included here: those need C++23, and only
// the implementation should have to.
class CxxFrontendPreprocessor
{
public:
    CxxFrontendPreprocessor();
    ~CxxFrontendPreprocessor();

    // Asked for the contents of a header the source included. Returning
    // nothing means the header could not be resolved, which is also what
    // happens when no resolver is set.
    using HeaderResolver
        = std::function<std::optional<QByteArray>(const QString &fileName, bool isSystem)>;
    void setHeaderResolver(const HeaderResolver &resolver);

    void defineMacro(const QString &name, const QString &body);
    void undefMacro(const QString &name);

    // The preprocessed text of \a source, named \a fileName for the sake of
    // diagnostics and __FILE__.
    QString run(const QString &source, const QString &fileName);

    // What CppSourceProcessor still could not get from this engine. Every one
    // of them is a Client callback with nothing to feed it.
    struct Gaps
    {
        bool reportsMacroUses = false;
        bool reportsSkippedBlocks = false;
        bool reportsIncludeGuards = false;
        bool marksExpandedTokens = false;
    };
    static Gaps gaps();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace CPlusPlus
