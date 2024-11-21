// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "astcheck.h"
#include "modelutils.h"

#include <qmljs/parser/qmljsast_p.h>
#include <qmljs/qmljsutils.h>

#include <utils/algorithm.h>
#include <utils/qtcassert.h>
#include <utils/qtcsettings.h>

#include <QColor>
#include <QDir>
#include <QRegularExpression>

using namespace QmlJS;
using namespace QmlJS::AST;
using namespace QmlJS::StaticAnalysis;

namespace {

class DeclarationsCheck : protected Visitor
{
public:
    QList<Message> operator()(FunctionExpression *function)
    {
        clear();
        for (FormalParameterList *plist = function->formals; plist; plist = plist->next) {
            if (!plist->element->bindingIdentifier.isEmpty())
                m_formalParameterNames += plist->element->bindingIdentifier.toString();
        }

        Node::accept(function->body, this);
        return m_messages;
    }

    QList<Message> operator()(Node *node)
    {
        clear();
        Node::accept(node, this);
        return m_messages;
    }

private:
    void clear()
    {
        m_messages.clear();
        m_declaredFunctions.clear();
        m_declaredVariables.clear();
        m_possiblyUndeclaredUses.clear();
        m_seenNonDeclarationStatement = false;
        m_formalParameterNames.clear();
        QTC_ASSERT(m_block == 0, m_block = 0);
    }

    void postVisit(Node *ast) override
    {
        if (!m_seenNonDeclarationStatement && ast->statementCast() && !cast<VariableStatement *>(ast)) {
            m_seenNonDeclarationStatement = true;
        }
    }

    bool visit(IdentifierExpression *ast) override
    {
        if (ast->name.isEmpty())
            return false;
        const QString &name = ast->name.toString();
        if (!m_declaredFunctions.contains(name)
            && !(m_declaredVariables.contains(name)
                 || m_declaredBlockVariables.contains({name, m_block}))) {
            m_possiblyUndeclaredUses[name].append(ast->identifierToken);
        }
        return false;
    }

    bool visit(VariableStatement *ast) override
    {
        if (m_seenNonDeclarationStatement)
            addMessage(HintDeclarationsShouldBeAtStartOfFunction, ast->declarationKindToken);
        return true;
    }

    bool visit(PatternElement *ast) override
    {
        if (ast->bindingIdentifier.isEmpty() || !ast->isVariableDeclaration())
            return true;
        const QString &name = ast->bindingIdentifier.toString();
        VariableScope scope = ast->scope;
        if (m_formalParameterNames.contains(name)) {
            addMessage(WarnAlreadyFormalParameter, ast->identifierToken, name);
        } else if (m_declaredFunctions.contains(name)) {
            addMessage(WarnAlreadyFunction, ast->identifierToken, name);
        } else if (scope == VariableScope::Let || scope == VariableScope::Const) {
            if (m_declaredBlockVariables.contains({name, m_block}))
                addMessage(WarnDuplicateDeclaration, ast->identifierToken, name);
        } else if (scope == VariableScope::Var) {
            if (m_declaredVariables.contains(name)) {
                addMessage(WarnDuplicateDeclaration, ast->identifierToken, name);
            } else {
                const auto found = std::find_if(m_declaredBlockVariables.keyBegin(),
                                                m_declaredBlockVariables.keyEnd(),
                                                [name](const auto &key) { return key.first == name; });
                if (found != m_declaredBlockVariables.keyEnd())
                    addMessage(WarnDuplicateDeclaration, ast->identifierToken, name);
            }
        }

        if (m_possiblyUndeclaredUses.contains(name)) {
            const QList<SourceLocation> values = m_possiblyUndeclaredUses.value(name);
            for (const SourceLocation &loc : values) {
                addMessage(WarnVarUsedBeforeDeclaration, loc, name);
            }
            m_possiblyUndeclaredUses.remove(name);
        }
        if (scope == VariableScope::Let || scope == VariableScope::Const)
            m_declaredBlockVariables[{name, m_block}] = ast;
        else
            m_declaredVariables[name] = ast;

        return true;
    }

    bool visit(FunctionDeclaration *ast) override
    {
        if (m_seenNonDeclarationStatement)
            addMessage(HintDeclarationsShouldBeAtStartOfFunction, ast->functionToken);

        return visit(static_cast<FunctionExpression *>(ast));
    }

    bool visit(FunctionExpression *ast) override
    {
        if (ast->name.isEmpty())
            return false;
        const QString &name = ast->name.toString();

        if (m_formalParameterNames.contains(name))
            addMessage(WarnAlreadyFormalParameter, ast->identifierToken, name);
        else if (m_declaredVariables.contains(name)
                 || m_declaredBlockVariables.contains({name, m_block}))
            addMessage(WarnAlreadyVar, ast->identifierToken, name);
        else if (m_declaredFunctions.contains(name))
            addMessage(WarnDuplicateDeclaration, ast->identifierToken, name);

        if (FunctionDeclaration *decl = cast<FunctionDeclaration *>(ast)) {
            if (m_possiblyUndeclaredUses.contains(name)) {
                const QList<SourceLocation> values = m_possiblyUndeclaredUses.value(name);
                for (const SourceLocation &loc : values) {
                    addMessage(WarnFunctionUsedBeforeDeclaration, loc, name);
                }
                m_possiblyUndeclaredUses.remove(name);
            }
            m_declaredFunctions[name] = decl;
        }

        return false;
    }

