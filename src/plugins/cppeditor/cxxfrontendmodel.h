// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppcursorinfo.h"
#include "cppfunctiondecldeflink.h"
#include "cppeditor_global.h"
#include "cppworkingcopy.h"
#include "indexitem.h"
#include "insertionpointlocator.h"
#include "semantichighlighter.h"

#include <cplusplus/CxxFrontendDocument.h>
#include <cplusplus/declarationcomments.h>

#include <texteditor/semantichighlighter.h>

#include <utils/filepath.h>
#include <utils/link.h>
#include <utils/utilsicons.h>

#include <functional>
#include <memory>
#include <optional>

namespace CPlusPlus {
class CxxFrontendSnapshot;
class Snapshot;
}

namespace CppEditor::Internal {

// The cxx-frontend model of the file being edited, kept beside the built-in
// one.
//
// The lexer could be swapped underneath its callers because they all read the
// same tokens. A document cannot: what the built-in model hands out is a tree
// of Symbol pointers, and every consumer walks it. So the way across is to
// run the new model over the files that are open, ask it the questions it can
// answer, and move the consumers one at a time -- and this is where the answers
// live in the meantime.
//
// Whether to keep it at all. Off unless QTC_CXX_FRONTEND_MODEL is set in the
// environment, because it is a second parse of everything the edited file
// includes, on every reparse. The built-in model is untouched either way:
// every consumer below reads this where it answers and that one where it
// does not, so with the variable unset nothing here runs at all.
//
// Asked by whoever is about to do the work rather than inside it, so that the
// work can be asked for directly -- which is how the test drives it.
bool cxxFrontendModelRequested();

// Runs \a filePath and everything it includes through the cxx-frontend model,
// and keeps the result until the next call for that file.
//
// Includes are not resolved again: \a builtinSnapshot has just been built for
// this file, and it records which path each include resolved to. Resolving
// them a second way would mean a second answer, and the point of running the
// two models side by side is that they read the same code. Contents come from
// \a workingCopy, so that what is being typed is what is parsed, and from disk
// for everything else.
//
// \a configFile is the #define lines the project part contributes, which the
// built-in model feeds in as a synthetic file.
void updateCxxFrontendModel(const CPlusPlus::Snapshot &builtinSnapshot,
                            const Utils::FilePath &filePath,
                            const QByteArray &configFile,
                            const WorkingCopy &workingCopy);

// What the model made of \a filePath the last time it ran over it, or nothing.
// The snapshot is shared and replaced wholesale by the next run, so hold what
// this returns for as long as the answers are needed.
std::shared_ptr<const CPlusPlus::CxxFrontendSnapshot> cxxFrontendModel(
    const Utils::FilePath &filePath);

// Drops what was kept for \a filePath, for when its parser lets go of its
// resources. Only the last few files parsed are kept in any case: a model
// holds a document per file in the include closure, and one per file ever
// edited is how a session runs out of memory.
void forgetCxxFrontendModel(const Utils::FilePath &filePath);

// Where the name at a position was declared, as a link the editor can follow.
// The first consumer, and a small one on purpose: a link is a file and a
// place, which is the whole of what the model has to produce.
//
// \a line is one-based and \a column zero-based, the way the editor counts.
// \a linkTextStart and \a linkTextEnd are the extent of the name in the
// document, which is what gets underlined; the model does not work them out,
// the caller already has them.
//
// An invalid link means the model has nothing to say -- it was never run over
// this file, the name is one of the things it cannot resolve
// (CxxFrontendSnapshot::unsupportedLookups()), it has only a declaration of
// what the name means and not the definition someone following it wants, or a
// using declaration brought the name in and the built-in answer for that is
// the using declaration itself.
// The caller then answers the way it did before, so this can only add
// answers, never change one.
Utils::Link cxxFrontendFollowSymbol(const CPlusPlus::Snapshot &builtinSnapshot,
                                    const Utils::FilePath &filePath, int line, int column,
                                    int linkTextStart, int linkTextEnd);

// Answers CPlusPlus::commentsForDeclaration() off this model, for as long as
// \a enabled: which comments are the documentation of the declaration at a
// position. Installed rather than called, the way the replacement lexer is,
// because the facility lives in a library that cannot depend on this one --
// and because its six readers ask it by position and should not each have to
// know which model answered.
//
// It declines the files this model has not read, and those where something
// other than comments stands between the comment block and the declaration,
// which its own token stream is what would say.
void useCxxFrontendComments(bool enabled);

// The other side of the function at a position: the definition where a
// declaration is there, the declaration where a definition is, as a link the
// editor can follow. What "Switch Between Function Declaration/Definition"
// asks, and what the decl/def link starts from.
//
// Inside one translation unit the model answers on its own -- a file being
// edited beside its header has both sides in reach. Where it does not, this
// looks for the definition the way SymbolFinder does: the project's files in
// the order the built-in snapshot puts them, nearest to this one first,
// skipping the ones whose parse never saw the name, each read by this model
// until one of them defines it. The files and the order are what the project
// knows and this model does not; what each file says is this model's answer.
//
// Nothing where the model has not read the file, where the position is on no
// function, or where no file in the project defines it.
std::optional<Utils::Link> cxxFrontendCounterpart(const CPlusPlus::Snapshot &builtinSnapshot,
                                                  const Utils::FilePath &filePath,
                                                  int line,
                                                  int column);

// Reads a file the way the editor has it. The model works in lines and
// columns and the decl/def link in the positions a QTextCursor counts, so
// turning one into the other takes the text -- and which file the other side
// of the function is in is what the call below is finding out, so the caller
// cannot be asked for it beforehand.
//
// Nothing where the file cannot be read. What comes back has to outlive the
// answer, which holds no text of its own.
using CxxFrontendFileText = std::function<const QTextDocument *(const Utils::FilePath &)>;

// The decl/def link on this model: what the two sides of the function at a
// position say, where each part of the other side is written, and how to
// read the side being edited as it now stands.
//
// One document answers all of it, because one translation unit holds both
// sides: a header is read into the file that includes it, so the source file
// that defines a function holds the header's declaration as well. Which
// document that is depends on what is being edited -- a source file's own
// will do, a header's will not, and then the source file that defines the
// function is read instead, found the way cxxFrontendCounterpart() finds it.
//
// Nothing where the model has not read the file, where the position is on no
// function, or where no file in the project defines it; the caller then
// answers the way it did before.
struct CxxFrontendDeclDefLink
{
    Utils::FilePath targetFilePath;

