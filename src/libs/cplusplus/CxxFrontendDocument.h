// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <cplusplus/Overview.h>
#include <cplusplus/FindUsages.h>

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
// This is the same thing on the other model, as far as that model reaches.
// It holds one file -- with its headers read into it, as a compiler reads
// them, so most of what a lookup across files was for is answered here.
// CxxFrontendSnapshot holds the files asked about and what is left of the
// searching. unsupportedQueries() says which of Document's questions cannot
// be answered and why, and tests/auto/cxxfrontend asserts on it.
//
// Deliberately no cxx/ header is included here: those need C++23, and only
// the implementation should have to.
class CxxFrontendDocument
{
public:
    // What kind of thing something declared is, for a reader that has to say
    // so. The words are the consumer's; these are the distinctions this
    // model makes, and a template is not among them -- a class template is a
    // class here, its parameters being something it has rather than
    // something it is.
    // What Qt's moc makes of a member function, which is what tells
    // writing a call to it apart from connecting to it. Nothing where
    // the file is not Qt, or where the extensions were not read.
    enum class QtMethod {
        None,
        Signal,
        Slot,
        Invokable,
    };

    // Who may name a member, which decides the section a declaration of
    // one goes into.
    enum class Access { Public, Protected, Private };

    enum class Kind {
        Unknown,
        Class,
        Enum,
        Enumerator,
        Namespace,
        Function,
        Variable,
        Field,
        TypeAlias,
        // A using declaration, which declares nothing of its own: it makes
        // a name declared somewhere else reachable here.
        UsingDeclaration
    };

    struct Config
    {
        Overview settings;

        // Macros in force before the first line, each written the way a
        // #define is: "FOO 1", "ADD(a, b) a + b". What definedMacros()
        // returns, so that what one file establishes can be handed to the
        // next.
        QStringList predefinedMacros;

        // Read the extensions Qt adds to C++ -- the signals and slots
        // sections of a class, Q_OBJECT, Q_INVOKABLE, emit -- rather than
        // letting Qt's own macros expand them into nothing. On as the
        // built-in front end has it on, since a file that says "signals:"
        // says it whether or not Qt is anywhere near it.
        bool qtExtensions = true;

        // Record what every file in the translation unit declares, each
        // symbol saying which file that is, rather than only what the file
        // being read declares itself.
        //
        // For a reader that is about the project rather than about one file
        // -- the index. Reading a header as a translation unit of its own
        // costs what reading it inside its includer costs, and a project has
        // tens of headers per source: over a slice of this one, reading
        // every reported file spent 89% of its time on headers it had
        // already read. Off by default, since every other reader here is
        // about one file and would have to filter the rest back out.
        bool everyFileInTheUnit = false;

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
        // As prettyName would print it, and empty for a scope written
        // without a name -- an anonymous namespace or union, which has
        // nothing written for it but holds what is written inside.
        QString name;
        QString type;          // as prettyType would print it, with the name
        // The enclosing scopes, outermost first. A scope with no name of its
        // own stands here as "<anonymous namespace>" and its like, the words
        // the built-in front end's readers write: a path with a gap in it
        // leads nowhere, and two things of one name in two anonymous scopes
        // have to be told apart.
        QStringList qualified;

        // Which file declares it. Empty unless Config::everyFileInTheUnit
        // was set, since without it every symbol is the read file's own.
        QString file;
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

        // What kind of thing it is, said the same way a Declaration says it
        // -- for a reader listing what a file declares rather than drawing
        // it, which is what an icon is for.
        Kind kind = Kind::Unknown;

        // Whether this file defines it and not only declares it. A function
        // declared here and defined further down is one entry -- this list
        // holds an entity once, at the place it is declared -- so this says
        // what the file does with it rather than what the place is.
        bool isDefinedHere = false;

        // Written extern, which says outright that what stands here is a
        // promise about something declared elsewhere.
        bool isExtern = false;

        // What Qt makes of it: which of a class's members are signals,
        // slots or invokable, and whether a class said Q_OBJECT or
        // Q_GADGET. Both are None and false for everything else.
        QtMethod qtMethod = QtMethod::None;
        bool isQObject = false;
    };

    // What a class declared with Q_PROPERTY, as written. The values are
    // text because that is what they are -- "d->count" is one -- and what
    // writes a getter for a property writes text too.
    struct QtProperty
    {
        QString name;
        QString type;    // as written, "const QString &" and not a path
        int line = 0;    // where the name stands, one-based
        int column = 0;
        // Where the Q_PROPERTY itself starts, which is where a reader
        // writing anything beside it has to write: a property declares
        // nothing, so its name is no place to start from.
        int startLine = 0;
        int startColumn = 0;
        // READ -> "title", WRITE -> "setTitle", FINAL -> "". In the order
        // the property wrote them.
        QList<QPair<QString, QString>> items;
    };
    const QList<Symbol> &symbols() const;

