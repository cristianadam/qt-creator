// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppelementevaluator.h"

#include "cppmodelmanager.h"

#ifdef QTC_WITH_CXX_FRONTEND
#include "cxxfrontendmodel.h"

#include <cplusplus/CxxFrontendDocument.h>
#endif
#include "cpptoolsreuse.h"
#include "symbolfinder.h"
#include "typehierarchybuilder.h"

#include <texteditor/textdocument.h>

#include <cplusplus/ExpressionUnderCursor.h>
#include <cplusplus/Icons.h>
#include <cplusplus/TypeOfExpression.h>

#include <utils/async.h>

#include <QDir>
#include <QQueue>
#include <QSet>

using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor::Internal {

struct SourceData
{
    Document::Ptr doc;
    Scope *scope;
    QString expression;

    // What the cursor is on, where a front end has already settled it: the
    // expression is then nothing to cut out of the text and resolve again.
    Symbol *resolved = nullptr;
};
using SourceFunction = std::function<std::optional<SourceData>(const CPlusPlus::Snapshot &)>;

struct ExecData
{
    CPlusPlus::Snapshot snapshot;
    CPlusPlus::LookupItem lookupItem;
    CPlusPlus::LookupContext context;

    // Taken where the editor's documents live, because building it reads
    // what is being typed: the hierarchy below is worked out on a worker.
    WorkingCopy workingCopy;
};
using ExecFunction = std::function<QFuture<std::shared_ptr<CppElement>>(const ExecData &)>;

static QStringList stripName(const QString &name)
{
    QStringList all;
    all << name;
    int colonColon = 0;
    const int size = name.size();
    while ((colonColon = name.indexOf(QLatin1String("::"), colonColon)) != -1) {
        all << name.right(size - colonColon - 2);
        colonColon += 2;
    }
    return all;
}

CppElement::CppElement() = default;

CppElement::~CppElement() = default;

CppClass *CppElement::toCppClass()
{
    return nullptr;
}

class CppInclude : public CppElement
{
public:
    explicit CppInclude(const Document::Include &includeFile)
        : path(includeFile.resolvedFileName())
        , fileName(path.fileName())
    {
        helpCategory = Core::HelpItem::Brief;
        helpIdCandidates = QStringList(fileName);
        helpMark = fileName;
        link = Utils::Link(path);
        tooltip = path.toUserOutput();
    }

public:
    FilePath path;
    QString fileName;
};

class CppMacro : public CppElement
{
public:
    explicit CppMacro(const Macro &macro)
    {
        helpCategory = Core::HelpItem::Macro;
        const QString macroName = QString::fromUtf8(macro.name(), macro.name().size());
        helpIdCandidates = QStringList(macroName);
        helpMark = macroName;
        link = Link(macro.filePath(), macro.line());
        tooltip = macro.toStringWithLineBreaks();
    }
};

// What the built-in front end says about a declaration. The scope and the
// context are what a variable's type is looked up in, and are nothing where
// the caller only wants the declaration itself said.
static CppElementFacts builtinFactsOf(Symbol *declaration, const LookupContext &context,
                                      Scope *scope);

// CppDeclarableElement
CppDeclarableElement::CppDeclarableElement(Symbol *declaration)
    : CppDeclarableElement(builtinFactsOf(declaration, LookupContext(), nullptr))
{}

CppDeclarableElement::CppDeclarableElement(const CppElementFacts &facts)
    : CppElement()
    , iconType(facts.iconType)
{
    name = facts.name;
    qualifiedName = facts.qualifiedName;
    // A name inside a named scope is looked up under every tail of its
    // path; one written at file scope or inside a function is its own
    // whole path, which stripName answers with just as well.
    helpIdCandidates = stripName(facts.qualifiedName);
    tooltip = facts.type;
    link = facts.link;
    helpMark = facts.name;
}

class CppNamespace : public CppDeclarableElement
{
public:
    explicit CppNamespace(const CppElementFacts &facts)
        : CppDeclarableElement(facts)
    {
        helpCategory = Core::HelpItem::ClassOrNamespace;
        tooltip = qualifiedName;
    }
};

CppClass::CppClass(Symbol *declaration) : CppDeclarableElement(declaration)
{
    helpCategory = Core::HelpItem::ClassOrNamespace;
    tooltip = qualifiedName;
}

CppClass::CppClass(const CppElementFacts &facts) : CppDeclarableElement(facts)
{
    helpCategory = Core::HelpItem::ClassOrNamespace;
    tooltip = qualifiedName;
}

CppClass *CppClass::toCppClass()
{
    return this;
}

#ifdef QTC_WITH_CXX_FRONTEND

// What the class inherits, as the cxx-frontend model reads it, and nothing
// where it has not read the file or a base cannot be said as a class of the
// file's own parse -- half a hierarchy is worse than the other model's whole
// one.
static bool lookupBasesOnTheModel(CppClass *cppClass, Symbol *declaration,
                                  const Snapshot &snapshot, const WorkingCopy &workingCopy)
{
    if (!declaration || declaration->filePath().isEmpty())
        return false;
    const std::optional<QList<CPlusPlus::CxxFrontendDocument::BaseClass>> bases
        = cxxFrontendBasesOfTheClassAt(workingCopy,
                                       declaration->filePath(), declaration->line(),
                                       declaration->column());
    if (!bases)
        return false;

    // The list says which entry each one is a base of, and a parent always
    // comes before its children, so the tree is built as it is read. Each
    // entry is remembered as the path to it rather than as a pointer: the
    // lists grow as this goes, and what a pointer into one meant a moment
    // ago is not where it is now.
    QList<QList<int>> paths;
    for (const CPlusPlus::CxxFrontendDocument::BaseClass &base : *bases) {
        Class * const symbol = builtinClassWrittenAt(snapshot, base.place);
        if (!symbol)
            return false;

        QList<int> path = base.parent == -1 ? QList<int>() : paths.at(base.parent);
        CppClass *parent = cppClass;
        for (const int step : path)
            parent = &parent->bases[step];
        parent->bases.append(CppClass(symbol));
        path.append(int(parent->bases.size()) - 1);
        paths.append(path);
    }
    return true;
}

#endif // QTC_WITH_CXX_FRONTEND

void CppClass::lookupBases(const QFuture<void> &future, Symbol *declaration,
                           const LookupContext &context, const WorkingCopy &workingCopy)
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (lookupBasesOnTheModel(this, declaration, context.snapshot(), workingCopy))
        return;
#else
    Q_UNUSED(workingCopy)
#endif

    ClassOrNamespace *hierarchy = context.lookupType(declaration);
    if (!hierarchy)
        return;
    QSet<ClassOrNamespace *> visited;
    addBaseHierarchy(future, context, hierarchy, &visited);
}