    bool openBlock()
    {
        ++m_block;
        return true;
    }

    void closeBlock()
    {
        auto it = m_declaredBlockVariables.begin();
        auto end = m_declaredBlockVariables.end();
        while (it != end) {
            if (it.key().second == m_block)
                it = m_declaredBlockVariables.erase(it);
            else
                ++it;
        }
        --m_block;
    }

    bool visit(Block *) override
    {
        return openBlock();
    }

    void endVisit(Block *) override
    {
        closeBlock();
    }

    bool visit(Catch *) override
    {
        return openBlock();
    }

    void endVisit(Catch *) override
    {
        closeBlock();
    }

    void throwRecursionDepthError() override
    {
        addMessage(ErrHitMaximumRecursion, SourceLocation());
    }

    void addMessage(StaticAnalysis::Type type, const SourceLocation &loc, const QString &arg1 = QString())
    {
        m_messages.append(Message(type, loc, arg1));
    }

    QList<Message> m_messages;
    QStringList m_formalParameterNames;
    QHash<QString, PatternElement *> m_declaredVariables;
    QHash<QPair<QString, uint>, PatternElement *> m_declaredBlockVariables;
    QHash<QString, FunctionDeclaration *> m_declaredFunctions;
    QHash<QString, QList<SourceLocation>> m_possiblyUndeclaredUses;
    bool m_seenNonDeclarationStatement;
    uint m_block = 0;
};

class VisualAspectsPropertyBlackList : public QStringList
{
public:
   VisualAspectsPropertyBlackList() : QStringList({"x",
                                                   "y",
                                                   "z",
                                                   "width",
                                                   "height",
                                                   "color",
                                                   "opacity",
                                                   "scale",
                                                   "rotation",
                                                   "margins",
                                                   "verticalCenterOffset",
                                                   "horizontalCenterOffset",
                                                   "baselineOffset",
                                                   "bottomMargin",
                                                   "topMargin",
                                                   "leftMargin",
                                                   "rightMargin",
                                                   "baseline",
                                                   "centerIn",
                                                   "fill",
                                                   "left",
                                                   "right",
                                                   "mirrored",
                                                   "verticalCenter"
                                                   "horizontalCenter"})
   {}
};

} // end of anonymous namespace