    // The properties the class written at a position declares, in the
    // order it wrote them. Nothing where no class is written there, or
    // where it declares none.
    QList<QtProperty> qtPropertiesAt(int line, int column) const;

    // The property a position is written on, which is the Q_PROPERTY token
    // and the two parentheses around what it says. A position inside them
    // is a question about the type, the name or an item, and answers
    // nothing here -- as a path through a tree stops at the innermost node
    // holding the position.
    std::optional<QtProperty> qtPropertyAt(int line, int column) const;


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
    //
    // \a inFile says which of the files this unit read the place is in,
    // empty being this document's own: the declaration whose definition is
    // wanted is as often in a header this file reads.
    Counterpart counterpartAt(int line, int column, const QString &inFile = {}) const;

    // A call or a new expression at a position whose value is thrown away,
    // which is what "assign this to a local variable" is offered on.
    //
    // Nothing where the position is on no such expression, where its value
    // is used after all -- an argument, a return, a member initializer --
    // or where there is no value to assign: a call of something that
    // returns nothing, and one this front end could not resolve.
    struct DiscardedValue
    {
        // Where the expression begins, which is where a declaration is
        // written in front of it. One-based.
        int line = 0;
        int column = 0;

        // The name of what is called, which is what a variable holding its
        // value would be named after.
        QString name;

        // The value's type, written as a declaration of that name and for
        // the scope the expression stands in. A caller writing a variable
        // there swaps the name for the one it chose, which is why the name
        // is written in rather than left out: how a declarator is written
        // around a name is not something a caller can work out from a type.
        QString declaration;

        bool isValid() const { return line > 0; }
    };
    DiscardedValue discardedValueAt(int line, int column) const;

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

    // \a function is on the function being read, \a writtenAt where its
    // types are to be written. The first names a function this translation
    // unit declares; the second need only be somewhere in it, since what a
    // place decides is how much has to stand in front of each name.
    //
    // Where it does name a function -- the other side of a declaration and
    // its definition, which is what the two-sided readers ask about -- a
    // parameter is written inside that function, so a type its own scope
    // reaches is written plain. Where it names none, which is what writing a
    // definition into a file that says nothing about it yet looks like,
    // every type is written for the scope the text is going into.
    Signature signatureAt(const Place &function, const Place &writtenAt) const;

    // The type of something a file declares, as whatever writes code with
    // it needs it.
    //
    // Not a string, for two reasons. How a type is written depends on where
    // it is written -- a type named from inside its namespace needs more of
    // its path from outside it -- and what is written is often not the type
    // as declared: a getter hands back what the member holds, a setter takes
    // it by const reference, and a Q_PROPERTY says the value behind either.
    // So the types made from one are asked for here rather than spelled by
    // the caller, which could only do it by editing the text.
    //
    // It reads the document it came from, so it may not outlive it.
    class Type
    {
    public:
        Type();
        Type(const Type &other);
        Type &operator=(const Type &other);
        ~Type();

        bool isValid() const;

        // What it is, as far as anything deciding how to hand it over
        // cares. A pointer, a reference, an enumeration and a number are
        // what is passed by value; anything else is a caller's own rule.
        bool isPointer() const;
        bool isReference() const;
        bool isEnumeration() const;
        bool isNumber() const;
        bool isConst() const;

        // The types made from it.
        Type withoutConst() const;
        // Neither const nor a reference: the value behind it.
        Type value() const;
        // const T &, and the const that a T & handed over needs -- which
        // leaves anything that is no reference as it is.
        Type constReference() const;
        Type withConstOnReference() const;
        // QList<int> -> int. Invalid where the type names no template.
        Type firstTemplateArgument() const;

        // Written as a declaration of \a name, or alone where that is
        // empty -- a declarator is written *around* a name, so no amount
        // of putting the name after the type gets there.
        //
        // As the place it was read in writes it, or, where \a writtenAt is
        // given, with as little in front of each name as still finds it
        // from there. That place is in this file: a type is written where
        // its declaration is going, and the document to ask is the one
        // that holds both.
        QString writtenAs(const QString &name) const;
        QString writtenAt(const Place &place, const QString &name) const;
        // The name it was declared under, without its path or its
        // arguments: what a caller with a rule per type has the rule
        // for. Empty for anything that is not a class, an enumeration
        // or an alias -- a number is not declared anywhere.
        // Without the template parameters, which is how a caller with a
        // rule per type names the type it has a rule for.
        QString declaredName() const;
        QString writtenWithoutTemplateParameters() const;

