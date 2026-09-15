// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppeditor_global.h"
#include "cpprefactoringchanges.h"
#include "cppworkingcopy.h"
#include "insertionpointlocator.h"

#include <utils/filepath.h>
#include <utils/link.h>

#include <QList>
#include <QString>

#include <memory>

QT_BEGIN_NAMESPACE
class QTextCursor;
QT_END_NAMESPACE

namespace CPlusPlus { class Snapshot; }

namespace CppEditor {

// Questions about what the code says, asked and answered without a symbol of
// any front end's: a place goes in and places come out.
//
// That is what lets a plugin outside this one read the code model at all --
// the cxx-frontend model is this plugin's own, so a consumer elsewhere cannot
// choose between the two front ends itself. These answer off whichever one
// has the file, the way the locator's place-based overloads already do, and a
// caller is none the wiser.

// Where a class's name stands in its own body, which is what a front end
// records a class at and what the locator is asked with.
struct CPPEDITOR_EXPORT WrittenClass
{
    QString name;              // its own name, with nothing in front of it
    QString qualifiedName;     // and the same written out in full
    Utils::FilePath filePath;
    int line = 0;              // counted from one
    int column = 0;

    bool isValid() const { return line > 0; }
};

// A member function a class declares: how it reads, and where its own name
// stands -- which is the place the questions below are asked with.
struct CPPEDITOR_EXPORT WrittenFunction
{
    QString name;              // its own name, with nothing in front of it

    // Its name and the types it takes, as they would be written:
    // "f(int, const QString &)". Two front ends do not write a type the same
    // way, so compare these through QMetaObject::normalizedSignature(),
    // which reduces both to the same.
    QString signature;

    Utils::FilePath filePath;
    int line = 0;
    int column = 0;
};

// Something a file declares, as a reader listing what is in a file needs it:
// how it reads, what stands for it, where it is written, and what it is
// written inside.
struct CPPEDITOR_EXPORT WrittenDeclaration
{
    QString name;              // its own name, with nothing in front of it,
                               // and empty for a scope written without one
    // What would be written after the name, which is what tells two things
    // of one name apart: a function's parameter list, anything else's type,
    // and for a scope its own name over again -- a scope stands under the
    // name and has nothing to add to it.
    QString type;
    int iconType = -1;         // Utils::CodeModelIcon::Type

    Utils::FilePath filePath;
    int line = 0;              // counted from one, the column too
    int column = 0;

    // What this is declared inside, as an index into the list it came from,
    // or -1 for the file itself. The list has a scope before its members, so
    // this always points backwards.
    int parent = -1;

    // A namespace, which a reader is shown for what is written in it rather
    // than for itself.
    bool isNamespace = false;
};

// The function a place is written inside of, and the lines it was written
// between -- which is what says whether some other line is still inside the
// same function.
struct CPPEDITOR_EXPORT EnclosingFunction
{
    QString qualifiedName;  // empty where the place is inside no function
    int fromLine = 0;       // counted from one, and zero where nothing is
    int toLine = 0;

    bool isValid() const { return !qualifiedName.isEmpty(); }
};

// Answered off whichever front end has already read \a filePath, with
// \a snapshot standing for the built-in one's reading of it.
//
// Nothing is read here. Whoever asks is showing a tooltip or following a
// stack frame, and a parse would happen on the thread that has to answer, so
// a file no front end has read is a place no function is known around.
CPPEDITOR_EXPORT EnclosingFunction functionAround(const CPlusPlus::Snapshot &snapshot,
                                                  const Utils::FilePath &filePath,
                                                  int line, int column);

// The function the name under \a cursor stands for, written out in full --
// "N::C::f", with nothing after it -- or empty where the name is of something
// else, or of nothing this front end resolved.
//
// A cursor rather than a place, because reading the expression written there
// is how the built-in front end answers this and the text is what a cursor
// carries; \a filePath says which file it is in. The cursor is taken as it
// comes and moved to the end of the name here, since that is what reading an
// expression backwards from it needs.
CPPEDITOR_EXPORT QString functionNamedAt(const CPlusPlus::Snapshot &snapshot,
                                         const Utils::FilePath &filePath,
                                         const QTextCursor &cursor);

// The same, for a name of anything: what the name at \a cursor resolves to,
// written out in full, whatever kind of thing it names -- or empty where this
// front end resolved nothing there.
//
// What a reader needs who is asking *which* thing a name means rather than
// what it is: whether the "disabled" in a Boost decorator is
// boost::unit_test::disabled, however the file reached that name -- through a
// using declaration, a namespace alias, or written out.
CPPEDITOR_EXPORT QString nameResolvedAt(const CPlusPlus::Snapshot &snapshot,
                                        const Utils::FilePath &filePath,
                                        const QTextCursor &cursor);

// The class \a line and \a column of \a filePath are written in, written out
// in full, or empty where what is written around them is no class -- a place
// in a member's *body* is in the function rather than in the class.
//
// Answered off whichever front end has already read the file, and nothing is
// read here either.
CPPEDITOR_EXPORT QString classAround(const CPlusPlus::Snapshot &snapshot,
                                     const Utils::FilePath &filePath, int line, int column);

// The files \a filePath includes, each as the file it was resolved to, in the
// order they are written. Empty where nothing has read \a filePath.
//
// Unlike the questions above this one takes no snapshot, because there is no
// front end to choose between: an include is resolved while preprocessing,
// and the cxx-frontend model is handed the resolutions the built-in reading
// made rather than making its own. So this is the model manager's own
// bookkeeping, and it is offered here so that a plugin wanting nothing more
// than the include closure need not know a front end at all.
CPPEDITOR_EXPORT Utils::FilePaths includesOf(const Utils::FilePath &filePath);

class CPPEDITOR_EXPORT CodeModelQueries
{
public:
    // \a workingCopy is what is being typed rather than what is on disk. It
    // has to be taken where the editor documents live, which is the GUI
    // thread, so it is handed in rather than fetched: the reading below may
    // run anywhere, and it reads files nobody has open.
    CodeModelQueries(const CPlusPlus::Snapshot &snapshot, const WorkingCopy &workingCopy);
    ~CodeModelQueries();