void CppClass::addBaseHierarchy(const QFuture<void> &future, const LookupContext &context,
                                ClassOrNamespace *hierarchy, QSet<ClassOrNamespace *> *visited)
{
    if (future.isCanceled())
        return;
    visited->insert(hierarchy);
    const QList<ClassOrNamespace *> &baseClasses = hierarchy->usings();
    for (ClassOrNamespace *baseClass : baseClasses) {
        const QList<Symbol *> &symbols = baseClass->symbols();
        for (Symbol *symbol : symbols) {
            if (!symbol->asClass())
                continue;
            ClassOrNamespace *baseHierarchy = context.lookupType(symbol);
            if (baseHierarchy && !visited->contains(baseHierarchy)) {
                CppClass classSymbol(symbol);
                classSymbol.addBaseHierarchy(future, context, baseHierarchy, visited);
                bases.append(classSymbol);
            }
        }
    }
}

void CppClass::lookupDerived(const QFuture<void> &future, Symbol *declaration,
                             const Snapshot &snapshot)
{
    snapshot.updateDependencyTable(future);
    if (future.isCanceled())
        return;
    addDerivedHierarchy(TypeHierarchyBuilder::buildDerivedTypeHierarchy(
                        declaration, snapshot, future));
}

void CppClass::addDerivedHierarchy(const TypeHierarchy &hierarchy)
{
    const QList<TypeHierarchy> derivedHierarchies = hierarchy.hierarchy();
    for (const TypeHierarchy &derivedHierarchy : derivedHierarchies) {
        CppClass classSymbol(derivedHierarchy.symbol());
        classSymbol.addDerivedHierarchy(derivedHierarchy);
        derived.append(classSymbol);
    }
}