    private:
        friend class CxxFrontendDocument;
        class Private;
        std::shared_ptr<Private> d;
    };

    // The type of the thing declared at a place -- a member, a variable,
    // a parameter -- where its declarator writes its name. Invalid where
    // nothing is declared there: a name that declares something is not a
    // use of it, so this asks the declaration and not whatever stands
    // there.
    //
    // The place may be in a header this file read, which is how a source
    // file answers about a member declared in the header it includes --
    // and the document to ask about writing a definition is that one,
    // since it holds both the member and the place the definition goes.
    Type typeOfTheThingDeclaredAt(const Place &place) const;

    // The function at \a function, written out as a declaration of \a name
    // for wherever \a writtenAt is: every type in it written with as little
    // in front of it as still finds that type from there, so a nested class
    // is named with its class outside it and by itself within.
    //
    // What moving a definition from one place to another has to write. The
    // name is the caller's to choose, because it is the caller that knows
    // where the thing is going: the same function is "f" written inside its
    // class and "C::f" written outside it.
    //
    // The parameters are written under the names the function gives them,
    // which the printer is told: a name is written *around* a parameter --
    // "void (*cb)(int)" -- so a caller cannot put it in afterwards. One the
    // function leaves unnamed stays unnamed.
    //
    // What is not written is the template the function is declared under,
    // and anything that says something about the declaration rather than
    // about the function: an "explicit", a "static", a default argument.
    // None of those may be repeated where a definition is written apart
    // from its declaration, but a template header must be, so whoever writes
    // one has to hand back for a template until this can say it.
    //
    // Empty where the position is on no function.
    QString declarationOfFunctionAt(const Place &function, const Place &writtenAt,
                                    const QString &name) const;

    // The same function, written out as the head of a *definition* at \a
    // writtenAt: everything a definition puts in front of its body.
    //
    // The name is not the caller's choice here, which is the whole
    // difference from the call above: a definition is of one particular
    // function, and how much of its path has to stand in front of it is
    // settled by where it is going -- "C::f" written outside the class and
    // "f" within it -- the same way each type in it is.
    //
    // Empty where the position is on no function, and where the function is
    // under a template: the "template<...>" a definition written apart from
    // its declaration has to carry is not something this writes, and half a
    // definition is worse than none. Whoever needs one hands back and lets
    // the built-in path do it.
    QString definitionHeadAt(const Place &function, const Place &writtenAt) const;

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

    // Where this file *declares* \a name -- written out in full, as
    // Counterpart::name is -- taking \a parameterCount parameters, without
    // defining it there, or nothing where it does not.
    //
    // The other way round from definitionOf(), and for the other half of the
    // same job: whoever has a definition in hand and has to change what was
    // declared elsewhere. The file to look in is again what the project
    // knows and this does not.
    Counterpart declarationOf(const QString &name, int parameterCount) const;

    // The fully qualified name of the function enclosing the position, or an
    // empty string if it is not inside one. What Document::functionAt answers,
    // and what the editor puts above the text.
    //
    // \a fromLine and \a toLine, where asked for, are the lines the function
    // was written between: the line its own name is on, which is what a
    // reader means by a function's first line, and the line of what closed
    // its scope. Both counted from one, and both left alone where nothing
    // is written there to point at -- what whoever asks wants to know is
    // whether some other line is still inside this function.
    QString functionAt(int line, int column, int *fromLine = nullptr,
                       int *toLine = nullptr) const;

    // The last symbol declared at or before the position, which is how
    // Document decides what the cursor is inside of. Empty if there is none.
    QString lastVisibleSymbolAt(int line, int column) const;

    // The name of the innermost scope written around the position, empty if
    // that is the file itself. What Document::scopeAt answers.
    QString scopeAt(int line, int column) const;

    // The class the position is written in, written out in full, and empty
    // where the innermost scope written around it is not a class -- a
    // position in a member's *body* is in the function, not in the class.
    // What a reader pointing anywhere at a class body means by "this class".
    QString classAround(int line, int column) const;

    // A use of a function-like macro this file makes, and what it was handed
    // as written. QTEST_MAIN(tst_Simple) is one: a macro whose definition
    // nobody here may have, whose argument says which class a test runs --
    // so what is wanted is the text, not what it expands to.
    struct MacroUse
    {
        QString name;
        QStringList arguments; // as written, trimmed
    };

