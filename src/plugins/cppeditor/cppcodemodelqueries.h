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

// The same four questions, asked of the model manager's reading as it stands
// rather than of one the caller holds.
//
// That is what a consumer reacting to the cursor wants: it has no reading of
// its own, and what it is being asked about is on screen now. A consumer that
// works through a project on a thread of its own passes its own reading to
// the overloads above instead, so that every answer it collects is about the
// same one -- and it is the only kind of consumer that needs to name a front
// end's snapshot at all.
CPPEDITOR_EXPORT EnclosingFunction functionAround(const Utils::FilePath &filePath,
                                                  int line, int column);
CPPEDITOR_EXPORT QString functionNamedAt(const Utils::FilePath &filePath,
                                         const QTextCursor &cursor);
CPPEDITOR_EXPORT QString nameResolvedAt(const Utils::FilePath &filePath,
                                        const QTextCursor &cursor);
CPPEDITOR_EXPORT QString classAround(const Utils::FilePath &filePath, int line, int column);

// The files \a filePath includes, each as the file it was resolved to, in the
// order they are written. Empty where nothing has read \a filePath.
//
// Answered off a built-in reading where the indexing pass left one, and off
// the cxx index's include graph otherwise -- a node per file it covered, so
// a header is answered for as readily as a source. The two differ in one
// way: a reading reports the include *lines*, so a file naming the same
// header twice is there twice, where the graph has it once.
//
// Nothing is read here. Whoever asks is drawing a diagram of a project's
// files, and reading them to find out would be a parse apiece.
CPPEDITOR_EXPORT Utils::FilePaths includesOf(const Utils::FilePath &filePath);

// The same, off the reading handed in rather than the model manager's own
// -- which is a copy of a hash of every document it holds, so a caller
// asking about file after file pays that per file.
CPPEDITOR_EXPORT Utils::FilePaths includesOf(const CPlusPlus::Snapshot &snapshot,
                                             const Utils::FilePath &filePath);

// The files that include a header called \a fileName, once each, in no
// particular order.
//
// By the name it is included under rather than by a path, and whether or not
// the include resolved to anything: a header uic writes is included by name
// long before a build has written it, and finding who includes it is how the
// class behind a form is found. A reading is taken here rather than the model
// manager's, because whoever asks means a particular set of files -- the
// project a form belongs to, say.
//
// Both front ends answer: \a snapshot's own documents, and the files the cxx
// index has read, which is every file of a project where no built-in pass
// fills that snapshot. The index is project-wide and knows nothing of the
// set \a snapshot stands for, so what comes back has to be sifted by a
// caller that means one project's files. And the index knows an include by
// the file it resolved to, so a header nothing has generated yet is found
// through the snapshot alone.
CPPEDITOR_EXPORT Utils::FilePaths filesIncludingFileNamed(const CPlusPlus::Snapshot &snapshot,
                                                          const QString &fileName);

// Where a file writes an include: the file, and the line the include
// stands on -- 0 where that is not known.
struct WrittenInclude
{
    Utils::FilePath file;
    int line = 0;
};

// The files that include \a filePath themselves, each with the line it
// writes the include on.
//
// What either model knows, together, for the same reason filesDependingOn()
// below asks both: without a built-in pass \a snapshot holds the open
// editors and the files they include, and the index holds a project's files
// and not what is open from elsewhere.
//
// The line is the snapshot's to give. The index's include graph records
// which file reached which and not where the line stands, so a file only it
// knows comes back with line 0 -- which is where a reader following the
// entry lands. Better than the entry not being there at all, which is what
// a session with no pass had.
CPPEDITOR_EXPORT QList<WrittenInclude> filesIncluding(const CPlusPlus::Snapshot &snapshot,
                                                      const Utils::FilePath &filePath);