class CppFunction : public CppDeclarableElement
{
public:
    explicit CppFunction(const CppElementFacts &facts)
        : CppDeclarableElement(facts)
    {
        helpCategory = Core::HelpItem::Function;

        // Functions marks can be found either by the main overload or signature based
        // (with no argument names and no return). Help ids have no signature at all.
        helpMark = facts.signature;
        helpIdCandidates.append(facts.name);
    }
};

class CppEnum : public CppDeclarableElement
{
public:
    explicit CppEnum(const CppElementFacts &facts)
        : CppDeclarableElement(facts)
    {
        helpCategory = Core::HelpItem::Enum;
        tooltip = qualifiedName;
    }
};

class CppTypedef : public CppDeclarableElement
{
public:
    explicit CppTypedef(const CppElementFacts &facts)
        : CppDeclarableElement(facts)
    {
        helpCategory = Core::HelpItem::Typedef;
        tooltip = facts.aliasedType;
    }
};

class CppVariable : public CppDeclarableElement
{
public:
    explicit CppVariable(const CppElementFacts &facts)
        : CppDeclarableElement(facts)
    {
        // What documentation a variable has is its type's, so where the
        // type names a class the reader is sent there instead.
        if (facts.typeClassName.isEmpty())
            return;
        tooltip = facts.typeClassName;
        helpCategory = Core::HelpItem::ClassOrNamespace;
        const QStringList &allNames = stripName(facts.typeClassName);
        if (!allNames.isEmpty()) {
            helpMark = allNames.last();
            helpIdCandidates = allNames;
        }
    }
};

class CppEnumerator : public CppDeclarableElement
{
public:
    explicit CppEnumerator(const CppElementFacts &facts)
        : CppDeclarableElement(facts)
    {
        helpCategory = Core::HelpItem::Enum;
        helpMark = facts.enumUnqualifiedName;

        tooltip = facts.name;
        if (!facts.enumName.isEmpty())
            tooltip.prepend(facts.enumName + QLatin1Char(' '));
        if (!facts.enumeratorValue.isEmpty())
            tooltip.append(QLatin1String(" = ") + facts.enumeratorValue);
    }
};

// The element a reader is shown, out of what a front end said is there.
static std::shared_ptr<CppElement> elementOf(const CppElementFacts &facts)
{
    switch (facts.kind) {
    case CppElementFacts::Kind::Namespace:
        return std::make_shared<CppNamespace>(facts);
    case CppElementFacts::Kind::Class:
        return std::make_shared<CppClass>(facts);
    case CppElementFacts::Kind::Enum:
        return std::make_shared<CppEnum>(facts);
    case CppElementFacts::Kind::Enumerator:
        return std::make_shared<CppEnumerator>(facts);
    case CppElementFacts::Kind::Typedef:
        return std::make_shared<CppTypedef>(facts);
    case CppElementFacts::Kind::Function:
        return std::make_shared<CppFunction>(facts);
    case CppElementFacts::Kind::Variable:
        return std::make_shared<CppVariable>(facts);
    case CppElementFacts::Kind::Unknown:
        break;
    }
    return std::make_shared<CppDeclarableElement>(facts);
}

static bool isCppClass(Symbol *symbol)
{
    return symbol->asClass() || symbol->asForwardClassDeclaration()
            || (symbol->asTemplate() && symbol->asTemplate()->declaration()
                && (symbol->asTemplate()->declaration()->asClass()
                    || symbol->asTemplate()->declaration()->asForwardClassDeclaration()));
}

static Symbol *followClassDeclaration(Symbol *symbol, const Snapshot &snapshot, SymbolFinder symbolFinder,
                               LookupContext *context = nullptr)
{
    if (!symbol->asForwardClassDeclaration())
        return symbol;

    Symbol *classDeclaration = symbolFinder.findMatchingClassDeclaration(symbol, snapshot);
    if (!classDeclaration)
        return symbol;

    if (context) {
        const Document::Ptr declarationDocument = snapshot.document(classDeclaration->filePath());
        if (declarationDocument != context->thisDocument())
            (*context) = LookupContext(declarationDocument, snapshot);
    }
    return classDeclaration;
}