    // In the order they are written, and only this file's own: a header's
    // uses are the header's. A macro used without arguments is not among
    // them -- there is nothing to read off one.
    QList<MacroUse> macroUses() const;

    // A call this file makes to a function asked about, and the function it
    // is written inside. What a reader of a call's written arguments needs:
    // QTest::newRow("a tag") says which tag, quick_test_main(argc, argv,
    // "Name") says which name.
    struct WrittenCall
    {
        // Written out in full, the function the call is written inside.
        QString insideFunction;

        // One entry per argument, in order. Where an argument is a string
        // literal, what it says: the characters between the quotes, without
        // the prefix in front of them, with the pieces of an adjacent run
        // joined and every escape as it was written -- which is the text
        // somebody wrote down. Empty where the argument is anything else, so
        // that a caller wanting the third can count to it.
        QStringList arguments;

        // Where the called name stands, one-based.
        int line = 0;
        int column = 0;
    };

    // Every call to any of the functions called \a qualifiedNames, in the
    // order they are written.
    //
    // How the call was written does not matter: what it resolves to is what
    // is compared, so an unqualified call made reachable by a using
    // directive is found without anybody tracking the directive.
    QList<WrittenCall> callsTo(const QStringList &qualifiedNames) const;

    // The classes this file hands to calls of the function called
    // \a qualifiedName: the type of each call's first argument with the
    // pointer taken off, written out in full, in the order the calls are
    // written.
    //
    // What a test runner is handed, "QTest::qExec(&tst_Simple)" being how a
    // Qt test says which class it runs. An argument that is not a pointer is
    // no answer: a runner is handed an object, not a value.
    QStringList classesPassedTo(const QString &qualifiedName) const;

    // Where this translation unit declares the class called
    // \a qualifiedName, or nothing where it declares no such class.
    //
    // The headers count: one is read into whoever includes it, so a class a
    // file only includes is found here, in the header that writes it -- and
    // the place says which file that is. Where the class is named before it
    // is written out, the place is the one with the body, which is what a
    // reader sent to the class wants.
    Place classNamed(const QString &qualifiedName) const;

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

        Kind kind = Kind::Unknown;

        // Its type, printed the way an outline or a tooltip shows it: the
        // type with the name in it, so a function reads as its signature.
        // Empty for what has no type -- a namespace, a class.
        QString type;

        // Whether this place defines the thing, rather than only declaring
        // it. A class forward declared here and defined elsewhere, or a
        // function declared here and defined in another file, answers false
        // -- and a caller that wanted the definition, as follow symbol does,
        // then knows to keep looking rather than to send someone here.
        //
        // The definition is preferred where this document has both, so false
        // means the document does not have it at all.
        bool isDefinition = true;

        // What Qt makes of it, where the file was read as Qt: a signal is
        // emitted, a slot is connected to, and an invokable one is called
        // by name through the meta object. None for everything else.
        QtMethod qtMethod = QtMethod::None;

        // Whether a using declaration in this file brought the name in. The
        // built-in model answers such a name with the using declaration
        // itself rather than with what it names, which is what
        // QTCREATORBUG7903 asked for; a caller that has to agree with it can
        // tell from here that it must answer this one itself.
        bool throughUsingDeclaration = false;

        // A base of the class this function belongs to declares one of the
        // same name. Then which of them a call means may not be settled
        // here: a using declaration lending the base's overloads is
        // recorded nowhere, so the call is weighed against what the class
        // itself wrote and the base's are never in the running. Name hiding
        // makes the same answer right and the two cannot be told apart, so
        // a caller that would send somebody here is told to think again.
        bool siblingsInABaseClass = false;

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

    // What a reader hovering over a name is shown about the thing it names:
    // what kind of thing it is, what it is called, how it reads and where it
    // stands.
    //
    // The same name declarationAt resolves, said the way a tooltip shows it,
    // which takes a few things a declaration does not say: how an alias
    // reads as a type, what tells one overload from another, the enum an
    // enumerator belongs to with the value it stands for, and the class this
    // thing's type names -- what documentation a variable has is its type's.
    struct Element
    {
        Kind kind = Kind::Unknown;

        QString name;          // as it is written, nothing in front of it
        QString qualifiedName; // the scopes it is written in included

        // Its type with the qualified name written into it, which is how a
        // declaration is shown; and the same as a type on its own -- no
        // name for a parameter and nothing about what it hands back --
        // which is how an alias is shown, an alias being a type and not a
        // declaration. Both empty for a class or a namespace, which have
        // no type to show.
        QString declaration;
        QString type;

