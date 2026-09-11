// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cpprefactoringchanges.h"

#include <QtTaskTree/QSingleTaskTreeRunner>

#include <utils/changeset.h>

#include <QString>
#include <QTextCursor>

#include <functional>
#include <memory>

namespace CppEditor {
class CppEditorWidget;

namespace Internal {
class FunctionDeclDefLink;

// What a function's signature says, as one front end read it.
//
// The types are canonical spellings and are only ever compared with each
// other: two of them are the same type exactly when the front end that
// printed them printed them alike. Which is the comparison the built-in front
// end's FullySpecifiedType::match() makes as well -- it tells named types
// apart by the name as written, not by what the name resolves to.
class FunctionSignature
{
public:
    class Parameter
    {
    public:
        QString name; // Empty where the parameter is unnamed.
        QString type;
    };

    // The name the declaration is written under, the qualifier included: the
    // C::f of a definition written outside its class.
    QString name;
    QString returnType;
    QList<Parameter> parameters;
    bool isConst = false;
    bool isVolatile = false;

    // As it is written, "noexcept" or "throw()", empty where there is none.
    QString exceptionSpecification;
};

// Where each part of a function declaration is written, in the positions a
// QTextCursor counts. This is every place the link changes: it never writes
// anywhere that is not one of these.
class WrittenDeclaration
{
public:
    class Parameter
    {
    public:
        // The parameter declaration itself.
        Utils::ChangeSet::Range range;

        // The whole of what stands between the punctuation around it: from
        // after the '(' or the comma before it, up to the ')' or the comma
        // after it. That is what a replacement for this parameter takes the
        // place of, and where the spacing somebody wrote is kept from.
        Utils::ChangeSet::Range slot;

        // Where its type stops, which is where a replacement for the type
        // has to stop too.
        int typeEnd = 0;

        // The name, or -1 where the parameter is unnamed.
        int nameStart = -1;
        int nameEnd = -1;

        // The '=' of a default argument, -1 where there is none.
        int defaultValueStart = -1;

        // A name written in a comment, void f(int /*count*/), is not a name
        // to rename -- and no front end sees one, so it is looked for in the
        // text.
        bool nameIsInAComment = false;

        // Whether what follows the name lets the name simply be dropped: a
        // comma, an '=' or the closing parenthesis leaves something that
        // still reads, anything else does not.
        bool nameMayBeDropped = false;
        bool anEqualFollowsTheName = false;

        // Every place the parameter is written after the parentheses, which
        // a rename has to follow. Only filled in where this declaration is a
        // definition, since only then is there a body to write it in.
        QList<Utils::ChangeSet::Range> uses;
    };

    // The whole declaration, which is what an offset the changes are moved
    // to counts from.
    int start = 0;

    // Where what it says stops: after the trailing return type, the
    // exception specification, the cv qualifiers or the closing parenthesis,
    // whichever is last. The body is not part of it, and neither is the
    // semicolon -- this is the run of text the link watches over.
    int end = 0;

    // The name it is declared under, the qualifier included: the whole of
    // the C::f of a definition written outside its class. The link aborts
    // when that changes under it, since it would then be following a
    // different function.
    int nameStart = 0;
    int nameEnd = 0;

    // Where a new return type is written, and whether one may be written at
    // all -- something other than a plain declaration or a definition has no
    // place to put it.
    int returnTypeStart = 0;
    bool returnTypeMayBeWritten = false;

    int lparenStart = 0;
    int lparenEnd = 0;
    int rparenStart = 0;
    int rparenEnd = 0;

    bool isDefinition = false;

    QList<Parameter> parameters;

    // Where a cv qualifier is written: the qualifier itself, and the place
    // taking it away starts from, so that the space in front of it goes too.
    class Qualifier
    {
    public:
        int start = -1;
        int end = -1;
        int removeFrom = -1;

        bool isWritten() const { return start != -1; }
    };
    Qualifier constQualifier;
    Qualifier volatileQualifier;