static Symbol *followTemplateAsClass(Symbol *symbol)
{
    if (Template *t = symbol->asTemplate(); t && t->declaration() && t->declaration()->asClass())
        return t->declaration()->asClass();
    return symbol;
}

static void createTypeHierarchy(QPromise<std::shared_ptr<CppElement>> &promise,
                                const ExecData &execData, SymbolFinder symbolFinder)
{
    if (promise.isCanceled())
        return;

    Symbol *declaration = execData.lookupItem.declaration();
    if (!declaration)
        return;

    if (!isCppClass(declaration))
        return;

    LookupContext contextToUse = execData.context;
    declaration = followClassDeclaration(declaration, execData.snapshot, symbolFinder, &contextToUse);
    declaration = followTemplateAsClass(declaration);

    if (promise.isCanceled())
        return;
    std::shared_ptr<CppClass> cppClass(new CppClass(declaration));
    const QFuture<void> future = QFuture<void>(promise.future());
    cppClass->lookupBases(future, declaration, contextToUse, execData.workingCopy);
    if (promise.isCanceled())
        return;
    cppClass->lookupDerived(future, declaration, execData.snapshot);
    if (promise.isCanceled())
        return;
    promise.addResult(cppClass);
}

// What kind of thing a declaration is, as far as a reader of it cares. The
// order is the one the built-in reading has always applied: a class before
// an enum, a function before a variable, and whatever is left over said as
// a declaration and nothing more.
static CppElementFacts::Kind kindOf(Symbol *declaration)
{
    const FullySpecifiedType &type = declaration->type();
    if (declaration->asNamespace())
        return CppElementFacts::Kind::Namespace;
    if (isCppClass(declaration))
        return CppElementFacts::Kind::Class;
    if (declaration->asEnum())
        return CppElementFacts::Kind::Enum;
    if (dynamic_cast<EnumeratorDeclaration *>(declaration))
        return CppElementFacts::Kind::Enumerator;
    if (declaration->isTypedef())
        return CppElementFacts::Kind::Typedef;
    if (declaration->asFunction() || (type.isValid() && type->asFunctionType())
        || declaration->asTemplate()) {
        return CppElementFacts::Kind::Function;
    }
    if (declaration->asDeclaration() && type.isValid())
        return CppElementFacts::Kind::Variable;
    return CppElementFacts::Kind::Unknown;
}

// The class a variable's type names, written out in full, or nothing where
// its type names none.
static QString classOfTheTypeOf(Symbol *declaration, const LookupContext &context, Scope *scope)
{
    const FullySpecifiedType &type = declaration->type();
    const Name *typeName = nullptr;
    if (type->asNamedType()) {
        typeName = type->asNamedType()->name();
    } else if (type->asPointerType() || type->asReferenceType()) {
        FullySpecifiedType associatedType;
        if (type->asPointerType())
            associatedType = type->asPointerType()->elementType();
        else
            associatedType = type->asReferenceType()->elementType();
        if (associatedType->asNamedType())
            typeName = associatedType->asNamedType()->name();
    }
    if (!typeName)
        return {};

    ClassOrNamespace * const clazz = context.lookupType(typeName, scope);
    if (!clazz || clazz->symbols().isEmpty())
        return {};
    return Overview().prettyName(LookupContext::fullyQualifiedName(clazz->symbols().at(0)));
}

