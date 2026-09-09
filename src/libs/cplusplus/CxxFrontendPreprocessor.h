// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

namespace CPlusPlus {

// Preprocessing through the cxx-frontend engine.
//
// Two things come out of it. One is the output the parser reads. The other is
// the running commentary CppSourceProcessor needs to build a Document: which
// macro was defined, which was used at which offset and with which arguments,
// which blocks #if left out, what the include guard was. Qt Creator highlights
// macros, follows them and dims inactive blocks out of the second.
//
// Includes are resolved by the caller rather than by the engine, which is the
// part Qt Creator has always had to own: the working copy of what is open in
// an editor, and its own search paths.
//
// gaps() says what CppSourceProcessor still could not get from here;
// tests/auto/cxxfrontend asserts on it.
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

    // A range of bytes in one of the files that were read. Pair fileName()
    // with it to say where it is.
    struct Range
    {
        int fileId = 0;
        int offset = 0;
        int length = 0;
    };

    struct MacroDefinition
    {
        QString name;
        QString body;
        QStringList parameters;
        Range definition;
        bool isFunctionLike = false;
        bool isVariadic = false;
    };

    struct MacroUse
    {
        QString name;
        Range range;
        Range definition;
        // Empty when the name was only asked about by defined() or #ifdef.
        bool expanded = false;
        QList<Range> arguments;
    };

    // What the engine did on the way to its output. Only filled in when a
    // delegate is attached, which run() does.
    struct Report
    {
        QList<MacroDefinition> definedMacros;
        QList<MacroUse> macroUses;
        QStringList undefinedMacroUses;
        QList<Range> skippedRegions;
        QHash<int, QString> includeGuards;
        QList<Range> pragmas;
    };

    const Report &report() const;

    // One token of the output, in Qt Creator's terms. The offsets of an
    // expanded token point at the macro invocation: the text it was replaced
    // by is in the macro's body and has no position here.
    struct Token
    {
        int kind = 0;
        Range range;
        bool expanded = false;
        bool generated = false;
    };

    // The tokens the last run() produced.
    const QList<Token> &tokens() const;

    // The file a Range belongs to, empty if there is no such file.
    QString fileName(int fileId) const;

    // The preprocessed text of \a source, named \a fileName for the sake of
    // diagnostics and __FILE__.
    QString run(const QString &source, const QString &fileName);

    // What CppSourceProcessor could not get from this engine when the move
    // started. Kept, and asserted on, so that a snapshot refresh that loses
    // any of it is noticed.
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