        // A function without what it hands back and without what it calls
        // its parameters: what tells one overload from another, and so what
        // documentation is marked with.
        QString signature;

        Utils::CodeModelIcon::Type icon = Utils::CodeModelIcon::Unknown;

        // Where it is declared, which for a function this file defines is
        // where the body is: what a reader following the name wants.
        Place place;

        // An enumerator stands for a value in an enum and is shown as both.
        // The value is the one written where it was declared, and nothing
        // where none was written -- it stands for one either way, but what
        // is shown is what somebody wrote.
        QString enumName;
        QString enumUnqualifiedName;
        QString enumeratorValue;

        // The class this thing's type names, written out in full, reached
        // through a pointer or a reference as readily as directly. Empty
        // where the type names no class.
        QString typeClassName;

        bool isValid() const { return !qualifiedName.isEmpty(); }
    };
    Element elementAt(int line, int column) const;

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

    // Every place this file names what is declared at \a declaration, which
    // may be in a header this file read: a header is read into the file that
    // includes it, so one unit holds both.
    //
    // This is find usages asked of one file, which is what a search over the
    // project asks of each file in turn. A name spelled the same and meaning
    // something else is not among the answers: every place is resolved and
    // compared with what was asked about by where the thing was first
    // declared -- the one place a declaration and a definition apart from it
    // agree on.
    //
    // The place asked about is among them where this file writes it, and a
    // place this file declares the same thing at -- the definition of a
    // function its header declares -- is too. Nothing where the position
    // declares nothing.
    struct NamedPlace
    {
        Occurrence place;
        bool isDeclaration = false; // it declares the thing rather than using it

        // The function the place is written in, written out in full, and
        // empty at file scope. What a list of call sites is grouped by, and
        // what a usages view shows beside a line.
        QString containingFunction;

        // What the place does with the thing: reads it, writes it, declares
        // it, hands out something that can write it. The same answer
        // FindUsages::GetUsageTags gives of the built-in tree, so that a
        // categorised search reads the same words either way. No tag at all
        // where the place is one this model has no rule for, which is what
        // the built-in says of such a place too.
        Usage::Tags tags;
    };
    //
    // Nothing where this unit writes the declaration's place but cannot say
    // what is declared there -- the parser records no symbol for a handler's
    // parameter, for one. That is not the same as a file with no usages in
    // it, and a caller that took it for one would quietly drop every place
    // in every file: silence read as absence is how a rename half-renames.
    std::optional<QList<NamedPlace>> usagesOf(const Place &declaration) const;

    // The member functions the class written around a position declares
    // without defining there, in the order they are written -- which is
    // what "put the definitions in the same order" compares against.
    //
    // A function a macro's replacement declared is not among them: nobody
    // wrote it where it stands, so there is no order to keep it in. Neither
    // is one defined inside the class, which is already where its
    // declaration is, nor a friend -- that is written in the class without
    // being one of its members, and its definition belongs with whatever
    // else the namespace it really lives in defines.
    struct MemberFunction
    {
        QString name; // written out in full, the scopes included

        // The same name with nothing in front of it, which is what a class
        // below writes when it declares one of its own.
        QString unqualifiedName;
        int parameterCount = 0;

        // How it reads where it is declared: its name and what it takes,
        // with what it hands back said apart rather than in front of it --
        // the two pieces a list of members shows, the way Overview prints
        // them apart. Empty for what hands nothing back.
        QString signature;
        QString returnType;

        // Where its own name stands, one-based, which is what a search for
        // its definition starts from. The file is this document's own
        // unless the class was declared in a header it read, which is why
        // it is said rather than assumed.
        QString filePath;
        int line = 0;
        int column = 0;

        // Declared with "= 0", which says that this class does not define it
        // -- so whoever is looking for the definitions of a class's members
        // is not looking for this one's.
        bool isPureVirtual = false;

        // Given a body right there, so its definition is where its
        // declaration is: whoever is putting definitions in order has
        // nothing to do about it, and whoever is offering what a class
        // below could implement still has.
        bool isDefinedHere = false;

        // What a class below it may do about it, and where a declaration
        // of it would go in that class.
        bool isVirtual = false;
        bool isFinal = false;
        Access access = Access::Public;
        QtMethod qtMethod = QtMethod::None;
    };
    // Every member function the class at a position declares -- the ones
    // it defines right there included, which say so. A reader putting
    // definitions in order wants the rest; one offering what a class
    // below could implement wants them all.
    //
    // \a inFile says which file the position is in, empty being this
    // document's own: a class somebody derives from is as often declared in
    // a header this file reads, and a header is read into whoever includes
    // it, so this document answers for that one too.
    QList<MemberFunction> memberFunctionsAt(int line, int column,
                                            const QString &inFile = {}) const;