static CppElementFacts builtinFactsOf(Symbol *declaration, const LookupContext &context,
                                      Scope *scope)
{
    Overview overview;
    overview.showArgumentNames = true;
    overview.showReturnTypes = true;
    overview.showTemplateParameters = true;

    CppElementFacts facts;
    facts.kind = kindOf(declaration);
    facts.iconType = CPlusPlus::Icons::iconTypeForSymbol(declaration);
    facts.name = overview.prettyName(declaration->name());
    // A name inside a named scope is written out in full; one inside a
    // function or at file scope is its own whole path.
    if (declaration->enclosingScope()->asClass() || declaration->enclosingScope()->asNamespace()
        || declaration->enclosingScope()->asEnum()
        || declaration->enclosingScope()->asTemplate()) {
        facts.qualifiedName = overview.prettyName(LookupContext::fullyQualifiedName(declaration));
    } else {
        facts.qualifiedName = facts.name;
    }
    facts.type = overview.prettyType(declaration->type(), facts.qualifiedName);
    facts.link = declaration->toLink();

    if (facts.kind == CppElementFacts::Kind::Typedef) {
        Overview asWritten;
        asWritten.showTemplateParameters = true;
        facts.aliasedType = asWritten.prettyType(declaration->type(), facts.qualifiedName);
    } else if (facts.kind == CppElementFacts::Kind::Function) {
        Overview asASignature;
        asASignature.showDefaultArguments = false;
        facts.signature = asASignature.prettyType(declaration->type(), facts.name);
    } else if (facts.kind == CppElementFacts::Kind::Variable && context.thisDocument()) {
        facts.typeClassName = classOfTheTypeOf(declaration, context, scope);
    } else if (facts.kind == CppElementFacts::Kind::Enumerator) {
        Symbol * const enumSymbol = declaration->enclosingScope();
        facts.enumName = overview.prettyName(LookupContext::fullyQualifiedName(enumSymbol));
        facts.enumUnqualifiedName = overview.prettyName(enumSymbol->name());
        const auto enumerator = dynamic_cast<EnumeratorDeclaration *>(declaration);
        if (const StringLiteral * const value = enumerator ? enumerator->constantValue() : nullptr)
            facts.enumeratorValue = QString::fromUtf8(value->chars(), value->size());
    }
    return facts;
}

#ifdef QTC_WITH_CXX_FRONTEND

// What the cxx-frontend model says is at a position, in the terms an element
// is built from, and nothing where it has not read the file or the position
// is on no name it resolved -- and then the built-in reading answers.
static std::optional<CppElementFacts> modelFactsAt(const FilePath &filePath, int line, int column)
{
    const std::optional<CPlusPlus::CxxFrontendDocument::Element> element
        = cxxFrontendElementAt(filePath, line, column);
    if (!element)
        return std::nullopt;

    using Kind = CPlusPlus::CxxFrontendDocument::Kind;
    CppElementFacts facts;
    switch (element->kind) {
    case Kind::Class: facts.kind = CppElementFacts::Kind::Class; break;
    case Kind::Enum: facts.kind = CppElementFacts::Kind::Enum; break;
    case Kind::Enumerator: facts.kind = CppElementFacts::Kind::Enumerator; break;
    case Kind::Namespace: facts.kind = CppElementFacts::Kind::Namespace; break;
    case Kind::Function: facts.kind = CppElementFacts::Kind::Function; break;
    case Kind::TypeAlias: facts.kind = CppElementFacts::Kind::Typedef; break;
    // What a class calls its own is a variable like any other to a reader
    // of one.
    case Kind::Variable:
    case Kind::Field: facts.kind = CppElementFacts::Kind::Variable; break;
    // A using declaration is no kind of thing itself: what a reader wants
    // to be told about is whatever it made reachable.
    case Kind::UsingDeclaration:
    case Kind::Unknown: break;
    }

    facts.name = element->name;
    facts.qualifiedName = element->qualifiedName;
    // A class and a namespace have no type to show, and are shown by name.
    facts.type = element->declaration.isEmpty() ? element->qualifiedName : element->declaration;
    facts.aliasedType = element->type;
    facts.signature = element->signature;
    facts.iconType = element->icon;
    facts.enumName = element->enumName;
    facts.enumUnqualifiedName = element->enumUnqualifiedName;
    facts.enumeratorValue = element->enumeratorValue;
    facts.typeClassName = element->typeClassName;
    // A link counts its column from zero where this model counts from one.
    facts.link = Link(FilePath::fromUserInput(element->place.filePath), element->place.line,
                      qMax(0, element->place.column - 1));
    return facts;
}

#endif // QTC_WITH_CXX_FRONTEND

#ifdef QTC_WITH_CXX_FRONTEND