    // Where the other side's own name stands, one-based as the model counts:
    // where somebody jumping to it lands, and what its documentation is
    // looked for above.
    int targetNameLine = 0;
    int targetNameColumn = 0;
    QString targetShortName;

    FunctionSignature sourceSignature;
    FunctionSignature targetSignature;
    WrittenDeclaration targetWritten;

    // Reads the declaration the cursor covers as it now stands in the
    // editor. Unlike every other question here it cannot be read off the
    // last parse: what is wanted is the text of this keystroke, so the file
    // that holds both sides is read again with it. The last reading is kept,
    // so asking again about text nobody has changed costs nothing.
    //
    // What it does cost, once per edit, is a parse of the file and
    // everything it includes -- a third of a second for a translation unit
    // of any size. It takes what the editor said rather than the editor's
    // own cursors, so that whoever asks can ask from a thread nobody is
    // typing on.
    std::function<std::shared_ptr<EditedDeclaration>(const EditedDeclarationRequest &request)>
        readEditedDeclaration;
};

std::optional<CxxFrontendDeclDefLink> cxxFrontendDeclDefLink(
    const CPlusPlus::Snapshot &builtinSnapshot, const Utils::FilePath &filePath,
    int line, int column, const WorkingCopy &workingCopy,
    const CxxFrontendFileText &textOf);

// The function declared at a position: where its name stands, whether what
// stands there is its definition rather than its declaration, and where the
// declaration it belongs to begins -- which is where its documentation is
// written above, and where documentation moved to it goes.
//
// The start is the outermost of the declarations written directly around it,
// so that a template function's comment goes above the template and not
// between it and the function.
struct CxxFrontendFunctionDeclaration
{
    // Which file the answer is about, which is not always the file asked:
    // the declaration of a function defined here may be in another one.
    Utils::FilePath filePath;

    // The name it is declared under, without the scopes in front of it and
    // past a destructor's tilde, which is where a link into a function
    // points. Where it begins and where it ends, so that whoever has the
    // text can read the name itself off it -- an operator is written under
    // a name too, and spelling each kind out here would only be a second
    // way of saying what the file says.
    int nameLine = 0; // one-based, as the model counts
    int nameColumn = 0;
    int nameEndLine = 0;
    int nameEndColumn = 0;

    int startLine = 0;
    int startColumn = 0;

    // And where it stops, so that the whole of it can be moved: the
    // template it is declared under is part of it, the documentation
    // written above it is not -- that is a question about comments, and
    // commentsForDeclaration() answers it.
    int endLine = 0;
    int endColumn = 0;

    bool isDefinition = false;