// The files that reach \a filePath through their includes, directly or
// through headers of their own -- what a search for the uses of something
// declared there has to look through.
//
// What either model knows, together. \a snapshot is what a built-in
// indexing pass left behind, and the answer this question has always had;
// the cxx index's include graph is the answer where no such pass runs.
//
// Both and not the first that answers, because neither is complete on its
// own and the ways they are incomplete do not overlap: without a pass the
// snapshot holds the open editors and the files they include, so it has a
// header somebody opened and knows next to nothing of what reaches it,
// while the index holds a project's files and not what is open from
// elsewhere. A file too many here costs a file searched; a file missing is
// a use not found.
//
// Snapshot::filesDependingOn() is not cheap: a snapshot is handed out with
// no dependency table, so the first ask builds one of every file against
// every other -- and snapshot() hands out a copy, so every caller is the
// first.
CPPEDITOR_EXPORT Utils::FilePaths filesDependingOn(const CPlusPlus::Snapshot &snapshot,
                                                   const Utils::FilePath &filePath);

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
    //
    // Answered without reading anything where it can be: the index says
    // which of the files \a filePath reads writes that class, and the class's
    // own tokens say the rest -- an access section, the word that marks one
    // as Qt's and a base clause are all there in the text, which is how moc
    // reads them too. That is what clangd does with a cross-file question,
    // and what makes a scan over a project affordable: asking a front end is
    // a parse of a file and every header it reaches, and this is asked of
    // every test class and of every class those derive from.
    //
    // Where that leaves the text and a reading saying different things, the
    // text is taken -- a slot declared in a branch this configuration does
    // not build is among them, the way a macro use in one is, and a slot's
    // signature carries its parameter list as written where a reading writes
    // the types alone. A class a macro's body writes has no tokens to read,
    // and is answered for by a reading as before.
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

    // The uses \a filePath makes of any of the macros called \a names, in
    // the order they are written, and what each was handed as written. A use
    // with no arguments is not among them: there is nothing to read off one.
    //
    // Read off the file's own tokens rather than off either front end: which
    // macro was used and what it was handed is what the text says, so a
    // reading -- a parse of the file and every header it reaches -- buys
    // nothing. That is why the names are asked for, too: a lexer cannot tell
    // a macro use from a call of the same name, and every reader of this is
    // looking for macros it can name.
    //
    // As the text has it, which differs from a preprocessed reading in two
    // ways, both of which suit a reader asking what a file says: a use in a
    // branch this configuration does not build is among them, and one
    // written inside a #define is not -- that names nothing, what stands
    // there being the definition's own parameter.
    QList<WrittenMacroUse> macroUsesIn(const Utils::FilePath &filePath,
                                       const QStringList &names) const;

    // Every file \a filePath reaches through its includes -- the headers of
    // its headers included, itself left out -- each as the file it was
    // resolved to, once each and in no particular order.
    //
    // What a reader asking whether a file reaches some header at all needs:
    // a test framework is known by the header its macros come from, however
    // deep the include that brings it in. Unlike includesOf() below there is
    // a front end to choose between here, a closure being what each model
    // read rather than bookkeeping either could hand over: the cxx-frontend
    // reading resolves its own includes, and the built-in answer is only as
    // complete as what a pass has parsed.
    Utils::FilePaths includeClosureOf(const Utils::FilePath &filePath) const;

    // The files \a filePath includes itself, off whichever model has it:
    // the reading passed in, and the index's include graph where that
    // reading has never heard of the file. Empty where neither has.
    //
    // What a caller walking a whole project's includes needs, and why this
    // is here rather than the free includesOf() below being used: that one
    // takes a fresh copy of the model manager's snapshot per call, which is
    // a hash of every document it holds.
    //
    // One step, so that such a walk can keep one set of the files it has
    // been to. Asking for each file's whole closure instead re-walks what
    // the closures share, which for a project's sources is nearly all of
    // it.
    Utils::FilePaths directIncludesOf(const Utils::FilePath &filePath) const;

    // The same, but only where it is already known -- from the reading
    // passed in or from the index -- and nothing where saying would mean
    // reading the file.
    //
    // For a caller that cannot pay a parse where the answer is missing: the
    // question is asked of a whole project's files at once, or on the thread
    // that draws, and an incomplete answer there costs less than a freeze.
    std::optional<Utils::FilePaths> includeClosureKnownFor(
        const Utils::FilePath &filePath) const;

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
    // call's first argument points at, once each and in the order the calls
    // are written.
    //
    // What a Qt test's main() says by calling QTest::qExec(&tst_Simple): the
    // class to run. An argument that is not a pointer is none of them, a
    // runner being handed an object rather than a value.
    //
    // Answered off the file's own tokens where it can be: which class is
    // handed over is what the text says -- "tst_Simple test;" above the call,
    // a class made right there, or a pointer that already holds one -- and
    // most of the files this is asked about hand over nothing at all, calling
    // no runner. Only where the text does not settle what the first argument
    // is does a front end read the file.
    //
    // As the text has it, then, and on purpose a superset: a call written
    // with as much in front of it as \a functionName ends in is one of them,
    // so a runner reached through a using directive counts.
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