// The class the cursor is on, as the cxx-frontend model reads it, handed
// back as a symbol of the parse a hierarchy is drawn from -- which is what
// draws it, this model handing out no symbols. Nothing where it has not read
// the file, where the cursor is on something that is not a class, or where
// the class is not in that parse.
//
// An alias is left alone: the built-in reading follows a typedef through to
// the class behind it, and what this model says of one is the alias.
static Symbol *classOnTheModel(const Document::Ptr &document, const Snapshot &snapshot,
                               int line, int column)
{
    const std::optional<CPlusPlus::CxxFrontendDocument::Element> element
        = cxxFrontendElementAt(document->filePath(), line, column);
    if (!element || element->kind != CPlusPlus::CxxFrontendDocument::Kind::Class)
        return nullptr;

    // Out of the parse the hierarchy works in: the editor's own for the file
    // being edited, the snapshot's for a header it reads.
    const FilePath classFile = FilePath::fromUserInput(element->place.filePath);
    return classFile == document->filePath()
               ? builtinClassWrittenAt(document, element->place)
               : builtinClassWrittenAt(snapshot, element->place);
}

#endif // QTC_WITH_CXX_FRONTEND

static std::shared_ptr<CppElement> handleLookupItemMatch(const ExecData &execData,
                                                         SymbolFinder symbolFinder)
{
    // Never null: whoever got here found a declaration, and a lookup item
    // without one never gets this far.
    Symbol *declaration = execData.lookupItem.declaration();
    LookupContext contextToUse = execData.context;
    if (isCppClass(declaration)) {
        declaration = followClassDeclaration(declaration, execData.snapshot, symbolFinder,
                                             &contextToUse);
    }
    return elementOf(builtinFactsOf(declaration, contextToUse, execData.lookupItem.scope()));
}

//  special case for bug QTCREATORBUG-4780
static bool shouldOmitElement(const LookupItem &lookupItem, const Scope *scope)
{
    return !lookupItem.declaration() && scope && scope->asFunction()
            && lookupItem.type().match(scope->asFunction()->returnType());
}

static QFuture<std::shared_ptr<CppElement>> createFinishedFuture()
{
    QFutureInterface<std::shared_ptr<CppElement>> futureInterface;
    futureInterface.reportStarted();
    futureInterface.reportFinished();
    return futureInterface.future();
}

static LookupItem findLookupItem(const CPlusPlus::Snapshot &snapshot, const SourceData &sourceData,
                                 LookupContext *lookupContext, bool followTypedef)
{
    TypeOfExpression typeOfExpression;
    typeOfExpression.init(sourceData.doc, snapshot);
    // make possible to instantiate templates
    typeOfExpression.setExpandTemplates(true);
    const QList<LookupItem> &lookupItems = typeOfExpression(sourceData.expression.toUtf8(),
                                                            sourceData.scope);
    *lookupContext = typeOfExpression.context();
    if (lookupItems.isEmpty())
        return LookupItem();

    auto isInteresting = [followTypedef](Symbol *symbol) {
        return symbol && (!followTypedef || (symbol->asClass() || symbol->asTemplate()
               || symbol->asForwardClassDeclaration() || symbol->isTypedef()));
    };

    for (const LookupItem &item : lookupItems) {
        if (shouldOmitElement(item, sourceData.scope))
            continue;
        Symbol *symbol = item.declaration();
        if (!isInteresting(symbol))
            continue;
        if (followTypedef && symbol->isTypedef()) {
            CPlusPlus::NamedType *namedType = symbol->type()->asNamedType();
            if (!namedType) {
                // Anonymous aggregate such as: typedef struct {} Empty;
                continue;
            }
            return TypeHierarchyBuilder::followTypedef(*lookupContext,
                         namedType->name(), symbol->enclosingScope());
        }
        return item;
    }
    return LookupItem();
}

static QFuture<std::shared_ptr<CppElement>> exec(SourceFunction &&sourceFunction,
                                                 ExecFunction &&execFunction,
                                                 bool followTypedef = true)
{
    const Snapshot &snapshot = CppModelManager::snapshot();

    const auto inputData = std::invoke(std::forward<SourceFunction>(sourceFunction), snapshot);
    if (!inputData)
        return createFinishedFuture();

    LookupContext lookupContext;
    LookupItem lookupItem;
    if (inputData->resolved) {
        lookupItem.setDeclaration(inputData->resolved);
        lookupItem.setType(inputData->resolved->type());
        lookupItem.setScope(inputData->scope);
        lookupContext = LookupContext(inputData->doc, snapshot);
    } else {
        lookupItem = findLookupItem(snapshot, *inputData, &lookupContext, followTypedef);
    }
    if (!lookupItem.declaration())
        return createFinishedFuture();

    return std::invoke(std::forward<ExecFunction>(execFunction),
                       ExecData{snapshot, lookupItem, lookupContext,
                                CppModelManager::workingCopy()});
}