    // Where a definition's head stops and its body ends, so that the body
    // can be written out again as it stands. Both zero for a declaration,
    // which has none.
    int bodyStartLine = 0;
    int bodyStartColumn = 0;
    int bodyEndLine = 0;
    int bodyEndColumn = 0;

    // A definition written "= default" is the one that does not end in a
    // body, so the ";" that closed it has to be written after it.
    bool endsWithSemicolon = false;

    // Written inside a class rather than at namespace scope. What decides
    // whether a definition put in its place has to say "inline": a member
    // defined in its class is inline already and a free function is not.
    bool isWrittenInAClass = false;

    // Just before the ')' of its parameter list, which is where another
    // parameter is appended, and whether it has any -- which decides
    // whether a comma goes in front of the new one.
    int parametersEndLine = 0;
    int parametersEndColumn = 0;
    bool hasParameters = false;

    bool isValid() const { return nameLine > 0 && startLine > 0; }
};

// Nothing where the model cannot read \a filePath at all, and then the
// caller answers the way it did before; an invalid answer where the position
// is on no function declaration.
//
// A file the model has not been run over is read here and now, which the
// editor's own questions must not do -- they are asked while somebody is
// typing. This one is asked by a fix somebody has already chosen, and about
// the other side of a function, which is a file nobody is editing.
std::optional<CxxFrontendFunctionDeclaration> cxxFrontendFunctionAt(
    const CPlusPlus::Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
    const Utils::FilePath &filePath, int line, int column);

// Where each of \a functions is defined, in the same order, with an invalid
// answer where one is not defined anywhere the search reached.
//
// Asked of all of them at once rather than one at a time, because reading a
// file is the expensive part: the files are read once each and asked about
// every name still outstanding. This file's own translation unit first, which
// already holds a function defined in the header it is declared in.
QList<CxxFrontendFunctionDeclaration> cxxFrontendDefinitionsOf(
    const CPlusPlus::Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
    const Utils::FilePath &filePath,
    const QList<CPlusPlus::CxxFrontendDocument::MemberFunction> &functions);

// The class written at a position, as the file about to give it away reads
// it, and nothing where this model has not read that file.
std::optional<CPlusPlus::CxxFrontendDocument::ClassToMove> cxxFrontendClassToMoveAt(
    const Utils::FilePath &filePath, int line, int column);

// A stretch of text that belongs to a class though it stands outside it,
// and which file writes it.
struct CxxFrontendClassPart
{
    Utils::FilePath filePath;
    CPlusPlus::CxxFrontendDocument::Extent extent;
};

// Everything the project writes that belongs to the class called
// \a qualifiedName -- a member's definition, a nested class's body, a
// static member's definition -- which is what moving that class to files of
// its own has to carry along.
//
// Every file that writes the class's name is read, since there is no count
// to stop at: a class's parts can be spread over as many files as somebody
// chose to put them in. What keeps that from reading the project is the
// filter the rest of this file uses -- the built-in parse of a file says
// which identifiers it wrote, and a file that never wrote this name cannot
// define a part of it.
QList<CxxFrontendClassPart> cxxFrontendPartsOfClass(
    const CPlusPlus::Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
    const Utils::FilePath &filePath, const QString &qualifiedName);

// What \a filePath declares, in the order it declares them, and nothing
// where this model cannot read the file. Read here and now where the editor
// is not running over it, as the declaration answer below is.
//
// One entry per thing declared, at the place it is declared: a function
// declared and defined in one file is one entry, which is what the outline
// shows too.
std::optional<QList<CPlusPlus::CxxFrontendDocument::Symbol>> cxxFrontendSymbolsIn(
    const CPlusPlus::Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
    const Utils::FilePath &filePath);

// What the name at a position means: where it was declared, what kind of
// thing it is and what its type reads as. Nothing where this model has not
// read the file or the position is on no name it resolved.
//
// The raw answer, without follow symbol's rules on top of it: whoever wants
// to *go* somewhere asks cxxFrontendFollowSymbol(), and whoever wants to say
// what something is asks this.
std::optional<CPlusPlus::CxxFrontendDocument::Declaration> cxxFrontendDeclarationAt(
    const Utils::FilePath &filePath, int line, int column);

// What a reader hovering over the name at a position is shown about the
// thing it names. Nothing where this model has not read the file or the
// position is on no name it resolved, and then the built-in reading answers.
std::optional<CPlusPlus::CxxFrontendDocument::Element> cxxFrontendElementAt(
    const Utils::FilePath &filePath, int line, int column);

// The same for a file nobody has open, read here and now: what an answer
// asked for out of band -- by the MCP server, say -- is about. One file read
// per question is the cost, which is the trade for asking about a file the
// editor is not running over.
std::optional<CPlusPlus::CxxFrontendDocument::Declaration> cxxFrontendDeclarationIn(
    const CPlusPlus::Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
    const Utils::FilePath &filePath, int line, int column);

// The class whose name is written at \a place, out of the file's own
// parse. A place is what either front end can say and a Symbol is what
// the built-in one draws with, so this is how a class the model found is
// handed to something that still works in symbols.
//
// Nothing where that file is not in \a snapshot or declares no class
// there.
CPlusPlus::Class *builtinClassWrittenAt(
    const CPlusPlus::Snapshot &snapshot,
    const CPlusPlus::CxxFrontendDocument::Place &place);

// The same out of a particular parse, for a caller whose other symbols
// came from that one: what the snapshot holds for a file is not the same
// objects as what an editor parsed of it, and two symbols of one class
// from two parses are not each other.
CPlusPlus::Class *builtinClassWrittenAt(
    const CPlusPlus::Document::Ptr &document,
    const CPlusPlus::CxxFrontendDocument::Place &place);

// What the class written at a position inherits, and what those inherit in
// turn, and nothing where this model cannot read the file.
//
// A class's bases are read into the file that writes it, so one document
// holds the whole hierarchy upwards -- read here and now where the editor is
// not running over that file, since whoever asks this is drawing a hierarchy
// rather than typing.
std::optional<QList<CPlusPlus::CxxFrontendDocument::BaseClass>> cxxFrontendBasesOfTheClassAt(
    const CPlusPlus::Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
    const Utils::FilePath &filePath, int line, int column);

// The members of the class written at \a classPlace in \a filePath that
// override the function declared at \a function, and nothing where this
// model cannot read the file.
//
// The file is read here and now unless the editor is running over it: the
// classes deriving from one are in files nobody has open.
std::optional<QList<CPlusPlus::CxxFrontendDocument::Place>> cxxFrontendOverridesIn(
    const CPlusPlus::Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
    const Utils::FilePath &filePath, const CPlusPlus::CxxFrontendDocument::Place &classPlace,
    const CPlusPlus::CxxFrontendDocument::Place &function);

// Whether the function whose name is written at a position is virtual, and
// where that was said. Nothing where this model has not read the file.
//
// Asked of the store only: whoever wants this is following a name in a file
// the editor is running over, and a class's bases are read into that file,
// so no other file has to be read to answer it.
//
// \a writtenIn says which file the position is in where that is not \a
// filePath itself -- a base class is as often declared in a header the
// edited file reads, and the document for that file answers about it.
std::optional<CPlusPlus::CxxFrontendDocument::Virtuality> cxxFrontendVirtualityAt(
    const Utils::FilePath &filePath, int line, int column,
    const Utils::FilePath &writtenIn = {});

// Every place \a filePath names what is declared at \a declaration, and
// nothing where this model cannot read the file -- which is then a file for
// the other one to read rather than one to leave out of a search.
//
// What find usages asks of each file it looks at. The declaration is a place
// in some other file as a rule, and that file is read into this one, so one
// document answers it.
std::optional<QList<CPlusPlus::CxxFrontendDocument::NamedPlace>> cxxFrontendUsagesIn(
    const CPlusPlus::Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
    const Utils::FilePath &filePath, const CPlusPlus::CxxFrontendDocument::Place &declaration);

// Every class \a filePath writes, with what each of its bases resolves to,
// and nothing where this model cannot read the file -- which is then a file
// for the other one to read rather than one to leave out.
//
// The file is read here and now unless the editor is running over it: a
// search for what derives from a class looks at files nobody has open.
std::optional<QList<CPlusPlus::CxxFrontendDocument::ClassWithBases>> cxxFrontendClassesIn(
    const CPlusPlus::Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
    const Utils::FilePath &filePath);

// The using directive written at a position, and nothing where this model
// has not read the file or the position is on no directive.
std::optional<CPlusPlus::CxxFrontendDocument::UsingDirective> cxxFrontendUsingDirectiveAt(
    const Utils::FilePath &filePath, int line, int column);

// What taking that directive out of \a filePath comes down to, and nothing
// where this model cannot read the file -- which is a file to be read by the
// other one rather than left alone.
//
// The file being edited comes out of the store, the rest are read here and
// now: removing a directive from a header reaches every file that includes
// it, and those are files nobody has open.
std::optional<CPlusPlus::CxxFrontendDocument::UsingDirectives> cxxFrontendUsingDirectivesIn(
    const CPlusPlus::Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
    const Utils::FilePath &filePath, const QString &namespaceName, int afterLine,
    int afterColumn, bool everyOneAtGlobalScope);

// Where the function at a position is *declared*, when that is somewhere
// other than the position itself -- which is what appending a parameter to a
// definition has to change as well.
//
// This translation unit first, which for a class member is where the
// declaration is: a header is read into the file that includes it. Otherwise
// the file that goes with this one, the header beside the source, which is
// where the built-in front end looks for a free function's declaration too.
//
// Nothing where the model cannot read the file; an invalid answer where the
// function is declared nowhere else, and then the definition is the only
// place there is.
std::optional<CxxFrontendFunctionDeclaration> cxxFrontendDeclarationOfFunctionAt(
    const CPlusPlus::Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
    const Utils::FilePath &filePath, int line, int column);

// The head of a definition of the function whose name is written at \a line
// and \a column of \a filePath, for the place \a targetLine and \a
// targetColumn of \a targetFilePath: everything it has to put in front of
// its body there, each name in it written with as little in front of it as
// still finds it from there.
//
// The two files may be two -- a definition goes from a header into the
// source file that includes it -- and then that source file is read, since
// reading it gives the one translation unit both places are in. Where they
// are the same file the model's own document answers and nothing is read.
//
// Nothing where the model has not read the file, and nothing where it cannot
// write the head: a definition under a template has to carry the
// "template<...>", which it does not write. Declining rather than writing
// half a definition is the rule for everything that moves text.
std::optional<QString> cxxFrontendDefinitionHeadFor(
    const CPlusPlus::Snapshot &builtinSnapshot, const Utils::FilePath &filePath,
    int line, int column, const Utils::FilePath &targetFilePath,
    int targetLine, int targetColumn);

// The function declared at \a line and \a column of \a filePath, written
// out as a declaration of \a name for the place \a targetLine and \a
// targetColumn of the same file: each name in it written with as little in
// front of it as still finds it from there.
//
// The name is the caller's, which is the difference from the head of a
// definition above: what a class writing a function of its own calls it is
// the caller's business, and the same function is "f" written inside a
// class and "C::f" written outside it.
//
// Nothing where the model has not read the file or cannot write the
// declaration -- a function under a template among them, since what it
// writes would be half of one.
// \a inFile is the document to ask, which is the file being edited; the
// function may be declared in a header it reads, and is then addressed by
// its own file.
std::optional<QString> cxxFrontendDeclarationHeadFor(
    const Utils::FilePath &inFile, const Utils::FilePath &functionFile,
    int line, int column, const QString &name, int targetLine, int targetColumn);

// The member functions the class at \a line and \a column of \a filePath,
// both counted from one, declares without defining there.
//
// Empty where the model has no such file and where the position is in no
// class, which a caller cannot tell apart -- and need not: with nothing to
// put in order there is nothing to offer either way.
QList<CPlusPlus::CxxFrontendDocument::MemberFunction> cxxFrontendMemberFunctionsAt(
    const Utils::FilePath &filePath, int line, int column);

// Every member function that class declares, the ones it defines right
// there included: what a reader offering what a class below could
// implement wants, where one putting definitions in order wants the rest.
//
// \a classFile says which file the class is written in and \a filePath is
// the document to ask -- the file being edited, which is the one that reads
// whatever header declares the class somebody derives from.
//
// Nothing where the model has not read that file, which is not the same as
// a class with nothing to offer.
std::optional<QList<CPlusPlus::CxxFrontendDocument::MemberFunction>>
cxxFrontendMemberFunctionsDeclaredAt(const Utils::FilePath &filePath,
                                     const Utils::FilePath &classFile, int line, int column);

// Files read with this model, and kept: a reader that asks several questions
// about one file pays for one parse.
//
// The store answers where the editor is running over the file; anything else
// is read then and there, since what asks here is not the editor -- a form's
// class is in whichever files the project has, open or not. A reading is
// worth what it cost, so it is worth keeping: a parse of a file and its
// headers is a third of a second.
//
// Every answer is nothing where the model is off or the file cannot be read,
// which is when the caller asks the built-in front end instead. An empty
// answer is an answer.
class CxxFrontendReading
{
public:
    // \a workingCopy has to be taken where the editor documents live.
    CxxFrontendReading(const CPlusPlus::Snapshot &builtinSnapshot,
                       const WorkingCopy &workingCopy);
    ~CxxFrontendReading();