    // A stretch of text this file writes, counted from one: where its
    // first token begins, and where its last one ends -- which is the
    // place just past its last character, as CxxAstRange says it too.
    struct Extent
    {
        int startLine = 0;
        int startColumn = 0;
        int endLine = 0;
        int endColumn = 0;

        bool isValid() const { return startLine > 0 && endLine > 0; }
    };

    // A call a meta object could make instead of whoever wrote it: what is
    // called on what, with what, and how much of the line says so. Every
    // piece of it is a place, since what the new call quotes is the text
    // the file has and not what this model would print.
    //
    // Nothing unless the position is on a member function call of
    // something Qt can invoke by name -- a signal, a slot, or a member
    // marked Q_INVOKABLE.
    struct MetaMethodCall
    {
        // An argument as Q_ARG takes it: where it is written, and the type
        // it has, which is the one thing here the file does not say.
        struct Argument
        {
            QString type;
            Extent written;
        };

        // What is replaced, taking in the "emit" or "Q_EMIT" in front of
        // the call where one stands there.
        Extent replaced;

        Extent base;
        // Whether what the call is made on is already a pointer, which is
        // what QMetaObject::invokeMethod() wants.
        bool baseIsPointer = false;

        QString methodName;
        QList<Argument> arguments;

        bool isValid() const { return replaced.isValid() && !methodName.isEmpty(); }
    };
    MetaMethodCall metaMethodCallAt(int line, int column) const;

    // The class written at a position, as the file about to give it away
    // reads it -- what moving a class to files of its own needs to know
    // about the file it stands in today.
    //
    // Nothing unless a class is written there: the position has to be in
    // the class's own declaration or on the name it is declared under,
    // which is where a reader asking for this has the cursor.
    struct ClassToMove
    {
        QString className;         // as written, without its scopes
        QString qualifiedName;     // with them, which is what names its parts
        QStringList namespacePath; // the namespaces around it, outermost first

        // The whole declaration, the template header included: what is
        // taken out of the file that writes it today.
        Extent declaration;

        // Whether this file writes anything besides the class. A class
        // that is all its file says is where it belongs already, and a
        // class named without being defined (class Foo;) is not something
        // the file says of its own.
        bool hasOtherDeclarations = false;

        bool isValid() const { return declaration.isValid(); }
    };
    ClassToMove classToMoveAt(int line, int column) const;

    // Everything this file writes that belongs to the class called
    // \a qualifiedName -- a member's definition, a nested class's body, a
    // static member's definition -- as the whole declaration around each,
    // the template header included, in the order they are written.
    //
    // A member written outside its class is written under that class's own
    // name, so that is what this looks for. A nested class comes along for
    // free, and so does everything written under it: those are written
    // under the class too.
    //
    // The class's own declaration is not among them, and neither is
    // anything written inside its body: what is asked for here is what
    // would be left behind.
    QList<Extent> partsOfClass(const QString &qualifiedName) const;

    // The using directive written at a position, and nothing where the
    // position is on none -- it has to be on the directive itself or on
    // the name it names, which is where a reader asking to remove one has
    // the cursor.
    //
    // Nothing either for one that names a nested namespace: what has to be
    // written in front of the names it found is then more than a name, and
    // the fix that reads this does not offer itself there.
    struct UsingDirective
    {
        QString namespaceName;
        Extent extent;

        // Written at global scope rather than in a block, which is what
        // makes it reach every file that includes this one.
        bool isAtGlobalScope = false;

        bool isValid() const { return extent.isValid(); }
    };
    UsingDirective usingDirectiveAt(int line, int column) const;

    // What taking "using namespace \a namespaceName" out of this file comes
    // down to: the directives for it whose lines go away, and every place
    // that has to write the namespace out once they are gone.
    //
    // Reading starts after \a afterLine and \a afterColumn -- a name written
    // before the directive never leaned on it -- and stops where the
    // directive's effect stops, at the end of the block or namespace body it
    // is written in. Zero for both means "after this file's own directive
    // for it at global scope", which is what a file that merely includes the
    // one holding it is asked.
    //
    // \a everyOneAtGlobalScope is the second thing there is to offer: every
    // directive for the namespace written at global scope goes, rather than
    // the one at the position.
    //
    // Which places need the namespace is the question only a front end that
    // resolves names can answer, and it is asked of the *first* component of
    // each name written: what stands after a :: is looked up in what stands
    // before it, so that is the only part a using directive can have found.
    struct UsingDirectives
    {
        QList<Extent> directivesToRemove;
        QList<Place> placesNeedingTheNamespace;