    // The exception specification, -1 where there is none, and where one is
    // written if there is none: after the ref qualifier, the cv qualifiers or
    // the closing parenthesis, whichever is last.
    int exceptionSpecificationStart = -1;
    int exceptionSpecificationEnd = -1;
    int exceptionSpecificationInsertAt = 0;
};

// The declaration as it now stands in the editor, read by the front end that
// read the two files: what it says, and how each of its types has to be
// written where the other side of the function stands.
//
// That last part is why this is an object rather than plain data. A type a
// header names as T may have to be written N::T in the file that defines the
// function, and the shortest spelling that still means the same thing there
// is a question only a front end that resolved both files can answer.
class EditedDeclaration
{
public:
    virtual ~EditedDeclaration() = default;

    // Nothing where the text does not read as one function declaration,
    // which is what it is for as long as somebody is in the middle of typing.
    virtual bool isValid() const = 0;

    virtual FunctionSignature signature() const = 0;

    // The return type written under the name the other side is written
    // under. The two go together, because a return type is written in front
    // of the name and is replaced along with it.
    virtual QString returnTypeDeclaration() const = 0;

    // Parameter \a index declaring \a name -- empty for one that is to stay
    // unnamed -- written as it has to be written at the other side.
    virtual QString parameterDeclaration(int index, const QString &name) const = 0;

    // The same type in the canonical spelling FunctionSignature uses, so
    // that it can be told from what the other side says.
    virtual QString rewrittenParameterType(int index) const = 0;
};

class FunctionDeclDefLinkFinder : public QObject
{
    Q_OBJECT
public:
    FunctionDeclDefLinkFinder(QObject *parent = nullptr);

    void startFindLinkAt(QTextCursor cursor,
                    const CPlusPlus::Document::Ptr &doc,
                    const CPlusPlus::Snapshot &snapshot);

    QTextCursor scannedSelection() const;

signals:
    void foundLink(std::shared_ptr<FunctionDeclDefLink> link);

private:
    QTextCursor m_scannedSelection;
    QTextCursor m_nameSelection;
    QtTaskTree::QSingleTaskTreeRunner m_taskTreeRunner;
};

class FunctionDeclDefLink
{
    Q_DISABLE_COPY(FunctionDeclDefLink)
    FunctionDeclDefLink() = default;
public:
    bool isValid() const;
    bool isMarkerVisible() const;

    void apply(CppEditorWidget *editor, bool jumpToMatch);
    void hideMarker(CppEditorWidget *editor);
    void showMarker(CppEditorWidget *editor);
    Utils::ChangeSet changes(const CPlusPlus::Snapshot &snapshot, int targetOffset = -1);

    QTextCursor linkSelection;

    // stored to allow aborting when the name is changed
    QTextCursor nameSelection;
    QString nameInitial;

    // The 'source' prefix denotes information about the original state
    // of the function before the user did any edits.
    CPlusPlus::Document::Ptr sourceDocument;

    // The 'target' prefix denotes information about the remote declaration matching
    // the 'source' declaration, where we will try to apply the user changes.
    // 1-based line and column
    int targetLine = 0;
    int targetColumn = 0;
    QString targetInitial;

    CppRefactoringFileConstPtr targetFile;

    // What the two sides say, and where the parts of the other side are
    // written: everything changes() works from. Which front end filled them
    // in it does not know.
    FunctionSignature sourceSignature;
    FunctionSignature targetSignature;
    WrittenDeclaration targetWritten;

    // Where the other side's own name stands, one-based as everything here
    // counts: where somebody jumping to it lands, and what its documentation
    // is looked for above.
    int targetNameLine = 0;
    int targetNameColumn = 0;

    // Its name without the scopes in front of it, which is the name a
    // comment above it documents things under.
    QString targetShortName;

    // Reads the declaration as it now stands in the editor. Held as a
    // function because which front end reads it is settled when the link is
    // found, and there is nothing to read until somebody has typed.
    //
    // Both cursors, because the two front ends need different things of the
    // same edit: the text the declaration now says, and where its name now
    // stands -- a model that resolves names has to read the file again and
    // find the declaration in it, and the name is what it is found by. Both
    // followed what was typed, which is why they are cursors.
    std::function<std::shared_ptr<EditedDeclaration>(const QTextCursor &linkSelection,
                                                     const QTextCursor &nameSelection,
                                                     const CPlusPlus::Snapshot &snapshot)>
        readEditedDeclaration;

private:
    QString normalizedInitialName() const;

    bool hasMarker = false;

    friend class FunctionDeclDefLinkFinder;
};

} // namespace Internal
} // namespace CppEditor