    // The classes \a filePath writes that use \a className -- a member of
    // that type, or a base -- written out in full. The headers it reads are
    // among them, each class's place saying which file it is in.
    std::optional<QList<CPlusPlus::CxxFrontendDocument::ClassUsingAClass>> classesUsing(
        const Utils::FilePath &filePath, const QString &className) const;

    // Every member function the class whose name stands at \a line and
    // \a column of \a classFile declares, the ones it defines right there
    // included. \a filePath is the file to read, which is not always the one
    // the class is in: a header is read into whoever includes it.
    std::optional<QList<CPlusPlus::CxxFrontendDocument::MemberFunction>> memberFunctionsIn(
        const Utils::FilePath &filePath, const Utils::FilePath &classFile,
        int line, int column) const;

    // What \a filePath declares, outermost first, each scope's members
    // after it.
    std::optional<QList<CPlusPlus::CxxFrontendDocument::Symbol>> symbolsIn(
        const Utils::FilePath &filePath) const;

    // The function-like macro uses \a filePath makes, and what each was
    // handed as written.
    std::optional<QList<CPlusPlus::CxxFrontendDocument::MacroUse>> macroUsesIn(
        const Utils::FilePath &filePath) const;

    // Every call \a filePath makes to any of \a functionNames, with what
    // each argument says where it is a string literal.
    std::optional<QList<CPlusPlus::CxxFrontendDocument::WrittenCall>> callsIn(
        const Utils::FilePath &filePath, const QStringList &functionNames) const;