    // The class \a filePath -- or a file it includes, at most
    // \a maxIncludeDepth deep -- writes that uses \a className: one that
    // declares a member of that type, a value or a pointer to it, or one
    // that derives from it. \a className is written out in full, as
    // "Ui::Form" is.
    //
    // The first such class, the file itself before the files it includes.
    // Nothing where none of them writes one.
    WrittenClass classUsingClass(const Utils::FilePath &filePath, const QString &className,
                                 int maxIncludeDepth) const;

    // The member functions \a klass declares, in the order it declares them.
    QList<WrittenFunction> memberFunctionsOf(const WrittenClass &klass) const;

    // Every class \a filePath declares, nested ones included, in the order
    // it declares them. A class named without its body is not one of them:
    // there is nothing declared there to say anything about.
    QList<WrittenClass> classesDeclaredIn(const Utils::FilePath &filePath) const;

    // Everything \a filePath declares, in the order it declares them and
    // with a scope always before what is written inside it.
    //
    // What the file only names is not among them: a class named without a
    // body, something declared extern, a using declaration or directive, a
    // name a macro's body wrote, and a definition written under a qualified
    // name -- which declares nothing that was not declared where the name
    // was given. Neither is what a function writes inside itself, which is
    // nobody's business but that function's.
    QList<WrittenDeclaration> declarationsIn(const Utils::FilePath &filePath) const;

    // A class read for what a test runner needs of it: where it is written,
    // the slots it declares privately -- which is how a Qt test writes its
    // test functions -- and what it derives from.
    struct ClassWithPrivateSlots
    {
        // Where the class itself is written, invalid where the reading of
        // the file writes no class of that name. A class that is written
        // but declares no private slots is found, with an empty list.
        WrittenClass klass;

        // In the order the class declares them, each where its own name
        // stands -- which is not always in the same file as the class, a
        // header's class being read into whoever includes it.
        QList<WrittenFunction> privateSlots;

        // Written out in full, the ones the class names itself. What those
        // derive from is for whoever asks about them in turn.
        QStringList baseClasses;
    };

    // The class called \a className -- written out in full -- as the reading
    // of \a filePath has it, the headers it reads included.
    ClassWithPrivateSlots classWithPrivateSlots(const Utils::FilePath &filePath,
                                                const QString &className) const;

    // A use of a function-like macro, and what it was handed as written. A
    // macro whose definition nobody here has still says what it was given,
    // which is how QTEST_MAIN(tst_Simple) names the class a test runs.
    struct WrittenMacroUse
    {
        QString name;
        QStringList arguments; // as written, trimmed
    };

    // The function-like macro uses \a filePath makes, in the order they are
    // written. A use with no arguments is not among them: there is nothing
    // to read off one.
    QList<WrittenMacroUse> macroUsesIn(const Utils::FilePath &filePath) const;

    // A call to a function asked about, and the function it stands in: what
    // the tags a Qt test's data function writes are made of, and what a
    // runner call says the test is named.
    struct WrittenCall
    {
        QString insideFunction;  // written out in full

        // One entry per argument, in order: what it says where it is a
        // string literal -- the text between the quotes -- and nothing
        // where it is anything else, so that a caller wanting the third
        // can count to it.
        QStringList arguments;

        int line = 0;            // where the called name stands, from one
        int column = 0;
    };

    // Every call \a filePath makes to any of the functions called
    // \a functionNames -- written out in full -- in the order they are
    // written.
    //
    // How a call is written does not matter: what it resolves to is what is
    // compared, so a call made reachable by a using directive is among them.
    QList<WrittenCall> callsTo(const Utils::FilePath &filePath,
                               const QStringList &functionNames) const;

    // The classes \a filePath hands to calls of the function called
    // \a functionName -- written out in full -- each by the name of what the
    // call's first argument points at, in the order the calls are written.
    //
    // What a Qt test's main() says by calling QTest::qExec(&tst_Simple): the
    // class to run. An argument that is not a pointer is none of them, a
    // runner being handed an object rather than a value.
    QStringList classesPassedTo(const Utils::FilePath &filePath,
                                const QString &functionName) const;

    // What the locator needs of the function whose own name stands at \a line
    // and \a column of \a filePath. Nothing where no function is declared
    // there.
    DeclarationToDefine declarationToDefineAt(const CppRefactoringChanges &changes,
                                              const Utils::FilePath &filePath,
                                              int line, int column) const;

    // Where the project defines the function declared at \a line and
    // \a column of \a filePath -- where its own name stands, again -- or
    // nothing where no file defines it.
    Utils::Link definitionOfFunctionAt(const Utils::FilePath &filePath,
                                       int line, int column) const;

    // Where the project defines whatever is declared at \a line and
    // \a column of \a filePath, a variable as well as a function, or
    // nothing where nothing else defines it -- which is the answer for
    // anything that is not declared in one place and defined in another.
    //
    // Unlike the question above this one does not insist that the place be
    // the name's own: the last thing declared at or before it is the one
    // asked about, the way a reader's cursor lands.
    Utils::Link definitionOfWhatIsDeclaredAt(const Utils::FilePath &filePath,
                                             int line, int column) const;

private:
    class Private;
    const std::unique_ptr<Private> d;
};

} // namespace CppEditor