static QFuture<std::shared_ptr<CppElement>> asyncExec(const ExecData &execData)
{
    return Utils::asyncRun(&createTypeHierarchy, execData, *CppModelManager::symbolFinder());
}

class FromExpressionFunctor
{
public:
    FromExpressionFunctor(const QString &expression, const FilePath &filePath)
        : m_expression(expression)
        , m_filePath(filePath)
    {}

    std::optional<SourceData> operator()(const CPlusPlus::Snapshot &snapshot)
    {
        Document::Ptr doc = snapshot.document(m_filePath);
        if (doc.isNull())
            return {};

        return SourceData{doc, doc->globalNamespace(), m_expression};
    }
private:
    const QString m_expression;
    const FilePath m_filePath;
};

QFuture<std::shared_ptr<CppElement>> CppElementEvaluator::asyncExecute(const QString &expression,
                                                                       const FilePath &filePath)
{
    return exec(FromExpressionFunctor(expression, filePath), asyncExec);
}

class FromGuiFunctor
{
public:
    FromGuiFunctor(TextEditor::TextEditorWidget *editor)
        : m_editor(editor)
        , m_tc(editor->textCursor())
    {}

    std::optional<SourceData> operator()(const CPlusPlus::Snapshot &snapshot)
    {
        Document::Ptr doc;
        doc = snapshot.document(m_editor->textDocument()->filePath());
        if (!doc)
            return {};

        int line = 0;
        int column = 0;
        const int pos = m_tc.position();
        m_editor->convertPosition(pos, &line, &column);

        checkDiagnosticMessage(pos);

        if (matchIncludeFile(doc, line) || matchMacroInUse(doc, pos))
            return {};

#ifdef QTC_WITH_CXX_FRONTEND
        // The cxx-frontend model answers about a place, so nothing has to be
        // cut out of the text and looked up again. Only where an element is
        // what is wanted: a hierarchy is drawn from the symbol itself, which
        // this model does not hand out.
        if (m_forAnElement) {
            if (const std::optional<CppElementFacts> facts
                = modelFactsAt(m_editor->textDocument()->filePath(), line, column + 1)) {
                m_element = elementOf(*facts);
                return {};
            }
        }
#endif

#ifdef QTC_WITH_CXX_FRONTEND
        // The class a hierarchy is drawn from, where the other model can say
        // which one it is: a place rather than an expression cut out of the
        // text and resolved again.
        if (!m_forAnElement) {
            if (Symbol * const onTheModel = classOnTheModel(doc, snapshot, line, column + 1))
                return SourceData{doc, doc->scopeAt(line, column), {}, onTheModel};
        }
#endif

        moveCursorToEndOfIdentifier(&m_tc);
        ExpressionUnderCursor expressionUnderCursor(doc->languageFeatures());
        return SourceData{doc, doc->scopeAt(line, column), expressionUnderCursor(m_tc)};
    }
    QFuture<std::shared_ptr<CppElement>> syncExec(const ExecData &execData);

private:
    void checkDiagnosticMessage(int pos);
    bool matchIncludeFile(const CPlusPlus::Document::Ptr &document, int line);
    bool matchMacroInUse(const CPlusPlus::Document::Ptr &document, int pos);

public:
    void clear();

    TextEditor::TextEditorWidget *m_editor;
    QTextCursor m_tc;
    std::shared_ptr<CppElement> m_element;
    QString m_diagnosis;

    // What this reading is for: an element a reader is shown, rather than
    // the symbol a hierarchy is drawn from.
    bool m_forAnElement = false;
};

QFuture<std::shared_ptr<CppElement>> FromGuiFunctor::syncExec(const ExecData &execData)
{
    QFutureInterface<std::shared_ptr<CppElement>> futureInterface;
    futureInterface.reportStarted();
    m_element = handleLookupItemMatch(execData, *CppModelManager::symbolFinder());
    futureInterface.reportResult(m_element);
    futureInterface.reportFinished();
    return futureInterface.future();
}