    // The classes \a filePath hands to calls of the function called
    // \a functionName, each written out in full.
    std::optional<QStringList> classesPassedToIn(const Utils::FilePath &filePath,
                                                 const QString &functionName) const;

    // Where the reading of \a filePath writes the class called
    // \a className -- a header's counts, being read into whoever includes
    // it, and the place says which file it is in.
    std::optional<CPlusPlus::CxxFrontendDocument::Place> classNamedIn(
        const Utils::FilePath &filePath, const QString &className) const;

    // What the class whose name stands at \a line and \a column of
    // \a filePath derives from, written out in full: the ones it names
    // itself, without what those derive from in turn.
    std::optional<QStringList> basesOfTheClassIn(
        const Utils::FilePath &filePath, int line, int column) const;

    // What the locator needs of the function whose own name stands at
    // \a line and \a column of \a filePath.
    std::optional<DeclarationToDefine> declarationToDefineIn(
        const Utils::FilePath &filePath, int line, int column) const;

    // Where the project defines the function declared at \a line and
    // \a column of \a filePath -- the same search cxxFrontendCounterpart
    // makes, under the same bound on how many files it reads.
    std::optional<Utils::Link> definitionOfFunctionIn(
        const Utils::FilePath &filePath, int line, int column) const;

private:
    class Private;
    const std::unique_ptr<Private> d;
};

// A literal at \a line and \a column of \a filePath, both counted from one,
// written inside a function.
//
// Nothing where the model has no such file, and then the caller answers the
// way it did before. An invalid answer is an answer: there is no literal
// there to make a parameter of.
std::optional<CPlusPlus::CxxFrontendDocument::LiteralInAFunction>
cxxFrontendLiteralInAFunctionAt(const Utils::FilePath &filePath, int line, int column);

// A call or a new expression at \a line and \a column of \a filePath, both
// counted from one, whose value is thrown away.
//
// Nothing where the model has no such file, and then the caller answers the
// way it did before. An invalid answer is an answer: there is nothing there
// to assign to a variable.
std::optional<CPlusPlus::CxxFrontendDocument::DiscardedValue> cxxFrontendDiscardedValueAt(
    const Utils::FilePath &filePath, int line, int column);

// The switch statement written around \a line and \a column of \a filePath,
// both counted from one: where to write new cases, and which values of the
// enumeration its condition has it does not handle yet.
//
// Nothing where the model has no such file, and then the caller answers the
// way it did before. An invalid answer is an answer: the position is in no
// switch, or in one there is nothing to complete.
// The call a meta object could make instead of the one at a position:
// what is called on what, with what, and how much of the line says so.
// Nothing where the model has no such file, or where the position is on
// no call Qt can make by name.
// What a name means where \a filePath can see it: whether anything is
// declared under it at all, and what. Nothing where the model has no such
// file. The question a fix asks before writing an include for something.
std::optional<CPlusPlus::CxxFrontendDocument::Declaration> cxxFrontendLookup(
    const Utils::FilePath &filePath, const QString &name);

// The Q_PROPERTY a position is written on -- the macro's own name and the
// parentheses around what it says, a position inside them being a
// question about the type, the name or an item. Nothing where this model
// has not read the file or the position is on no property.
std::optional<CPlusPlus::CxxFrontendDocument::QtProperty> cxxFrontendQtPropertyAt(
    const Utils::FilePath &filePath, int line, int column);

// What is made of a type before it is written down. A getter hands back
// what a member holds, a setter takes it by const reference, a Q_PROPERTY
// says the value behind either, and a getter of a container hands back
// what it holds -- so what is written is hardly ever the type as
// declared. Applied in the order given.
enum class CxxFrontendTypeStep {
    WithoutConst,
    Value,
    ConstReference,
    ConstOnReference,
    FirstTemplateArgument,
};

// The type of the thing declared at a place, and where the answer is
// going.
struct CxxFrontendTypeRequest
{
    Utils::FilePath filePath; // where the thing is declared
    int line = 0;
    int column = 0;
    QList<CxxFrontendTypeStep> steps;