        // Whether the directive reaches whatever includes this file -- it
        // is written at global scope rather than in a block -- and whether
        // another one for the same namespace is still in force here once
        // this one is gone.
        bool isGlobalUsingNamespace = false;
        bool foundGlobalUsingNamespace = false;
    };
    UsingDirectives usingDirectivesOf(const QString &namespaceName, int afterLine,
                                      int afterColumn, bool everyOneAtGlobalScope) const;

    // A literal written inside a function, which "extract it as a parameter"
    // works on: its type, and every place that function writes the same
    // literal -- they all say the same thing, which is what makes them one
    // parameter.
    //
    // Nothing where the position is on no literal, or on one outside any
    // function. Nothing either where the front end recorded something other
    // than what stands at the literal's place, which is what the
    // preprocessor joining literals written next to each other looks like:
    // rewriting by that place would replace the wrong text.
    struct LiteralInAFunction
    {
        // In the order they are written, the one asked about among them.
        QList<Occurrence> places;

        // Printed for the scope the function stands in, since that is where
        // the parameter is going to be declared.
        QString type;

        bool isValid() const { return !places.isEmpty(); }
    };
    LiteralInAFunction literalInAFunctionAt(int line, int column) const;

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

    // That same type, written as a declaration of \a name and for the scope
    // the expression stands in: what a fix declaring a variable to hold the
    // value has to write there.
    //
    // Not the spelling typeAt() hands out with a name after it: how a
    // declarator is written around a name -- the star of a pointer, the
    // brackets of an array -- is not something a caller can work out from a
    // type. Empty in the same cases typeAt() is.
    QString declarationOfTypeAt(int line, int column, const QString &name) const;

    // The type declared at \a line and \a column -- where a declarator
    // writes its name -- as a declaration of \a name, written for the place
    // \a writtenAt. For a function it is the type it hands back, since that
    // is the part written in front of its name.
    //
    // Two callers want two different things of it. One is about to write a
    // declaration of the same thing somewhere else, and \a writtenAt is
    // where: a type needs more of its path in one place than in another. The
    // other is rewriting the declaration where it stands, and hands over the
    // name as it is written -- the qualification in front of it and the
    // spacing of an operator included -- so that nothing of what somebody
    // wrote is lost.
    //
    // Empty where nothing is declared at the position: a name that declares
    // something is not a use of it, so this asks the declaration rather than
    // whatever stands there.
    // \a settings says where the spaces of a pointer or a reference go,
    // for a caller that has a style of its own rather than the project's --
    // a reformatting of declarations is about those spaces, and a page
    // previewing a setting shows one nobody has chosen yet. The document's
    // own settings where it is nothing.
    // \a parameterNames are the names to write the parameters of a
    // function type under, in the order they are written. A parameter's
    // name is not part of a type, so a caller rewriting "char *(*f)(int n)"
    // has to hand the n over or it is lost.
    QString typeDeclaredAt(int line, int column, const QString &name,
                           const Place &writtenAt,
                           const std::optional<Overview> &settings = {},
                           const QStringList &parameterNames = {}) const;

    // The function written around a position, as a reader about to write
    // another one beside it needs it.
    struct EnclosingFunction
    {
        QString name; // as written, without its scopes
        Place namePlace;

        // The whole definition: what the new one is written in front of.
        Extent definition;

        bool isConst = false;

        // The class it is a member of, where it is one: what stands in
        // front of its name today -- "NS::C::", which the new function
        // writes as well -- and where that class writes its own name, so
        // that a declaration can be put in its body.
        bool isMemberFunction = false;
        Extent writtenQualifier;
        Place classNamePlace;

        // Whether the definition is written inside the class's own body,
        // where a second definition needs no qualification at all and the
        // declaration it would be given is a second one. Nothing reads
        // this yet but to hand back.
        bool isWrittenInAClass = false;

        bool isValid() const { return definition.isValid(); }
    };
    EnclosingFunction enclosingFunctionAt(int line, int column) const;

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

        // One of the functions a call being written could be of: how it
        // reads, and where each of its parameters stands in that -- what a
        // reader marks as the argument being typed, which is not the one
        // the parser saw a moment ago but whatever has been written since.
        struct Signature
        {
            QString text;

            struct Parameter
            {
                int start = 0;
                int length = 0;

