// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <cplusplus/Overview.h>

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

namespace CPlusPlus {

// One parsed file, on the cxx-frontend model.
//
// CPlusPlus::Document is what Qt Creator's built-in code model is made of: a
// file, its symbols, the macros it defined and used, its diagnostics, and the
// questions an editor asks about a position -- which function is the cursor
// in, which scope, which symbol was last declared above it. Snapshot is a
// collection of them, and the lookup that resolves a name walks it.
//
// This is the same thing on the other model, as far as that model reaches. It
// holds one file: the collection and the lookup across it are the slices
// after this one. unsupportedQueries() says which of Document's questions
// cannot be answered yet and why, and tests/auto/cxxfrontend asserts on it.
//
// Deliberately no cxx/ header is included here: those need C++23, and only
// the implementation should have to.
class CxxFrontendDocument
{
public:
    struct Config
    {
        Overview settings;

        // Macros in force before the first line, each written the way a
        // #define is: "FOO 1", "ADD(a, b) a + b". What definedMacros()
        // returns, so that what one file establishes can be handed to the
        // next.
        QStringList predefinedMacros;

        // Called when the file includes a header, and answers with the macros
        // that header established, or nothing if the header could not be
        // found. The header's text is never taken into this document: Qt
        // Creator keeps one translation unit per file, so the header gets a
        // document of its own and only its macros cross over. Without a
        // handler every include is treated as not found.
        //
        // inForce is everything defined at the point of the include, which
        // the header is entitled to see: it is preprocessed where it is
        // included, not on its own.
        std::function<std::optional<QStringList>(const QString &name, bool isSystem,
                                                 const QStringList &inForce)>
            onInclude;
    };

    // Parses \a source straight away; there is nothing useful to do with an
    // unparsed one.
    CxxFrontendDocument(const QString &source, const QString &fileName,
                        const Config &config = {});
    ~CxxFrontendDocument();

    QString fileName() const;

    // What the file declares, outermost first, each scope's members after it.
    struct Symbol
    {
        QString name;          // as prettyName would print it
        QString type;          // as prettyType would print it, with the name
        QStringList qualified; // the enclosing scopes, outermost first
        int line = 0;
        int column = 0;
    };
    const QList<Symbol> &symbols() const;

    // The macros this file defines, in the form Config::predefinedMacros
    // takes, so that they can be handed to whatever includes it.
    QStringList definedMacros() const;

    // The headers it included, as they were written.
    QStringList includedHeaders() const;

    // Every macro in force at the end of the file: what it was given plus
    // what it defined, less what it undefined.
    QStringList macrosInForce() const;

    // The macros this file's preprocessing asked about before defining them
    // itself, each with the definition it saw -- an empty string where the
    // answer was that there was none. This is what the file's parse depends
    // on from outside, and so what decides whether it can be reused.
    QHash<QString, QString> consultedMacros() const;

    // Whether this document would come out the same under \a environment.
    // True when every macro it consulted resolves there exactly as it did
    // here. A macro the file never asked about cannot change its parse, so
    // an unrelated define does not force a reparse.
    bool isValidFor(const QStringList &environment) const;

    // The name a #define line declares, up to the parameter list if there is
    // one. Public because a caller assembling an environment needs the same
    // rule.
    static QString macroNameOf(const QString &defineLine);

    struct Diagnostic
    {
        int line = 0;
        int column = 0;
        QString text;
        bool isError = true;
    };
    const QList<Diagnostic> &diagnostics() const;

    // The fully qualified name of the function enclosing the position, or an
    // empty string if it is not inside one. What Document::functionAt answers,
    // and what the editor puts above the text.
    QString functionAt(int line, int column) const;

    // The last symbol declared at or before the position, which is how
    // Document decides what the cursor is inside of. Empty if there is none.
    QString lastVisibleSymbolAt(int line, int column) const;

    // The name of the innermost scope written around the position, empty if
    // that is the file itself. What Document::scopeAt answers.
    QString scopeAt(int line, int column) const;

    // Where the name used at a position was declared. What follow symbol
    // needs, and what find usages and completion are built on.
    //
    // The built-in model answers this by resolving the name through
    // LookupContext over the snapshot. The cxx-frontend parser has already
    // resolved it while parsing and left the answer on the syntax tree, so
    // this reads it off rather than working it out again.
    struct Declaration
    {
        QString name;   // fully qualified
        int line = 0;
        int column = 0;

        bool isValid() const { return line != 0; }
    };
    Declaration declarationAt(int line, int column) const;

    // Document's questions that cannot be answered on this model yet, each
    // with what is missing. Asserted on in tests/auto/cxxfrontend so the list
    // cannot go stale.
    static QStringList unsupportedQueries();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace CPlusPlus