    // The file the answer is being written into, which is not always the
    // one that declares the thing: a member is declared in a header and
    // defined in the source file that includes it. Both are then read as
    // one, since that is how a compiler reads them. Empty for "as the file
    // that declares it writes it".
    Utils::FilePath writtenIn;
    int writtenAtLine = 0;
    int writtenAtColumn = 0;
};

// What that type is, as far as anything deciding how to hand it over
// cares. Nothing where the model cannot read the file or nothing is
// declared at the place.
struct CxxFrontendTypeFacts
{
    bool isPointer = false;
    bool isReference = false;
    bool isEnumeration = false;
    bool isNumber = false;
    bool isConst = false;
    // The name it was declared under, without its path or its arguments:
    // what a caller with a rule per type has the rule for.
    QString declaredName;
};
std::optional<CxxFrontendTypeFacts> cxxFrontendTypeFacts(
    const CPlusPlus::Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
    const CxxFrontendTypeRequest &request);

// That same type written as a declaration of \a name, or alone where that
// is empty -- a declarator is written around a name, so no amount of
// putting the name after the type gets there.
std::optional<QString> cxxFrontendTypeWritten(
    const CPlusPlus::Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
    const CxxFrontendTypeRequest &request, const QString &name);

// And written without the arguments of a template, which is how a caller
// with a rule per type names the type it has a rule for.
std::optional<QString> cxxFrontendTypeWithoutTemplateParameters(
    const CPlusPlus::Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
    const CxxFrontendTypeRequest &request);

std::optional<CPlusPlus::CxxFrontendDocument::MetaMethodCall> cxxFrontendMetaMethodCallAt(
    const Utils::FilePath &filePath, int line, int column);

std::optional<CPlusPlus::CxxFrontendDocument::Switch> cxxFrontendSwitchAt(
    const Utils::FilePath &filePath, int line, int column);

// The function a position is written inside of, and the lines it was written
// between. Nothing where the model has no such file -- and it is not read
// here: whoever asks is looking at a tooltip or a stack frame, and reading a
// file would be a parse on the thread that has to answer.
//
// An empty name is an answer: the position is inside no function.
struct CxxFrontendEnclosingFunction
{
    QString qualifiedName;
    int fromLine = 0; // counted from one
    int toLine = 0;
};
std::optional<CxxFrontendEnclosingFunction> cxxFrontendFunctionAround(
    const Utils::FilePath &filePath, int line, int column);

// The class a position is written in, written out in full. Nothing where the
// model has no such file, and not read here either; an empty string is an
// answer -- what is written around the position is no class.
std::optional<QString> cxxFrontendClassAround(const Utils::FilePath &filePath,
                                              int line, int column);

// A comment a file writes: where it stands, and which of the four ways it
// is written -- which is what tells one run of comments from the next.
struct CxxFrontendComment
{
    CPlusPlus::CommentRange range;
    CPlusPlus::CommentStyle style = CPlusPlus::CommentStyle::CStyle;
};

// The comments of \a filePath that the run from \a start to \a end covers,
// in the order they are written.
//
// Nothing where the model has no such file, and then the caller answers the
// way it did before. An empty list is an answer: the run is on no comment,
// or something other than a comment is in it -- and something other than a
// comment is read off the text, since this model hands out no token stream:
// whatever is neither one of these comments nor space is something else.
std::optional<QList<CxxFrontendComment>> cxxFrontendCommentsIn(
    const Utils::FilePath &filePath, const QTextDocument &textDoc, int start, int end);

// A local variable of a function: its name, whether it is one of the
// function's parameters, the class its type names where it names one, and
// every place the file writes it -- the declaration first, the uses after.
struct CxxFrontendLocal
{
    QString name;
    bool isParameter = false;
    QString className;
    CursorInfo::Ranges places;
};

// The locals of the function written around \a line and \a column, both
// counted from one, as a CursorInfo::Range counts them.
//
// Nothing where the model was never run over this file, and the caller
// answers the way it did before. An answer with no locals in it is an answer:
// the position is outside any function, or the function has none.
//
// A local is the one question a single file settles completely -- a parameter
// or a block variable cannot be named anywhere else -- so what comes back
// here is the whole of what the code says. What it leaves out is what is
// written about the code: a parameter named in the function's documentation
// is highlighted with it, and comments are not in this model's token stream
// at all.
std::optional<QList<CxxFrontendLocal>> cxxFrontendLocalsAt(const Utils::FilePath &filePath,
                                                           int line, int column);

// One entry of what an outline draws: what to write, which icon to write it
// with, where it takes the reader, and where it sits in the tree.
struct CxxFrontendOutlineEntry
{
    QString name;
    QString signature; // a function's parameter list, empty otherwise
    QString valueType; // what follows the colon, empty for a scope
    int line = 0;      // one-based
    int column = 0;    // one-based
    // The entry this one is inside, as an index into the list, or -1 at file
    // scope. An entry always follows the one it is inside.
    int parent = -1;
    Utils::CodeModelIcon::Type icon = Utils::CodeModelIcon::Unknown;
    bool isGenerated = false;
    bool isForwardDeclaration = false;
};

// What \a filePath declares, in the order it declares it, or nothing where
// the model has no such file to read.
//
// A file's own structure is what a single document settles, so this is the
// one question the model answers whole. What it cannot read at all is
// Objective-C, so a file written in it is declined rather than answered with
// the little that parsed.
std::optional<QList<CxxFrontendOutlineEntry>> cxxFrontendOutline(const Utils::FilePath &filePath);

// What \a filePath declares, as the entries a symbol index is made of --
// the question SearchSymbols asks of a built-in document, answered off this
// model. Nothing where the model has no such file, and nothing for
// Objective-C, which this front end does not read.
//
// One entry per thing declared, at the place it is declared, where the
// built-in reading has one per place a name is written: a function declared
// in a class and defined below is one entry here and two there. Whoever
// reads a list of the second kind drops the declaration of a function it
// has exactly one definition of, so the two lists hold the same things --
// they point at different ends of the same function, and this one points at
// the declaration.
std::optional<QList<IndexItem::Ptr>> cxxFrontendIndexItems(const Utils::FilePath &filePath);

// The entries the project-wide index keeps for a file, as the tree it keeps
// them in: one root per file with what it declares hung under it, nested so
// that a walk can stop at an enum without seeing its enumerators.
//
// The file is read here. Not out of the store, an index being about every
// file a project has rather than the few being edited; and not from the
// text the indexer hands over, which is the file already preprocessed.
// That is what this costs: a parse of each file on top of the indexer's.
//
// Nothing where the model is off or cannot read the file, and then the
// built-in walk makes the entries.
std::optional<IndexItem::Ptr> cxxFrontendIndexTreeFor(const CPlusPlus::Snapshot &builtinSnapshot,
                                                      const Utils::FilePath &filePath);

// What the editor colours in \a filePath: every name it writes, with the
// kind that decides the colour, in the order they are written. Nothing
// where the model has no such file.
//
// The macros are not here. The preprocessor reports those and the caller
// merges them in, which is what it does for the built-in model too, so
// this answers for the names and leaves that where it is.
std::optional<QList<TextEditor::HighlightingResult>> cxxFrontendHighlighting(
    const Utils::FilePath &filePath);

// What could be written at \a line and \a column of \a filePath -- both
// counted from one -- with \a source as the text stands in the editor,
// half-written expression and all.
//
// Not read off the last parse, as every other question here is: where the
// question is asked has to be settled before the file is preprocessed, so
// this reads \a source again, headers and all. That is affordable because
// completion already runs on a worker thread, and it is why the answer is
// not kept -- it belongs to one keystroke.
//
// \a builtinSnapshot is the snapshot the completion is running against, and
// includes are resolved through it exactly as updateCxxFrontendModel does,
// so both models read the same headers.
//
// Nothing unless the model was asked for, or where the file cannot be read;
// then the caller answers the way it did before.
std::optional<CPlusPlus::CxxFrontendDocument::Completion> cxxFrontendCompletion(
    const CPlusPlus::Snapshot &builtinSnapshot,
    const Utils::FilePath &filePath,
    const QString &source,
    int line,
    int column);

} // namespace CppEditor::Internal