                bool isValid() const { return length > 0; }
            };
            // In the order they are written. A parameter the printer wrote
            // in a way this could not find again stands here with nothing
            // to mark rather than not at all, so that the rest keep their
            // places.
            QList<Parameter> parameters;
        };

        // Inside the parentheses of a call both apply at once: a name can be
        // written there, and the call it belongs to has a signature worth
        // showing. So the hints sit beside the names rather than instead of
        // them, which is also how an editor shows them.
        QList<Signature> signatures;
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

    // Whether the function whose name is written at a position is virtual,
    // and where the declarations that first made it so are written.
    //
    // "First" is by how far up the hierarchy they stand: what a reader
    // following a virtual call is offered is the declarations furthest up
    // that say "virtual", and the function itself where it is the one
    // saying it. A base that declares it final ends the search, since
    // nothing below it overrides anything.
    //
    // Said, not merely being: a function that overrides a virtual one is
    // virtual whether it writes the word or not, and what is wanted here is
    // where somebody wrote it.
    //
    // The bases are looked at rather than looked up: a class's bases are
    // read into the file that writes it, so they are all in reach here.
    struct Virtuality
    {
        // A declaration that said it, and the class that wrote it -- named
        // with nothing in front of it, since what a reader does about one
        // of these can turn on which class it is. QObject declares three
        // that nobody implements by hand.
        struct FirstVirtual
        {
            Place place;
            QString className;
        };

        bool isVirtual = false;
        bool isPureVirtual = false;
        QList<FirstVirtual> firstVirtuals;

        // Whether a function is written at the position at all, which is
        // what tells "not virtual" from "nothing to say".
        bool namesAFunction = false;
    };
    // \a inFile says which file the position is in, empty being this
    // document's own: the function asked about is as often one a header
    // this file reads declares.
    Virtuality virtualityAt(int line, int column, const QString &inFile = {}) const;

    // The members of the class whose name is written at \a classPlace that
    // override the function declared at \a function: same name, what it
    // takes and whether it may be called on a const object -- not what it
    // hands back, which cannot tell two overrides apart.
    //
    // Both places are in this unit, which is what makes the comparison
    // possible at all: a class that derives from another reads the header
    // declaring it, so the two functions are written down by one front end
    // and compared without anything having to be spelled out and matched.
    QList<Place> overridesIn(const Place &classPlace, const Place &function) const;

    // What the class written at a position inherits, and what those inherit
    // in turn: the hierarchy upwards, which is the other half of what a type
    // hierarchy shows.
    //
    // A flat list, each entry saying which one it is a base of, because that
    // is how the symbols themselves are laid out here and a tree of them is
    // the caller's to build. The order is the order they are written in.
    //
    // The bases are looked at rather than looked up: a class's bases are
    // read into the file that writes it, so a base declared in a header this
    // file included is here with the place it writes its name.
    struct BaseClass
    {
        QString qualifiedName;
        Place place;

        // Which entry it is a base of, as an index into the list, or -1 for
        // a base of the class asked about.
        int parent = -1;
    };
    QList<BaseClass> basesOfTheClassAt(int line, int column) const;

    // Every class this file writes, with what each of its bases resolves to
    // written out in full: what a search for the classes deriving from a
    // particular one compares against.
    //
    // A base named through an alias is the class the alias stands for, and
    // nothing here has to follow one -- the parser did. Two classes of the
    // same name in different namespaces are told apart by the path, which is
    // the whole reason the bases are written out rather than named.
    struct ClassWithBases
    {
        QString qualifiedName;
        Place place;       // where the class writes its name, in this file
        QStringList bases; // written out in full
    };
    QList<ClassWithBases> classesWithTheirBases() const;

    // A class that uses another one: it declares a member of that type -- a
    // value, a pointer or a reference to it -- or it derives from it. That
    // is how the class a form belongs to is told from the class uic writes
    // for the form, the one being written into the other.
    struct ClassUsingAClass
    {
        QString name;          // its own name, with nothing in front of it
        QString qualifiedName; // written out in full
        Place place;           // where its name stands in its own body
    };

    // Every class this translation unit writes that uses \a className, which
    // is a path written out in full ("Ui::Form"), in the order they are
    // written.
    //
    // The headers this file reads are among them: a header is read into
    // whoever includes it, so a class declared in one is written here too,
    // and the place says which file it is in. A caller that cares how far
    // from this file a class may stand reads that.
    //
    // A type nothing declares names no class here, where the built-in front
    // end takes it for one of that name -- so a form whose ui header was
    // never generated, and which nothing forward declares either, is a class
    // this answers nothing about.
    QList<ClassUsingAClass> classesUsing(const QString &className) const;

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