namespace QmlDesigner {

Q_GLOBAL_STATIC(VisualAspectsPropertyBlackList, visualAspectsPropertyBlackList)

QList<StaticAnalysis::Type> AstCheck::defaultDisabledMessages()
{
    static const QList<StaticAnalysis::Type> disabled = Utils::sorted(QList<StaticAnalysis::Type>{
        HintAnonymousFunctionSpacing,
        HintDeclareVarsInOneLine,
        HintDeclarationsShouldBeAtStartOfFunction,
        HintBinaryOperatorSpacing,
        HintOneStatementPerLine,
        HintExtraParentheses,
        WarnAliasReferRootHierarchy,

        // QmlDesigner related
        WarnImperativeCodeNotEditableInVisualDesigner,
        WarnUnsupportedTypeInVisualDesigner,
        WarnReferenceToParentItemNotSupportedByVisualDesigner,
        WarnUndefinedValueForVisualDesigner,
        WarnStatesOnlyInRootItemForVisualDesigner,
        ErrUnsupportedRootTypeInVisualDesigner,
        ErrInvalidIdeInVisualDesigner,

    });
    return disabled;
}

QList<StaticAnalysis::Type> AstCheck::defaultDisabledMessagesForNonQuickUi()
{
    static const QList<StaticAnalysis::Type> disabled = Utils::sorted(QList<StaticAnalysis::Type>{
        // QmlDesigner related
        ErrUnsupportedRootTypeInQmlUi,
        ErrUnsupportedTypeInQmlUi,
        ErrFunctionsNotSupportedInQmlUi,
        ErrBlocksNotSupportedInQmlUi,
        ErrBehavioursNotSupportedInQmlUi,
        ErrStatesOnlyInRootItemInQmlUi,
        ErrReferenceToParentItemNotSupportedInQmlUi,
        WarnDoNotMixTranslationFunctionsInQmlUi,
    });
    return disabled;
}

AstCheck::AstCheck(Document::Ptr doc)
    : m_document(doc)
    , _importsOk(false)
    , _inStatementBinding(false)

{
    m_enabledMessages = Utils::toSet(Message::allMessageTypes());

    if (!isQtQuick2Ui()) {
        for (auto type : defaultDisabledMessagesForNonQuickUi())
            disableMessage(type);
    }

    enableQmlDesignerChecks();
}

AstCheck::~AstCheck() {}

QList<Message> AstCheck::operator()()
{
    m_messages.clear();

    Node::accept(m_document->ast(), this);
    return m_messages;
}

void AstCheck::enableMessage(StaticAnalysis::Type type)
{
    m_enabledMessages.insert(type);
}

void AstCheck::disableMessage(StaticAnalysis::Type type)
{
    m_enabledMessages.remove(type);
}

void AstCheck::enableQmlDesignerChecks()
{
    enableMessage(WarnImperativeCodeNotEditableInVisualDesigner);
    enableMessage(WarnUnsupportedTypeInVisualDesigner);
    enableMessage(WarnReferenceToParentItemNotSupportedByVisualDesigner);
    enableMessage(ErrUnsupportedRootTypeInVisualDesigner);
    enableMessage(ErrInvalidIdeInVisualDesigner);
    enableMessage(WarnAliasReferRootHierarchy);
    //## triggers too often ## check.enableMessage(StaticAnalysis::WarnUndefinedValueForVisualDesigner);
}

void AstCheck::enableQmlDesignerUiFileChecks()
{
    enableMessage(ErrUnsupportedRootTypeInQmlUi);
    enableMessage(ErrUnsupportedTypeInQmlUi);
    enableMessage(ErrFunctionsNotSupportedInQmlUi);
    enableMessage(ErrBlocksNotSupportedInQmlUi);
    enableMessage(ErrBehavioursNotSupportedInQmlUi);
    enableMessage(ErrStatesOnlyInRootItemInQmlUi);
    enableMessage(ErrReferenceToParentItemNotSupportedInQmlUi);
    enableMessage(WarnDoNotMixTranslationFunctionsInQmlUi);
}

void AstCheck::disableQmlDesignerUiFileChecks()
{
    disableMessage(ErrUnsupportedRootTypeInQmlUi);
    disableMessage(ErrUnsupportedTypeInQmlUi);
    disableMessage(ErrFunctionsNotSupportedInQmlUi);
    disableMessage(ErrBlocksNotSupportedInQmlUi);
    disableMessage(ErrBehavioursNotSupportedInQmlUi);
    disableMessage(ErrStatesOnlyInRootItemInQmlUi);
    disableMessage(ErrReferenceToParentItemNotSupportedInQmlUi);
    disableMessage(WarnDoNotMixTranslationFunctionsInQmlUi);
}

bool AstCheck::preVisit(Node *ast)
{
    m_chain.append(ast);
    return true;
}

void AstCheck::postVisit(Node *)
{
    m_chain.removeLast();
}

bool AstCheck::visit(UiProgram *)
{
    return true;
}

bool AstCheck::visit(UiImport *ast)
{
    ShortImportInfo info;
    if (auto ver = ast->version)
        info.second = LanguageUtils::ComponentVersion(ver->majorVersion, ver->minorVersion);

    if (!ast->fileName.isNull())  // it must be a file import
        info.first = ast->fileName.toString();
    else                          // no file import - construct full uri
        info.first = toString(ast->importUri);

    if (m_importInfo.contains(info)) {
        SourceLocation location = ast->firstSourceLocation();
        location.length = ast->lastSourceLocation().end();
        addMessage(WarnDuplicateImport, location, info.first);
    }
    m_importInfo.append(info);
    return true;
}

bool AstCheck::visit(UiObjectInitializer *)
{
    QString typeName;
    m_propertyStack.push(StringSet());
    UiQualifiedId *qualifiedTypeId = qualifiedTypeNameId(parent());
    if (qualifiedTypeId) {
        typeName = qualifiedTypeId->name.toString();
        if (typeName == "Component") {
            m_idStack.push(StringSet());
            _componentChildCount = 0;
        }
    }

    m_typeStack.push(typeName);

    if (m_idStack.isEmpty())
        m_idStack.push(StringSet());

    return true;
}

bool AstCheck::visit(AST::UiEnumDeclaration *ast)
{
    return true;
}

bool AstCheck::visit(AST::UiEnumMemberList *ast)
{
    QStringList names;
    for (auto it = ast; it; it = it->next) {
        if (!it->member.first().isUpper())
            addMessage(ErrInvalidEnumValue, it->memberToken); // better a different message?
        if (names.contains(it->member)) // duplicate enum value
            addMessage(ErrInvalidEnumValue, it->memberToken); // better a different message?
        names.append(it->member.toString());
    }
    return true;
}

bool AstCheck::visit(AST::TemplateLiteral *ast)
{
    Node::accept(ast->expression, this);
    return true;
}

void AstCheck::endVisit(UiObjectInitializer *uiObjectInitializer)
{
    Q_UNUSED(uiObjectInitializer)

    m_propertyStack.pop();

    const QString type = m_typeStack.pop();

    if (type == "Component" && _componentChildCount == 0) {
        SourceLocation loc;
        UiObjectDefinition *objectDefinition = cast<UiObjectDefinition *>(parent());
        if (objectDefinition)
            loc = objectDefinition->qualifiedTypeNameId->identifierToken;
        UiObjectBinding *objectBinding = cast<UiObjectBinding *>(parent());
        if (objectBinding)
            loc = objectBinding->qualifiedTypeNameId->identifierToken;
        addMessage(WarnComponentRequiresChildren, loc);
    }

    UiObjectDefinition *objectDefinition = cast<UiObjectDefinition *>(parent());
    if (objectDefinition && objectDefinition->qualifiedTypeNameId->name == QLatin1String("Component"))
        m_idStack.pop();
    UiObjectBinding *objectBinding = cast<UiObjectBinding *>(parent());
    if (objectBinding && objectBinding->qualifiedTypeNameId->name == QLatin1String("Component"))
        m_idStack.pop();
}

void AstCheck::throwRecursionDepthError()
{
    addMessage(ErrHitMaximumRecursion, SourceLocation());
}

void AstCheck::checkProperty(UiQualifiedId *qualifiedId)
{
    const QString id = toString(qualifiedId);

    if (id.isEmpty())
        return;

    if (id.at(0).isLower()) {
        if (m_propertyStack.top().contains(id))
            addMessage(ErrPropertiesCanOnlyHaveOneBinding, fullLocationForQualifiedId(qualifiedId));
        m_propertyStack.top().insert(id);
    }
}

bool AstCheck::visit(UiObjectDefinition *ast)
{
    visitQmlObject(ast, ast->qualifiedTypeNameId, ast->initializer);
    return false;
}

bool AstCheck::visit(UiObjectBinding *ast)
{
    if (!ast->hasOnToken) {
        checkProperty(ast->qualifiedId);
    } else {
        //addMessage(ErrBehavioursNotSupportedInQmlUi, locationFromRange(ast->firstSourceLocation(), ast->lastSourceLocation()));
    }

    if (!m_typeStack.isEmpty() && m_typeStack.last() == "State"
        && toString(ast->qualifiedId) == "when") {
        addMessage(
            ErrWhenConditionCannotBeObject,
            locationFromRange(ast->firstSourceLocation(), ast->lastSourceLocation()));
    }

    visitQmlObject(ast, ast->qualifiedTypeNameId, ast->initializer);
    return false;
}

static bool expressionAffectsVisualAspects(BinaryExpression *expression)
{
    if (expression->op == QSOperator::Assign
            || expression->op == QSOperator::InplaceSub
            || expression->op == QSOperator::InplaceAdd
            || expression->op == QSOperator::InplaceDiv
            || expression->op == QSOperator::InplaceMul
            || expression->op == QSOperator::InplaceOr
            || expression->op == QSOperator::InplaceXor
            || expression->op == QSOperator::InplaceAnd) {

        const ExpressionNode *lhsValue = expression->left;

        if (const IdentifierExpression* identifierExpression = cast<const IdentifierExpression *>(lhsValue)) {
            if (visualAspectsPropertyBlackList()->contains(identifierExpression->name.toString()))
                return true;
        } else if (const FieldMemberExpression* fieldMemberExpression = cast<const FieldMemberExpression *>(lhsValue)) {
            if (visualAspectsPropertyBlackList()->contains(fieldMemberExpression->name.toString()))
                return true;
        }
    }
    return false;
}

static UiQualifiedId *getRightMostIdentifier(UiQualifiedId *typeId)
{
        if (typeId->next)
            return getRightMostIdentifier(typeId->next);

        return typeId;
}

static bool checkTopLevelBindingForParentReference(ExpressionStatement *expStmt, const QString &source)
{
    if (!expStmt)
        return false;

    SourceLocation location = locationFromRange(expStmt->firstSourceLocation(), expStmt->lastSourceLocation());
    QString stmtSource = source.mid(int(location.begin()), int(location.length));

    if (stmtSource.contains(QRegularExpression("(^|\\W)parent\\.")))
        return true;

    return false;
}

void AstCheck::visitQmlObject(Node *ast, UiQualifiedId *typeId, UiObjectInitializer *initializer)
{
    const SourceLocation typeErrorLocation = fullLocationForQualifiedId(typeId);

    const QString typeName = getRightMostIdentifier(typeId)->name.toString();

    if (!m_typeStack.isEmpty() && m_typeStack.last() == "State"
            && typeId->name.toString() != "AnchorChanges"
            && typeId->name.toString() != "ParentChange"
            && typeId->name.toString() != "PropertyChanges"
            && typeId->name.toString() != "StateChangeScript")
        addMessage(StateCannotHaveChildItem, typeErrorLocation, typeName);

    if (typeId->next == nullptr && m_document->fileName().baseName() == typeName)
        addMessage(ErrTypeIsInstantiatedRecursively, typeErrorLocation, typeName);

    if (m_typeStack.count() > 1 && typeName == "State" && m_typeStack.last() != "StateGroup") {
        addMessage(WarnStatesOnlyInRootItemForVisualDesigner, typeErrorLocation);
        addMessage(ErrStatesOnlyInRootItemInQmlUi, typeErrorLocation);
    }

    if (!m_typeStack.isEmpty() && m_typeStack.last() == "Component") {
        _componentChildCount++;
        if (_componentChildCount > 1)
            addMessage(ErrToManyComponentChildren, typeErrorLocation);
    }

    Node::accept(initializer, this);
}

bool AstCheck::visit(UiScriptBinding *ast)
{
    // special case for id property
    if (ast->qualifiedId->name == QLatin1String("id") && !ast->qualifiedId->next) {
        if (! ast->statement)
            return false;

        const SourceLocation loc = locationFromRange(ast->statement->firstSourceLocation(),
                                                     ast->statement->lastSourceLocation());

        ExpressionStatement *expStmt = cast<ExpressionStatement *>(ast->statement);
        if (!expStmt) {
            addMessage(ErrIdExpected, loc);
            return false;
        }

        QString id;
        if (IdentifierExpression *idExp = cast<IdentifierExpression *>(expStmt->expression)) {
            id = idExp->name.toString();
        } else if (StringLiteral *strExp = cast<StringLiteral *>(expStmt->expression)) {
            id = strExp->value.toString();
            addMessage(ErrInvalidId, loc);
        } else {
            addMessage(ErrIdExpected, loc);
            return false;
        }

        if (id.isEmpty() || (!id.at(0).isLower() && id.at(0) != '_')) {
            addMessage(ErrInvalidId, loc);
            return false;
        }

        if (ModelUtils::isBannedQmlId(id))
            addMessage(ErrInvalidIdeInVisualDesigner, loc);

        if (m_idStack.top().contains(id)) {
            addMessage(ErrDuplicateId, loc);
            return false;
        }
        m_idStack.top().insert(id);
    }

    if (m_typeStack.count() == 1
        && visualAspectsPropertyBlackList()->contains(ast->qualifiedId->name.toString())
        && checkTopLevelBindingForParentReference(cast<ExpressionStatement *>(ast->statement),
                                                  m_document->source())) {
        addMessage(WarnReferenceToParentItemNotSupportedByVisualDesigner,
                   locationFromRange(ast->firstSourceLocation(), ast->lastSourceLocation()));
        addMessage(ErrReferenceToParentItemNotSupportedInQmlUi,
                   locationFromRange(ast->firstSourceLocation(), ast->lastSourceLocation()));
    }

    checkProperty(ast->qualifiedId);

    if (!ast->statement)
        return false;

    checkBindingRhs(ast->statement);

    Node::accept(ast->qualifiedId, this);

    _inStatementBinding = true;
    Node::accept(ast->statement, this);
    _inStatementBinding = false;

    return false;
}

bool AstCheck::visit(UiArrayBinding *ast)
{
    checkProperty(ast->qualifiedId);

    return true;
}

bool AstCheck::visit(UiPublicMember *ast)
{
    if (ast->type == UiPublicMember::Property) {
        const QStringView typeName = ast->memberType->name;

        // Check alias properties don't reference root item
        // Item {
        //     id: root
        //     property alias p1: root
        //     property alias p2: root.child
        //
        //     Item { id: child }
        // }
        // - Show error for alias property p1
        // - Show warning for alias property p2

        // Check if type and id stack only contain one item as we are only looking for alias
        // properties in the root item.
        if (typeName == QLatin1String("alias") && ast->type == AST::UiPublicMember::Property
            && m_typeStack.count() == 1 && m_idStack.count() == 1 && m_idStack.top().count() == 1) {

            const QString rootId = m_idStack.top().values().first();
            if (!rootId.isEmpty()) {
                if (ExpressionStatement *exp = cast<ExpressionStatement *>(ast->statement)) {
                    ExpressionNode *node = exp->expression;

                    // Check for case property alias p1: root
                    if (IdentifierExpression *idExp = cast<IdentifierExpression *>(node)) {
                        if (!idExp->name.isEmpty() && idExp->name.toString() == rootId)
                            addMessage(ErrAliasReferRoot, idExp->identifierToken);

                    // Check for case property alias p2: root.child
                    } else if (FieldMemberExpression *fmExp = cast<FieldMemberExpression *>(node)) {
                        if (IdentifierExpression *base = cast<IdentifierExpression *>(fmExp->base)) {
                            if (!base->name.isEmpty() && base->name.toString() == rootId)
                                addMessage(WarnAliasReferRootHierarchy, base->identifierToken);
                        }
                    }
                }
            }
        }

        checkBindingRhs(ast->statement);

        _inStatementBinding = true;
        Node::accept(ast->statement, this);
        _inStatementBinding = false;
        Node::accept(ast->binding, this);
    }

    return false;
}

bool AstCheck::visit(IdentifierExpression *)
{
    return true;
}

bool AstCheck::visit(FieldMemberExpression *)
{
    return true;
}

bool AstCheck::visit(FunctionDeclaration *ast)
{
    return visit(static_cast<FunctionExpression *>(ast));
}

bool AstCheck::visit(FunctionExpression *ast)
{
    SourceLocation locfunc = ast->functionToken;
    SourceLocation loclparen = ast->lparenToken;

    if (ast->name.isEmpty()) {
        if (locfunc.isValid() && loclparen.isValid()
                && (locfunc.startLine != loclparen.startLine
                    || locfunc.end() + 1 != loclparen.begin())) {
            addMessage(HintAnonymousFunctionSpacing, locationFromRange(locfunc, loclparen));
        }
    }

    const bool isDirectInConnectionsScope =
            (!m_typeStack.isEmpty() && m_typeStack.last() == "Connections");

    if (!isDirectInConnectionsScope)
        addMessage(ErrFunctionsNotSupportedInQmlUi, locationFromRange(locfunc, loclparen));

    DeclarationsCheck bodyCheck;
    addMessages(bodyCheck(ast));

    Node::accept(ast->formals, this);

    const bool wasInStatementBinding = _inStatementBinding;
    _inStatementBinding = false;

    Node::accept(ast->body, this);

    _inStatementBinding = wasInStatementBinding;

    return false;
}

bool AstCheck::visit(BinaryExpression *ast)
{
    const QString source = m_document->source();

    // check spacing
    SourceLocation op = ast->operatorToken;
    if ((op.begin() > 0 && !source.at(int(op.begin()) - 1).isSpace())
        || (int(op.end()) < source.size() && !source.at(int(op.end())).isSpace())) {
        addMessage(HintBinaryOperatorSpacing, op);
    }

    SourceLocation expressionSourceLocation = locationFromRange(ast->firstSourceLocation(),
                                                                ast->lastSourceLocation());

    const bool isDirectInConnectionsScope = (!m_typeStack.isEmpty()
                                             && m_typeStack.last() == "Connections");

    if (expressionAffectsVisualAspects(ast) && !isDirectInConnectionsScope)
        addMessage(WarnImperativeCodeNotEditableInVisualDesigner, expressionSourceLocation);

    // check ==, !=
    if (ast->op == QSOperator::Equal || ast->op == QSOperator::NotEqual) {
        addMessage(MaybeWarnEqualityTypeCoercion, ast->operatorToken);
    }

    // check odd + ++ combinations
    const QLatin1Char newline('\n');
    if (ast->op == QSOperator::Add || ast->op == QSOperator::Sub) {
        QChar match;
        StaticAnalysis::Type msg;
        if (ast->op == QSOperator::Add) {
            match = '+';
            msg = WarnConfusingPluses;
        } else {
            QTC_CHECK(ast->op == QSOperator::Sub);
            match = '-';
            msg = WarnConfusingMinuses;
        }

        if (int(op.end()) + 1 < source.size()) {
            const QChar next = source.at(int(op.end()));
            if (next.isSpace() && next != newline && source.at(int(op.end()) + 1) == match)
                addMessage(msg, SourceLocation((op.begin()), 3, op.startLine, op.startColumn));
        }
        if (op.begin() >= 2) {
            const QChar prev = source.at(int(op.begin()) - 1);
            if (prev.isSpace() && prev != newline && source.at(int(op.begin()) - 2) == match)
                addMessage(msg, SourceLocation(op.begin() - 2, 3, op.startLine, op.startColumn - 2));
        }
    }

    return true;
}

bool AstCheck::visit(Block *ast)
{

    bool isDirectInConnectionsScope =
            (!m_typeStack.isEmpty() && m_typeStack.last() == "Connections");

    if (!isDirectInConnectionsScope)
        addMessage(ErrBlocksNotSupportedInQmlUi, locationFromRange(ast->firstSourceLocation(), ast->lastSourceLocation()));

    if (Node *p = parent()) {
        if (!cast<UiScriptBinding *>(p)
                && !cast<UiPublicMember *>(p)
                && !cast<TryStatement *>(p)
                && !cast<Catch *>(p)
                && !cast<Finally *>(p)
                && !cast<ForStatement *>(p)
                && !cast<ForEachStatement *>(p)
                && !cast<DoWhileStatement *>(p)
                && !cast<WhileStatement *>(p)
                && !cast<IfStatement *>(p)
                && !cast<SwitchStatement *>(p)
                && !isCaseOrDefault(p)
                && !cast<WithStatement *>(p)
                && hasVarStatement(ast)) {
            addMessage(WarnBlock, ast->lbraceToken);
        }
        if (!ast->statements
                && cast<UiPublicMember *>(p)
                && ast->lbraceToken.startLine == ast->rbraceToken.startLine) {
            addMessage(WarnUnintentinalEmptyBlock, locationFromRange(ast->firstSourceLocation(), ast->lastSourceLocation()));
        }
    }
    return true;
}

bool AstCheck::visit(WithStatement *ast)
{
    addMessage(WarnWith, ast->withToken);
    return true;
}

bool AstCheck::visit(VoidExpression *ast)
{
    addMessage(WarnVoid, ast->voidToken);
    return true;
}

bool AstCheck::visit(Expression *ast)
{
    if (ast->left && ast->right) {
        Node *p = parent();
        if (!cast<ForStatement *>(p)) {
            addMessage(WarnComma, ast->commaToken);
        }
    }
    return true;
}

bool AstCheck::visit(ExpressionStatement *ast)
{
    if (ast->expression) {
        bool ok = cast<CallExpression *>(ast->expression)
                || cast<DeleteExpression *>(ast->expression)
                || cast<PreDecrementExpression *>(ast->expression)
                || cast<PreIncrementExpression *>(ast->expression)
                || cast<PostIncrementExpression *>(ast->expression)
                || cast<PostDecrementExpression *>(ast->expression)
                || cast<YieldExpression *>(ast->expression)
                || cast<FunctionExpression *>(ast->expression);
        if (BinaryExpression *binary = cast<BinaryExpression *>(ast->expression)) {
            switch (binary->op) {
            case QSOperator::Assign:
            case QSOperator::InplaceAdd:
            case QSOperator::InplaceAnd:
            case QSOperator::InplaceDiv:
            case QSOperator::InplaceLeftShift:
            case QSOperator::InplaceRightShift:
            case QSOperator::InplaceMod:
            case QSOperator::InplaceMul:
            case QSOperator::InplaceOr:
            case QSOperator::InplaceSub:
            case QSOperator::InplaceURightShift:
            case QSOperator::InplaceXor:
                ok = true;
                break;
            default: break;
            }
        }
        if (!ok)
            ok = _inStatementBinding;

        if (!ok) {
            addMessage(WarnConfusingExpressionStatement,
                       locationFromRange(ast->firstSourceLocation(), ast->lastSourceLocation()));
        }
    }
    return true;
}

bool AstCheck::visit(IfStatement *ast)
{
    if (ast->expression)
        checkAssignInCondition(ast->expression);
    return true;
}

bool AstCheck::visit(ForStatement *ast)
{
    if (ast->condition)
        checkAssignInCondition(ast->condition);
    return true;
}

bool AstCheck::visit(WhileStatement *ast)
{
    if (ast->expression)
        checkAssignInCondition(ast->expression);
    return true;
}

bool AstCheck::visit(DoWhileStatement *ast)
{
    if (ast->expression)
        checkAssignInCondition(ast->expression);
    return true;
}

bool AstCheck::visit(CaseBlock *ast)
{
    return true;
}

static QString functionName(ExpressionNode *ast, SourceLocation *location)
{
    if (IdentifierExpression *id = cast<IdentifierExpression *>(ast)) {
        if (!id->name.isEmpty()) {
            *location = id->identifierToken;
            return id->name.toString();
        }
    } else if (FieldMemberExpression *fme = cast<FieldMemberExpression *>(ast)) {
        if (!fme->name.isEmpty()) {
            *location = fme->identifierToken;
            return fme->name.toString();
        }
    }
    return QString();
}

static QString functionNamespace(ExpressionNode *ast)
{
   if (FieldMemberExpression *fme = cast<FieldMemberExpression *>(ast)) {
        if (!fme->name.isEmpty()) {
            SourceLocation location;
            return functionName(fme->base, &location);
        }
    }
    return QString();
}

void AstCheck::checkNewExpression(ExpressionNode *ast)
{
    SourceLocation location;
    const QString name = functionName(ast, &location);
    if (name.isEmpty())
        return;
    if (!name.at(0).isUpper())
        addMessage(WarnNewWithLowercaseFunction, location);
}

void AstCheck::checkBindingRhs(Statement *statement)
{
    if (!statement)
        return;

    DeclarationsCheck bodyCheck;
    addMessages(bodyCheck(statement));
}

void AstCheck::checkExtraParentheses(ExpressionNode *expression)
{
    if (NestedExpression *nested = cast<NestedExpression *>(expression))
        addMessage(HintExtraParentheses, nested->lparenToken);
}

void AstCheck::addMessages(const QList<Message> &messages)
{
    for (const Message &msg : messages)
        addMessage(msg);
}

static bool hasOnlySpaces(const QString &s)
{
    for (int i = 0; i < s.size(); ++i)
        if (!s.at(i).isSpace())
            return false;
    return true;
}

void AstCheck::addMessage(const Message &message)
{
    if (message.isValid() && m_enabledMessages.contains(message.type)) {
        if (m_disabledMessageTypesByLine.contains(int(message.location.startLine))) {
            QList<MessageTypeAndSuppression> &disabledMessages
                = m_disabledMessageTypesByLine[int(message.location.startLine)];
            for (int i = 0; i < disabledMessages.size(); ++i) {
                if (disabledMessages[i].type == message.type) {
                    disabledMessages[i].wasSuppressed = true;
                    return;
                }
            }
        }

        m_messages += message;
    }
}

void AstCheck::addMessage(StaticAnalysis::Type type,
                          const SourceLocation &location,
                          const QString &arg1,
                          const QString &arg2)
{
    addMessage(Message(type, location, arg1, arg2));
}

bool AstCheck::isQtQuick2() const
{
    return true;
}

bool AstCheck::isQtQuick2Ui() const
{
    return m_document->language() == Dialect::QmlQtQuick2Ui;
}

bool AstCheck::isCaseOrDefault(Node *n)
{
    if (!cast<StatementList *>(n))
        return false;
    if (Node *p = parent(1))
        return p->kind == Node::Kind_CaseClause || p->kind == Node::Kind_DefaultClause;
    return false;
}

bool AstCheck::hasVarStatement(AST::Block *b) const
{
    QTC_ASSERT(b, return false);
    StatementList *s = b->statements;
    while (s) {
        if (auto var = cast<VariableStatement *>(s->statement)) {
            VariableDeclarationList *declList = var->declarations;
            while (declList) {
                if (declList->declaration && declList->declaration->scope == VariableScope::Var)
                    return true;
                declList = declList->next;
            }
        }
        s = s->next;
    }
    return false;
}

bool AstCheck::visit(NewExpression *ast)
{
    checkNewExpression(ast->expression);
    return true;
}

bool AstCheck::visit(NewMemberExpression *ast)
{
    checkNewExpression(ast->base);

    // check for Number, Boolean, etc constructor usage
    if (IdentifierExpression *idExp = cast<IdentifierExpression *>(ast->base)) {
        const QStringView name = idExp->name;
        if (name == QLatin1String("Number")) {
            addMessage(WarnNumberConstructor, idExp->identifierToken);
        } else if (name == QLatin1String("Boolean")) {
            addMessage(WarnBooleanConstructor, idExp->identifierToken);
        } else if (name == QLatin1String("String")) {
            addMessage(WarnStringConstructor, idExp->identifierToken);
        } else if (name == QLatin1String("Object")) {
            addMessage(WarnObjectConstructor, idExp->identifierToken);
        } else if (name == QLatin1String("Function")) {
            addMessage(WarnFunctionConstructor, idExp->identifierToken);
        }
    }

    return true;
}

bool AstCheck::visit(CallExpression *ast)
{
    // check for capitalized function name being called
    SourceLocation location;
    const QString name = functionName(ast->base, &location);

    const QString namespaceName = functionNamespace(ast->base);

    // We have to allow the translation functions
    static const QStringList translationFunctions = {"qsTr",
                                                     "qsTrId",
                                                     "qsTranslate",
                                                     "qsTrNoOp",
                                                     "qsTrIdNoOp",
                                                     "qsTranslateNoOp",
                                                     "QT_TR_NOOP",
                                                     "QT_TRANSLATE_NOOP",
                                                     "QT_TRID_NOOP"};

    static const QStringList positiveListedFunctions = {
        "toString",    "toFixed",           "toExponential", "toPrecision",    "isFinite",
        "isNaN",       "valueOf",           "toLowerCase",   "toLocaleString", "toLocaleLowerCase",
        "toUpperCase", "toLocaleUpperCase", "substring",     "charAt",         "charCodeAt",
        "concat",      "endsWith",          "includes",      "indexOf",        "lastIndexOf",
        "arg"};

    static const QStringList colorFunctions = {"lighter", "darker", "rgba",  "tint", "hsla", "hsva"};

    static const QStringList qtFunction = {"point",
                                           "rect",
                                           "size",
                                           "vector2d",
                                           "vector3d",
                                           "vector4d",
                                           "quaternion",
                                           "matrix4x4",
                                           "formatDate",
                                           "formatDateTime",
                                           "formatTime",
                                           "resolvedUrl"};

    const bool positiveListedFunction = translationFunctions.contains(name)
                                        || positiveListedFunctions.contains(name)
                                        || colorFunctions.contains(name) || qtFunction.contains(name);

    // We allow the Math. functions
    const bool isMathFunction = namespaceName == "Math";
    const bool isDateFunction = namespaceName == "Date";
    // allow adding connections with the help of Qt Design Studio
    bool isDirectInConnectionsScope =
            (!m_typeStack.isEmpty() && m_typeStack.last() == QLatin1String("Connections"));
    if (!positiveListedFunction && !isMathFunction && !isDateFunction && !isDirectInConnectionsScope)
        addMessage(ErrFunctionsNotSupportedInQmlUi, location);

    if (translationFunctions.contains(name)) {
        TranslationFunction translationFunction = noTranslationfunction;
        if (name == "qsTr" || name == "qsTrNoOp")
            translationFunction = qsTr;
        else if (name == "qsTrId" || name == "qsTrIdNoOp")
            translationFunction = qsTrId;
        else if (name == "qsTranslate" || name == "qsTranslateNoOp")
            translationFunction = qsTranslate;

        if (lastTransLationfunction != noTranslationfunction
            && lastTransLationfunction != translationFunction)
            addMessage(WarnDoNotMixTranslationFunctionsInQmlUi, location);

        lastTransLationfunction = translationFunction;
    }

    static const QStringList globalFunctions = {"String", "Boolean", "Date", "Number", "Object", "Array", "Symbol", "Object", "Function", "RegExp",
                                                "QT_TR_NOOP", "QT_TRANSLATE_NOOP", "QT_TRID_NOOP"};

    if (!name.isEmpty() && name.at(0).isUpper() && !globalFunctions.contains(name)) {
        addMessage(WarnExpectedNewWithUppercaseFunction, location);
    }
    if (cast<IdentifierExpression *>(ast->base) && name == QLatin1String("eval"))
        addMessage(WarnEval, location);
    return true;
}

bool AstCheck::visit(StatementList *ast)
{
    SourceLocation warnStart;
    SourceLocation warnEnd;
    unsigned currentLine = 0;
    for (StatementList *it = ast; it; it = it->next) {
        if (!it->statement)
            continue;
        const SourceLocation itLoc = it->statement->firstSourceLocation();
        if (itLoc.startLine != currentLine) { // first statement on a line
            if (warnStart.isValid())
                addMessage(HintOneStatementPerLine, locationFromRange(warnStart, warnEnd));
            warnStart = SourceLocation();
            currentLine = itLoc.startLine;
        } else { // other statements on the same line
            if (!warnStart.isValid())
                warnStart = itLoc;
            warnEnd = it->statement->lastSourceLocation();
        }
    }
    if (warnStart.isValid())
        addMessage(HintOneStatementPerLine, locationFromRange(warnStart, warnEnd));

    return true;
}

bool AstCheck::visit(ReturnStatement *ast)
{
    checkExtraParentheses(ast->expression);
    return true;
}

bool AstCheck::visit(ThrowStatement *ast)
{
    checkExtraParentheses(ast->expression);
    return true;
}

bool AstCheck::visit(DeleteExpression *ast)
{
    checkExtraParentheses(ast->expression);
    return true;
}

bool AstCheck::visit(TypeOfExpression *ast)
{
    checkExtraParentheses(ast->expression);
    return true;
}

void AstCheck::checkAssignInCondition(AST::ExpressionNode *condition)
{
    if (BinaryExpression *binary = cast<BinaryExpression *>(condition)) {
        if (binary->op == QSOperator::Assign)
            addMessage(WarnAssignmentInCondition, binary->operatorToken);
    }
}

Node *AstCheck::parent(int distance)
{
    const int index = m_chain.size() - 2 - distance;
    if (index < 0)
        return nullptr;
    return m_chain.at(index);
}

} // namespace QmlDesigner