void FromGuiFunctor::checkDiagnosticMessage(int pos)
{
    const QList<QTextEdit::ExtraSelection> &selections = m_editor->extraSelections(
        TextEditor::TextEditorWidget::CodeWarningsSelection);
    for (const QTextEdit::ExtraSelection &sel : selections) {
        if (pos >= sel.cursor.selectionStart() && pos <= sel.cursor.selectionEnd()) {
            m_diagnosis = sel.format.toolTip();
            break;
        }
    }
}

bool FromGuiFunctor::matchIncludeFile(const Document::Ptr &document, int line)
{
    const QList<Document::Include> &includes = document->resolvedIncludes();
    for (const Document::Include &includeFile : includes) {
        if (includeFile.line() == line) {
            m_element = std::shared_ptr<CppElement>(new CppInclude(includeFile));
            return true;
        }
    }
    return false;
}

bool FromGuiFunctor::matchMacroInUse(const Document::Ptr &document, int pos)
{
    for (const Document::MacroUse &use : document->macroUses()) {
        if (use.containsUtf16charOffset(pos)) {
            const int begin = use.utf16charsBegin();
            if (pos < begin + use.macro().nameToQString().size()) {
                m_element = std::shared_ptr<CppElement>(new CppMacro(use.macro()));
                return true;
            }
        }
    }
    return false;
}

void FromGuiFunctor::clear()
{
    m_element.reset();
    m_diagnosis.clear();
}

class CppElementEvaluatorPrivate
{
public:
    CppElementEvaluatorPrivate(TextEditor::TextEditorWidget *editor) : m_functor(editor)
    {
        // Whoever holds an evaluator of their own asks it for an element:
        // the hierarchy goes through asyncExecute(), which reads with a
        // functor of its own.
        m_functor.m_forAnElement = true;
    }
    FromGuiFunctor m_functor;
};

CppElementEvaluator::CppElementEvaluator(TextEditor::TextEditorWidget *editor)
    : d(new CppElementEvaluatorPrivate(editor))
{}

CppElementEvaluator::~CppElementEvaluator()
{
    delete d;
}

void CppElementEvaluator::setTextCursor(const QTextCursor &tc)
{
    d->m_functor.m_tc = tc;
}

QFuture<std::shared_ptr<CppElement>> CppElementEvaluator::asyncExecute(
        TextEditor::TextEditorWidget *editor)
{
    return exec(FromGuiFunctor(editor), asyncExec);
}

void CppElementEvaluator::execute()
{
    d->m_functor.clear();
    const auto execFunction = [this](const ExecData &execData) {
        return d->m_functor.syncExec(execData);
    };
    exec(std::ref(d->m_functor), execFunction, false);
}

const std::shared_ptr<CppElement> &CppElementEvaluator::cppElement() const
{
    return d->m_functor.m_element;
}

bool CppElementEvaluator::hasDiagnosis() const
{
    return !d->m_functor.m_diagnosis.isEmpty();
}

const QString &CppElementEvaluator::diagnosis() const
{
    return d->m_functor.m_diagnosis;
}

Utils::Link CppElementEvaluator::linkFromExpression(const QString &expression, const FilePath &filePath)
{
    const Snapshot &snapshot = CppModelManager::snapshot();
    Document::Ptr doc = snapshot.document(filePath);
    if (doc.isNull())
        return Utils::Link();
    Scope *scope = doc->globalNamespace();

    TypeOfExpression typeOfExpression;
    typeOfExpression.init(doc, snapshot);
    typeOfExpression.setExpandTemplates(true);
    const QList<LookupItem> &lookupItems = typeOfExpression(expression.toUtf8(), scope);
    if (lookupItems.isEmpty())
        return Utils::Link();

    for (const LookupItem &item : lookupItems) {
        Symbol *symbol = item.declaration();
        if (!symbol)
            continue;
        if (!symbol->asClass() && !symbol->asTemplate())
            continue;
        return symbol->toLink();
    }
    return Utils::Link();
}

} // namespace CppEditor::Internal
