// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "typehierarchybuilder.h"

#include <coreplugin/helpitem.h>
#include <cplusplus/CppDocument.h>
#include <texteditor/texteditor.h>

#include <QFuture>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QTextCursor>

#include <functional>

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
    static QFuture<QSharedPointer<CppElement>> asyncExecute(TextEditor::TextEditorWidget *editor);
    static QFuture<QSharedPointer<CppElement>> asyncExecute(const QString &expression,
                                                            const QString &fileName);
    bool identifiedCppElement() const;
    const QSharedPointer<CppElement> &cppElement() const;
    bool hasDiagnosis() const;
    const QString &diagnosis() const;

    static Utils::Link linkFromExpression(const QString &expression, const QString &fileName);

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

class CppDeclarableElement : public CppElement
{
public:
    explicit CppDeclarableElement(CPlusPlus::Symbol *declaration);

public:
    CPlusPlus::Symbol *declaration;
    QString name;
    QString qualifiedName;
    QString type;
    QIcon icon;
};

class CppClass : public CppDeclarableElement
{
public:
    CppClass();
    explicit CppClass(CPlusPlus::Symbol *declaration);

    bool operator==(const CppClass &other);

    CppClass *toCppClass() final;

    void lookupBases(QFutureInterfaceBase &futureInterface,
                     CPlusPlus::Symbol *declaration, const CPlusPlus::LookupContext &context);
    void lookupDerived(QFutureInterfaceBase &futureInterface,
                       CPlusPlus::Symbol *declaration, const CPlusPlus::Snapshot &snapshot);

    QList<CppClass> bases;
    QList<CppClass> derived;

private:
    void addBaseHierarchy(QFutureInterfaceBase &futureInterface,
                          const CPlusPlus::LookupContext &context,
                          CPlusPlus::ClassOrNamespace *hierarchy,
                          QSet<CPlusPlus::ClassOrNamespace *> *visited);
    void addDerivedHierarchy(const TypeHierarchy &hierarchy);
};

} // namespace Internal
} // namespace CppEditor
