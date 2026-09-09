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

        // Ask what could be written at this position, one-based, zero for
        // neither. It has to be set before parsing rather than asked
        // afterwards: the parser is told where to stop and look around, which
        // is also how it copes with the half-written expression that is there
        // while someone is typing.
        int completionLine = 0;
        int completionColumn = 0;
    };

    // Parses \a source straight away; there is nothing useful to do with an
    // unparsed one. Two overloads rather than a defaulted argument, because
    // Config carries default member initializers and cannot be written as {}
    // while this class is still being defined.
    CxxFrontendDocument(const QString &source, const QString &fileName);
    CxxFrontendDocument(const QString &source, const QString &fileName,
                        const Config &config);
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
        QString name;      // fully qualified
        QString filePath;  // the file it was declared in
        int line = 0;
        int column = 0;

        bool isValid() const { return line != 0; }
    };

    // Only reaches what this file declares. A name a header declared is not
    // in this translation unit at all -- the header's text is not taken in --
    // so it does not resolve here, and the snapshot has to be asked instead.
    Declaration declarationAt(int line, int column) const;

    // The declaration whose own name is written at a position, rather than
    // the one a name at a position refers to.
    //
    // declarationAt answers for a *use*, and a declaration is not a use: the
    // parser had nothing to resolve where the name was introduced, because
    // that is the place a name comes from. Asking for the usages of something
    // while standing on the line that declares it -- which is how anyone
    // reading a header does it -- means this.
    Declaration declarationOfNameAt(int line, int column) const;

    // Every place this file writes \a name as an identifier, in the order
    // they appear. What a search over the snapshot needs before it can ask, of
    // each one, whether it means the declaration being looked for.
    //
    // Only what the file itself writes: a token a macro's replacement list
    // produced is not here, since no text at that position corresponds to it,
    // which is the rule the built-in model's find usages follows too. A token
    // that came out of a macro argument is here, where the argument was
    // written -- and a macro that repeats its argument produces that one
    // place several times, so it is reported once.
    struct Occurrence
    {
        int line = 0;
        int column = 0;
        int length = 0;
    };
    QList<Occurrence> occurrencesOf(const QString &name) const;

    // The type of the expression written at a position, and whether it is an
    // lvalue. What a tooltip shows, and what completion needs before it can
    // offer the members of something.
    //
    // The type checker worked this out while parsing and left it on the
    // expression, so this reads it off, the same way declarationAt reads off
    // what the parser resolved. Empty for a position that is not inside an
    // expression, and for one whose type the checker could not settle.
    struct ExpressionType
    {
        QString type;
        bool isLvalue = false;

        bool isValid() const { return !type.isEmpty(); }
    };
    ExpressionType typeAt(int line, int column) const;

    // What could be written where Config asked. The parser works this out on
    // its way past the position, so a document built without asking has
    // nothing here.
    struct Completion
    {
        enum class Kind {
            None,
            Unqualified,  // a name, anywhere a name can go
            Member,       // after a . or ->
            Scope         // after a ::
        };

        Kind kind = Kind::None;
        // What is being looked into, for a member or scope completion.
        QString objectType;
        // The names on offer.
        QStringList candidates;

        // Inside the parentheses of a call both apply at once: a name can be
        // written there, and the call it belongs to has a signature worth
        // showing. So the hints sit beside the names rather than instead of
        // them, which is also how an editor shows them.
        QStringList signatures;
        int activeParameter = 0;

        bool isValid() const { return kind != Kind::None || !signatures.isEmpty(); }
    };
    const Completion &completion() const;

    // The identifier written at a position, empty if there is none. What the
    // snapshot needs in order to go looking elsewhere.
    QString identifierAt(int line, int column) const;

    // The names written in front of it, so that the N and A of A::N::x come
    // back as {"A", "N"}. Read off the tokens rather than the syntax tree,
    // because a qualifier naming something this file cannot see is exactly
    // the case the snapshot has to answer, and the parser did not resolve it.
    QStringList qualifierAt(int line, int column) const;

    // The bases of the innermost class written around a position, by name.
    // A base this file cannot see is still named here, which is what lets
    // the snapshot go and find it.
    QStringList basesAt(int line, int column) const;

    // The bases of a class this file declares, by name. Same reading as
    // basesAt, addressed by name rather than by position, so that a search
    // that has followed a base here can go on to the next one.
    QStringList basesOf(const QString &className) const;

    // Looks a name up in what this file declares, through the front end's own
    // lookup rather than by scanning what symbols() flattened.
    //
    // \a qualifier is the path written in front of the name, outermost first
    // and possibly empty. The name is interned in this document's own
    // control, which is what makes the front end's lookup usable here at all:
    // a scope matches names by identity, and every document interns its own.
    //
    // Going through the real lookup means the rules that hold inside this
    // file hold here too -- a base class, a using declaration, a class
    // declared in one place and defined in another -- without any of them
    // being written out a second time.
    Declaration lookup(const QStringList &qualifier, const QString &name) const;

    // Document's questions that cannot be answered on this model yet, each
    // with what is missing. Asserted on in tests/auto/cxxfrontend so the list
    // cannot go stale.
    static QStringList unsupportedQueries();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace CPlusPlus
