// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "typehierarchybuilder.h"

#include "cppworkingcopy.h"

#include <coreplugin/helpitem.h>
#include <cplusplus/CppDocument.h>
#include <texteditor/texteditor.h>
#include <utils/utilsicons.h>

#include <QFuture>
#include <QString>
#include <QStringList>
#include <QTextCursor>


namespace CPlusPlus {
class ClassOrNamespace;
class LookupItem;
class LookupContext;
}

namespace CppEditor {
class CppModelManager;

namespace Internal {
class CppElement;

class CppElementEvaluator final
{
public:
    explicit CppElementEvaluator(TextEditor::TextEditorWidget *editor);
    ~CppElementEvaluator();

    void setTextCursor(const QTextCursor &tc);

    void execute();
    static QFuture<std::shared_ptr<CppElement>> asyncExecute(TextEditor::TextEditorWidget *editor);
    static QFuture<std::shared_ptr<CppElement>> asyncExecute(const QString &expression,
                                                            const Utils::FilePath &filePath);
    const std::shared_ptr<CppElement> &cppElement() const;
    bool hasDiagnosis() const;
    const QString &diagnosis() const;

    static Utils::Link linkFromExpression(const QString &expression, const Utils::FilePath &filePath);

private:
    class CppElementEvaluatorPrivate *d;
};

class CppClass;

class CppElement
{
protected:
    CppElement();

public:
    virtual ~CppElement();

    virtual CppClass *toCppClass();

    Core::HelpItem::Category helpCategory = Core::HelpItem::Unknown;
    QStringList helpIdCandidates;
    QString helpMark;
    Utils::Link link;
    QString tooltip;
};

// What a front end says about the thing under the cursor: what kind of thing
// it is, what it is called, how it reads and where it stands. The element a
// reader is shown -- its tooltip, the help it offers, the icon beside it --
// is built from these and from nothing else, so whichever front end read the
// position fills the same facts in.
struct CppElementFacts
{
    enum class Kind {
        Unknown,
        Namespace,
        Class,
        Enum,
        Enumerator,
        Typedef,
        Function,
        Variable
    };
    Kind kind = Kind::Unknown;

    QString name;          // as it is written, with nothing in front of it
    QString qualifiedName; // the scopes it is written in included

    // Its type with the qualified name written into it, which is how a
    // tooltip shows a declaration, and the same for an alias -- written
    // without the names of a function's parameters, since an alias is a
    // type and a type has none.
    QString type;
    QString aliasedType;

    // A function without what it hands back and without what it calls its
    // parameters: what tells one overload from another, which is what
    // documentation is marked with.
    QString signature;

    Utils::CodeModelIcon::Type iconType = Utils::CodeModelIcon::Unknown;
    Utils::Link link;

    // An enumerator stands for a value in an enum and is shown as both: the
    // enum's name in front of its own, and the value where one was written.
    QString enumName;
    QString enumUnqualifiedName;
    QString enumeratorValue;

    // The class a variable's type names, written out in full. What
    // documentation a variable has is its type's -- nobody documents a
    // variable -- so the help goes under this where there is one.
    QString typeClassName;
};

class CppDeclarableElement : public CppElement
{
public:
    explicit CppDeclarableElement(CPlusPlus::Symbol *declaration);
    explicit CppDeclarableElement(const CppElementFacts &facts);

public:
    Utils::CodeModelIcon::Type iconType;
    QString name;
    QString qualifiedName;
    QString type;
};

class CppClass : public CppDeclarableElement
{
public:
    CppClass();
    explicit CppClass(CPlusPlus::Symbol *declaration);
    explicit CppClass(const CppElementFacts &facts);

    CppClass *toCppClass() final;

    // \a workingCopy is what a front end reading the files again is handed:
    // it is built off the editor's own documents, so whoever runs this on a
    // worker has to have taken it where those live.
    void lookupBases(const QFuture<void> &future, CPlusPlus::Symbol *declaration,
                     const CPlusPlus::LookupContext &context,
                     const WorkingCopy &workingCopy);
    void lookupDerived(const QFuture<void> &future, CPlusPlus::Symbol *declaration,
                       const CPlusPlus::Snapshot &snapshot);

    QList<CppClass> bases;
    QList<CppClass> derived;

private:
    void addBaseHierarchy(const QFuture<void> &future,
                          const CPlusPlus::LookupContext &context,
                          CPlusPlus::ClassOrNamespace *hierarchy,
                          QSet<CPlusPlus::ClassOrNamespace *> *visited);
    void addDerivedHierarchy(const TypeHierarchy &hierarchy);
};

} // namespace Internal
} // namespace CppEditor
