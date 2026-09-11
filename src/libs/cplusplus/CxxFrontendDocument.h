// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <cplusplus/Overview.h>

#include <utils/utilsicons.h>

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

namespace cxx {
class TranslationUnit;
}

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

        // Where a header is and what it says.
        struct Include
        {
            QString filePath;
            QString source;
        };

        // Called when the file includes a header -- with the name as it was
        // written, whether it was written in angle brackets, and the file
        // that wrote it -- and answers with the header, or nothing if it
        // could not be found. Without a handler every include is treated as
        // not found.
        //
        // The header's text is read into this translation unit, the way a
        // compiler reads it. It has to be: a declaration whose type comes
        // from a header cannot be read at all without it, and a file that
        // uses its headers is what every file is.
        std::function<std::optional<Include>(const QString &name, bool isSystem,
                                             const QString &includedFrom)>
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

        // The symbol this one is declared inside, as an index into the list
        // it came from, or -1 at file scope. The list is in the order the
        // file declares things, so a scope always comes before its members,
        // and the two together are the tree an outline draws.
        int parent = -1;

        // The two halves an outline writes after the name, printed apart
        // because Overview prints them apart: a function's parameter list,
        // and the type after the colon -- its return type, or the type of
        // whatever else this is. Both empty for a scope, which has no type
        // to show.
        QString signature;
        QString valueType;

        // Which icon stands for it, the question Icons::iconTypeForSymbol()
        // answers of a built-in symbol: what it is, who may see it, and
        // whether it belongs to the class rather than to an object.
        Utils::CodeModelIcon::Type icon = Utils::CodeModelIcon::Unknown;

        // Written by a macro's replacement rather than by the file, the way
        // Q_OBJECT declares things. An outline leaves those out: there is no
        // text of its own to point at.
        bool isGenerated = false;

        // A class named without its body, which an outline greys out because
        // the thing itself is somewhere else.
        bool isForwardDeclaration = false;
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
    //
    // Except the macro the file guards itself with, which is what makes it
    // idempotent rather than something it branches on.
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

    // Which of the two ways a comment is written, and whether it is written
    // for a documentation tool. One enum rather than two flags because that
    // is the question a reader asks: two comments belong to one block only if
    // they are of the same kind, and these four are the built-in front end's
    // four comment tokens.
    enum class CommentKind {
        CStyle,         // /* ... */
        CppStyle,       // // ...
        CStyleDoxygen,  // /** ... */ or /*! ... */
        CppStyleDoxygen // /// ... or //! ...
    };

    struct Comment
    {
        int line = 0; // one-based, as everything here counts
        int column = 0;
        int endLine = 0;
        int endColumn = 0;
        CommentKind kind = CommentKind::CStyle;
    };

    // Every comment this file writes, in the order they are written.
    //
    // Comments are not code, and the parser never sees one: this is the
    // preprocessor's own account, which reads each comment and hands it over
    // before dropping it. What a header writes is not here -- a comment is
    // read where it is written, and what is asked of a document is about the
    // file in hand.
    //
    // Why a document holds them at all: a declaration's documentation is
    // written above it, and the editor answers for the two together --
    // renaming a parameter renames it in the comment, moving a function takes
    // its comment along, and a parameter named in the comment is highlighted
    // with it.
    const QList<Comment> &comments() const;

    // The other place the function at a position is written: its declaration
    // where a definition stands there, its definition where a declaration
    // does. What "Switch Between Function Declaration/Definition" follows,
    // and half of what the decl/def link needs.
    //
    // Not answered by resolving the name, which is what every other question
    // here does: the name in "void C::f() {}" declares nothing new and the
    // parser resolves it to nothing. What answers is the definition itself --
    // the function it declares carries the place it was first declared.
    //
    // Nothing where the position is on no function. Where there is one but
    // the other place is not in this translation unit -- a declaration in a
    // header defined in some source file this document never read -- the
    // name and the parameter count come back without a place: which file
    // that is, is a question about the project, and whoever knows the
    // project's files can go on from there with definitionOf().
    struct Counterpart
    {
        QString filePath;
        int line = 0; // one-based
        int column = 0;
        // Whether what was found defines the function -- which says which
        // way round the answer is, without the caller having to work out
        // what it asked from.
        bool isDefinition = false;

        // The function the position is on, whether or not its other place
        // was found: the name as it would be written out in full, and how
        // many parameters it takes.
        QString name;
        int parameterCount = 0;

        bool isValid() const { return line > 0; }
        bool namesAFunction() const { return !name.isEmpty(); }
    };
    Counterpart counterpartAt(int line, int column) const;

    // The switch statement written around a position: where its body opens,
    // and which values of the enumeration its condition has it does not
    // handle yet, each written the way a case label has to write it.
    //
    // One question rather than two, because which values are missing and how
    // each is written are the same question asked twice -- and how an
    // enumerator is written is the whole difference a scoped enumeration
    // makes: the values of an unscoped one are named in the scope around it,
    // of a scoped one under the enumeration itself.
    //
    // Nothing where the position is in no switch, where the switch's body is
    // not a block, or where its condition is not of an enumeration -- which
    // is the answer "there is nothing to complete here".
    struct Switch
    {
        int bodyLine = 0; // just after the '{', one-based
        int bodyColumn = 0;
        QStringList missingValues;

        bool isValid() const { return bodyLine > 0; }
    };
    Switch switchAt(int line, int column) const;

    // What the function declaration at a position says, and how each of its
    // types has to be written where another declaration in this file stands.
    //
    // The two places are both in this file, which is what a function's
    // declaration and its definition are whenever one translation unit holds
    // them: a header is read into the file that includes it, so the source
    // file that defines a function holds the header's declaration as well.
    // Where they are in two files and neither reads the other, this is not
    // the document to ask -- read the one that holds both.
    //
    // An object rather than plain data, because writing a type means writing
    // it under a name, and which name is the caller's choice, made after it
    // has compared what the two sides say. It reads the document it came
    // from, so it may not outlive it.
    class Signature
    {
    public:
        Signature();
        Signature(Signature &&other) noexcept;
        Signature &operator=(Signature &&other) noexcept;
        ~Signature();

        bool isValid() const;

        // The name it is declared under, the qualifier included: the C::f of
        // a definition written outside its class.
        QString name() const;

        // Canonical spellings, which are only ever compared: what tells one
        // type here from another.
        QString returnType() const;
        int parameterCount() const;
        QString parameterName(int index) const; // empty where it is unnamed
        QString parameterType(int index) const;

        bool isConst() const;
        bool isVolatile() const;

        // "noexcept", or empty where the function has no exception
        // specification. Which of the ways of writing one was used is not
        // recorded, so a throw() comes back as noexcept -- on
        // unsupportedQueries() with the rest.
        QString exceptionSpecification() const;

        // Written for the other place: as little in front of each name as
        // still finds it from there. The return type is written under \a
        // name, since a return type is written in front of the name and
        // replaced along with it; a parameter under its own, empty for one
        // that is to stay unnamed.
        QString writeReturnType(const QString &name) const;
        QString writeParameter(int index, const QString &name) const;

        // The canonical spelling of that same parameter type, so that what
        // would be written there can be told from what is written there now.
        QString writtenParameterType(int index) const;

    private:
        friend class CxxFrontendDocument;
        class Private;
        std::unique_ptr<Private> d;
    };

    // A place in one of the files this translation unit read: the file as
    // Config::onInclude handed it over, empty for this document's own, and a
    // line and a column in it, both counted from one.
    //
    // Which file has to be said, because a header is read into the file that
    // includes it: one unit holds both, and line 3 of a header is not line 3
    // here.
    struct Place
    {
        QString filePath;
        int line = 0;
        int column = 0;
    };

    // \a function is on the function being read, \a writtenAt on the
    // declaration its types are to be written at. Both name a function this
    // translation unit declares.
    Signature signatureAt(const Place &function, const Place &writtenAt) const;

    // Where this file defines \a name -- written out in full, as
    // Counterpart::name is -- taking \a parameterCount parameters, or
    // nothing where it does not define it.
    //
    // For whoever is looking for the definition of a declaration across
    // files: the files to read and the order to read them in is what the
    // project knows, and this is the question to ask of each.
    //
    // Which of several overloads is not settled here beyond the number of
    // parameters, so two that differ only in their types are not told
    // apart. On unsupportedLookups() with the rest.
    Counterpart definitionOf(const QString &name, int parameterCount) const;

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

        // Whether this place defines the thing, rather than only declaring
        // it. A class forward declared here and defined elsewhere, or a
        // function declared here and defined in another file, answers false
        // -- and a caller that wanted the definition, as follow symbol does,
        // then knows to keep looking rather than to send someone here.
        //
        // The definition is preferred where this document has both, so false
        // means the document does not have it at all.
        bool isDefinition = true;

        // Whether a using declaration in this file brought the name in. The
        // built-in model answers such a name with the using declaration
        // itself rather than with what it names, which is what
        // QTCREATORBUG7903 asked for; a caller that has to agree with it can
        // tell from here that it must answer this one itself.
        bool throughUsingDeclaration = false;

        // Where the thing was first declared, which is what tells one
        // entity from another. A function declared in a header and defined
        // in a source file is one thing written in two places, and this is
        // the place both of them agree on; a search compares this rather
        // than where it happens to be pointing.
        QString canonicalFilePath;
        int canonicalLine = 0;
        int canonicalColumn = 0;

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

    // The locals of the function written around a position -- its parameters
    // and the variables of its blocks -- each with every place it is written,
    // its declaration first and its uses after, in the order they appear.
    //
    // This is the one question a single file answers completely: a parameter
    // or a block variable cannot be named anywhere else, so nothing outside
    // this document can be missing from the answer. It is what the editor
    // highlights when the cursor is on a local, and what a rename confined to
    // one function works from.
    //
    // Two locals of the same name in nested blocks are two entries with their
    // own places, since that is what they are.
    struct Local
    {
        QString name;
        QList<Occurrence> places;

        // A parameter of the function rather than a variable of one of its
        // blocks. The two are written in the same places and highlighted
        // alike, but only a parameter is documented: a caller looking for a
        // name in the function's comment has to know which locals can be
        // there.
        bool isParameter = false;

        // The class this local's type names, without the scopes it is in, or
        // empty where the type does not name one -- an int, or a pointer or a
        // reference to a class, since a handle to a thing is not the thing.
        // What tells a local that is doing its work by existing, a lock or a
        // scoped pointer, from one that is declared and forgotten.
        QString className;
    };
    QList<Local> localsAt(int line, int column) const;

    // What every name in the file stands for, which is what the editor
    // colours it by. The distinctions are CheckSymbols': what the name
    // means, who it belongs to, and whether it is being declared here or
    // used.
    //
    // A name whose meaning does not change how it is written is not here at
    // all -- a global variable is left plain by the built-in model too, so
    // there is no entry for one.
    enum class NameKind {
        Type,
        Namespace,
        Local,
        Field,
        StaticField,
        Enumeration,
        Function,
        VirtualMethod,
        StaticMethod,
        FunctionDeclaration,
        VirtualFunctionDeclaration,
        StaticMethodDeclaration,
        Label,
        // A word that reads as a keyword without being one: override and
        // final, which are identifiers anywhere else.
        PseudoKeyword,
    };

    struct Name
    {
        int line = 0;
        int column = 0;
        int length = 0;
        NameKind kind = NameKind::Type;
    };

    // Every name the file writes that stands for something, in the order
    // they are written.
    //
    // The parser resolved most of them on its way past, so this reads its
    // answers rather than looking anything up: a name it did not resolve has
    // no entry, the same way an unresolved name is left plain today.
    //
    // Not here, and not for this to answer: a macro, which the preprocessor
    // reports and the caller already merges in; and the punctuation the
    // editor also colours -- the angle brackets of a template argument list
    // and the two halves of a ternary -- which are not names.
    QList<Name> namesIn() const;

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

        // How it is being looked into, for a member completion: whether it
        // is a pointer, and whether a dot or an arrow was written. An
        // editor offers the members either way and puts the right operator
        // there afterwards, so it has to be told which was written.
        bool objectIsPointer = false;
        bool dotWasWritten = false;

        // The front end could not see everything that is being looked
        // into: a base class it could not work out, a class whose body
        // this file never saw. What comes back is then a part of the
        // answer, and a part of a list of what can be written here is
        // worse than none -- the name somebody wants may be the one
        // missing.
        bool membersMayBeMissing = false;

        // One of the things that could be written: its name, the
        // declaration it stands for as Overview would print it, and the
        // icon that says what it is -- which is what a proposal shows.
        struct Candidate
        {
            QString name;
            QString detail;
            Utils::CodeModelIcon::Type icon = Utils::CodeModelIcon::Unknown;

            // What choosing it writes. An editor does not put the name in
            // and stop: a function is written with its parentheses, one
            // that takes nothing has them closed, and one that returns
            // nothing ends the statement as well.
            //
            // These are here because that decision is about the
            // declaration, and this is what knows the declaration. A
            // consumer that had to work them out again would be reading
            // the code model a second way.
            bool isFunction = false;
            bool takesArguments = false;
            bool returnsNothing = false;

            // Where it sits in the list. A proposal offers what a class
            // says anybody may use ahead of the rest, and the class's own
            // name -- which stands for the type, and is the reason a
            // constructor is never offered here -- behind them.
            bool isPublic = false;
            bool isInjectedClassName = false;
        };
        QList<Candidate> candidates;

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

    // The parsed file itself, for the readers that work on the shape of the
    // code rather than on what it means: the quick fixes, the decl/def link,
    // expanding a selection. Everything above is a question with an answer
    // this document works out; this hands over the tree those readers walk.
    //
    // Only forward declared, so that including this header still does not
    // mean compiling cxx's own headers, which need C++23. Whoever takes it
    // up does have to; CxxFrontendAst.h is where that begins.
    cxx::TranslationUnit *translationUnit() const;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace CPlusPlus
