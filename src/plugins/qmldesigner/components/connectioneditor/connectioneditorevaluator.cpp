// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "connectioneditorevaluator.h"
#include "qmljs/parser/qmljsast_p.h"

#include <utils/ranges.h>

#include <QList>

using namespace QmlDesigner;

using QmlJS::AST::Node;
using Kind = Node::Kind;

namespace {
enum class TrackingArea { No, Condition, Ok, Ko };

template<typename... Ts>
struct Overload : Ts...
{
    using Ts::operator()...;
};
template<class... Ts>
Overload(Ts...) -> Overload<Ts...>;

QByteArray name(Node *ast)
{
    if (!ast)
        return {};

    QByteArray name = typeid(*ast).name();
    return name;
}

inline CES::ConditionToken operator2ConditionToken(const int &op)
{
    using CT = CES::ConditionToken;
    using QSOperator::Op;
    static QHash<Op, CT> conditionHash = {{Op::Le, CT::SmallerEqualsThan},
                                          {Op::Lt, CT::SmallerThan},
                                          {Op::Ge, CT::LargerEqualsThan},
                                          {Op::Gt, CT::LargerThan},
                                          {Op::Equal, CT::Equals},
                                          {Op::NotEqual, CT::Not},
                                          {Op::And, CT::And},
                                          {Op::Or, CT::Or},
                                          {Op::StrictEqual, CT::Equals},
                                          {Op::StrictNotEqual, CT::Not}};
    return conditionHash.value(Op(op), CT::Unknown);
}

inline bool isStatementNode(const int &kind)
{
    static QSet<int> lhsSet = {Kind::Kind_CallExpression,
                               Kind::Kind_FieldMemberExpression,
                               Kind::Kind_IdentifierExpression};
    return lhsSet.contains(kind);
}

inline bool isAcceptedIfBinaryOperator(const int &operation)
{
    switch (operation) {
    case QSOperator::Le:
    case QSOperator::Lt:
    case QSOperator::Ge:
    case QSOperator::Gt:
    case QSOperator::Equal:
    case QSOperator::NotEqual:
    case QSOperator::And:
    case QSOperator::Or:
    case QSOperator::StrictEqual:
    case QSOperator::StrictNotEqual:
    case QSOperator::InplaceXor:
        return true;
    default:
        return false;
    }
}

class NodeStatus
{
public:
    NodeStatus()
        : kind(Kind::Kind_Undefined)
        , m_children(0)
        , m_isStatementNode(false)
    {}

    NodeStatus(Node *node)
        : kind(Kind(node->kind))
        , m_children(0)
        , m_isStatementNode(::isStatementNode(kind))
    {}

    int childId() const { return m_children - 1; }

    int children() const { return m_children; }

    operator Kind() { return this->kind; }

    bool operator==(const int &kind) const { return this->kind == kind; }

    bool operator==(const Kind &kind) const { return this->kind == kind; }

    bool operator!=(const Kind &kind) const { return this->kind != kind; }

    int increaseChildNo() { return m_children++; };

    bool isStatementNode() const { return m_isStatementNode; }

private:
    Kind kind;
    int m_children = 0;
    bool m_isStatementNode = false;
};

class BoolCondition : public QmlJS::AST::Visitor
{
public:
    CES::MatchedCondition matchedCondition() const { return m_condition; }

    void reset()
    {
        m_failed = false;
        m_depth = 0;
        fields.clear();
        identifier.clear();
    }

    bool isValid() { return !m_failed; }

protected:
    bool preVisit(QmlJS::AST::Node *node) override
    {
        if (m_failed)
            return false;

        switch (node->kind) {
        case Kind::Kind_BinaryExpression:
        case Kind::Kind_FieldMemberExpression:
        case Kind::Kind_IdentifierExpression:
        case Kind::Kind_StringLiteral:
        case Kind::Kind_NumericLiteral:
        case Kind::Kind_TrueLiteral:
        case Kind::Kind_FalseLiteral:
            return true;
        default: {
            m_failed = true;
            return false;
        }
        }
    }

    bool visit(QmlJS::AST::BinaryExpression *binaryExpression) override
    {
        if (m_failed)
            return false;

        if (binaryExpression->op == QSOperator::Equal
            && binaryExpression->left->kind == Kind::Kind_FieldMemberExpression
            && binaryExpression->right->kind == Kind::Kind_FieldMemberExpression) {
            m_failed = true;
            return false;
            //            Use \"===\" for comparing two field member expressions.
        }

        if (!isAcceptedIfBinaryOperator(binaryExpression->op)) {
            m_failed = true;
            return false;
        }

        acceptBoolOperand(binaryExpression->left);
        m_condition.tokens.append(operator2ConditionToken(binaryExpression->op));
        acceptBoolOperand(binaryExpression->right);

        return false;
    }

    bool visit(QmlJS::AST::IdentifierExpression *identifier) override
    {
        if (m_failed)
            return false;

        m_depth++;
        return false;
    }

    bool visit(QmlJS::AST::FieldMemberExpression *field) override
    {
        if (m_failed)
            return false;

        m_depth++;
        return true;
    }

    void endVisit(QmlJS::AST::FieldMemberExpression *fieldExpression) override
    {
        if (m_failed)
            return;

        this->fields << fieldExpression->name.toString();
        checkAndResetVariable();
    }

    void endVisit(QmlJS::AST::IdentifierExpression *identifier) override
    {
        if (m_failed)
            return;

        this->identifier = identifier->name.toString();
        checkAndResetVariable();
    }

    void endVisit(QmlJS::AST::StringLiteral *stringLiteral) override
    {
        if (m_failed)
            return;
        m_condition.statements << stringLiteral->value.toString();
    }

    void endVisit(QmlJS::AST::NumericLiteral *numericLiteral) override
    {
        if (m_failed)
            return;
        m_condition.statements << numericLiteral->value;
    }

    void endVisit(QmlJS::AST::TrueLiteral *trueLiteral) override
    {
        if (m_failed)
            return;
        m_condition.statements << true;
    }

    void endVisit(QmlJS::AST::FalseLiteral *falseLiteral) override
    {
        if (m_failed)
            return;
        m_condition.statements << false;
    }

    void throwRecursionDepthError() override{};

    void checkAndResetVariable()
    {
        if (--m_depth == 0) {
            m_condition.statements << CES::Variable{identifier, fields.join(".")};
            identifier.clear();
            fields.clear();
        }
    }

    void acceptBoolOperand(Node *operand)
    {
        BoolCondition boolEvaluator;
        operand->accept(&boolEvaluator);
        if (!boolEvaluator.isValid()) {
            m_failed = true;
            return;
        }

        m_condition.statements.append(boolEvaluator.m_condition.statements);
        m_condition.tokens.append(boolEvaluator.m_condition.tokens);
    }

private:
    bool m_failed = false;
    int m_depth = 0;
    QString identifier;
    QStringList fields;
    CES::MatchedCondition m_condition;
};

class RightHandVisitor : public QmlJS::AST::Visitor
{
public:
    CES::RightHandSide rhs() const { return m_rhs; }

    void reset()
    {
        m_failed = false;
        m_specified = false;
        m_depth = 0;
        fields.clear();
        identifier.clear();
    }

    bool isValid() const { return !m_failed && m_specified; }

    bool isLiteralType() const
    {
        if (!isValid())
            return false;

        return CES::isLiteralType(rhs());
    }

    bool couldBeLHS() const
    {
        if (!isValid())
            return false;
        return std::holds_alternative<CES::Variable>(m_rhs);
    }

    inline bool couldBeVariable() const { return couldBeLHS(); }

    CES::Literal literal() const
    {
        if (!isLiteralType())
            return {};

        return std::visit(Overload{[](const bool &var) -> CES::Literal { return var; },
                                   [](const double &var) -> CES::Literal { return var; },
                                   [](const QString &var) -> CES::Literal { return var; },
                                   [](const auto &) -> CES::Literal { return false; }},
                          m_rhs);
    }

    CES::BindingProperty lhs() const
    {
        if (!couldBeLHS())
            return {};

        CES::Variable var = std::get<CES::Variable>(m_rhs);
        return CES::BindingProperty{var.nodeId + "." + var.propertyName};
    }

    CES::Variable variable() const
    {
        if (!isValid())
            return {};

        if (std::holds_alternative<CES::Variable>(m_rhs))
            return std::get<CES::Variable>(m_rhs);

        return {};
    }

protected:
    bool preVisit(QmlJS::AST::Node *node) override
    {
        if (skipIt())
            setFailed();

        if (m_failed)
            return false;

        switch (node->kind) {
        case Kind::Kind_CallExpression:
        case Kind::Kind_FieldMemberExpression:
        case Kind::Kind_IdentifierExpression:
        case Kind::Kind_StringLiteral:
        case Kind::Kind_NumericLiteral:
        case Kind::Kind_TrueLiteral:
        case Kind::Kind_FalseLiteral:
            return true;
        default: {
            setFailed();
            return false;
        }
        }
    }

    bool visit(QmlJS::AST::CallExpression *call) override
    {
        if (skipIt())
            return false;

        m_depth++;
        return true;
    }

    bool visit(QmlJS::AST::IdentifierExpression *identifier) override
    {
        if (skipIt())
            return false;

        m_depth++;
        return false;
    }

    bool visit(QmlJS::AST::FieldMemberExpression *field) override
    {
        if (skipIt())
            return false;

        m_depth++;
        return true;
    }

    void endVisit(QmlJS::AST::CallExpression *call) override
    {
        if (skipIt())
            return;

        checkAndResetCal();
    }

    void endVisit(QmlJS::AST::IdentifierExpression *identifier) override
    {
        if (skipIt())
            return;

        this->identifier = identifier->name.toString();
        checkAndResetNonCal();
    }

    void endVisit(QmlJS::AST::FieldMemberExpression *fieldExpression) override
    {
        if (skipIt())
            return;

        this->fields << fieldExpression->name.toString();
        checkAndResetNonCal();
    }

    void endVisit(QmlJS::AST::StringLiteral *stringLiteral) override
    {
        if (skipIt())
            return;
        m_rhs = stringLiteral->value.toString();
        m_specified = true;
    }

    void endVisit(QmlJS::AST::NumericLiteral *numericLiteral) override
    {
        if (skipIt())
            return;
        m_rhs = numericLiteral->value;
        m_specified = true;
    }

    void endVisit(QmlJS::AST::TrueLiteral *trueLiteral) override
    {
        if (skipIt())
            return;
        m_rhs = true;
        m_specified = true;
    }

    void endVisit(QmlJS::AST::FalseLiteral *falseLiteral) override
    {
        if (skipIt())
            return;
        m_rhs = false;
        m_specified = true;
    }

    void throwRecursionDepthError() override{};

    void checkAndResetCal()
    {
        if (--m_depth == 0) {
            m_rhs = CES::MatchedFunction{identifier, fields.join(".")};
            m_specified = true;

            identifier.clear();
            fields.clear();
        }
    }

    void checkAndResetNonCal()
    {
        if (--m_depth == 0) {
            m_rhs = CES::Variable{identifier, fields.join(".")};
            m_specified = true;

            identifier.clear();
            fields.clear();
        }
    }

    bool skipIt() { return m_failed || m_specified; }

    void setFailed() { m_failed = true; }

private:
    bool m_failed = false;
    bool m_specified = false;
    int m_depth = 0;
    QString identifier;
    QStringList fields;
    CES::RightHandSide m_rhs;
};

CES::MatchedStatement checkForStateSet(const CES::MatchedStatement &curState)
{
    using namespace CES;
    return std::visit(Overload{[](const PropertySet &pSet) -> MatchedStatement {
                                   QString exp = pSet.lhs.expression();
                                   if (exp.size() && exp.split(".").last().compare("state") == 0)
                                       return StateSet{exp, CES::toString(pSet.rhs)};
                                   return pSet;
                               },
                               [](const auto &pSet) -> MatchedStatement { return pSet; }},
                      curState);
}

class ConsoleLogEvaluator : public QmlJS::AST::Visitor
{
public:
    bool isValid() { return m_completed; }

    CES::ConsoleLog expression() { return CES::ConsoleLog{m_arg}; };

protected:
    bool preVisit(QmlJS::AST::Node *node) override
    {
        if (m_failed)
            return false;

        switch (m_stepId) {
        case 0: {
            if (node->kind != Kind::Kind_CallExpression)
                m_failed = true;
        } break;
        case 1: {
            if (node->kind != Kind::Kind_FieldMemberExpression)
                m_failed = true;
        } break;
        case 2: {
            if (node->kind != Kind::Kind_IdentifierExpression)
                m_failed = true;
        } break;
        case 3: {
            if (node->kind != Kind::Kind_ArgumentList)
                m_failed = true;
        } break;
        }

        m_stepId++;
        if (m_failed || m_completed)
            return false;

        return true;
    }

    bool visit(QmlJS::AST::IdentifierExpression *identifier) override
    {
        if (m_completed)
            return true;

        if (identifier->name.compare("console")) {
            m_failed = true;
            return false;
        }
        return true;
    }

    bool visit(QmlJS::AST::FieldMemberExpression *fieldExpression) override
    {
        if (m_completed)
            return true;

        if (fieldExpression->name.compare("log")) {
            m_failed = true;
            return false;
        }
        return true;
    }

    bool visit(QmlJS::AST::ArgumentList *arguments) override
    {
        if (m_completed)
            return true;

        if (arguments->next) {
            m_failed = true;
            return false;
        }
        m_completed = true;
        RightHandVisitor rVis;
        arguments->expression->accept(&rVis);
        m_arg = rVis.rhs();
        return true;
    }

    void throwRecursionDepthError() override{};

private:
    bool m_failed = false;
    bool m_completed = false;
    int m_stepId = 0;
    CES::RightHandSide m_arg;
};

struct StatementReply
{
    const TrackingArea area;

    CES::MatchedStatement *operator()(CES::MatchedStatement &handler) const { return &handler; }

    CES::MatchedStatement *operator()(CES::ConditionalStatement &handler) const
    {
        switch (area) {
        case TrackingArea::Ko:
            return &handler.ko;
        case TrackingArea::Ok:
            return &handler.ok;
        case TrackingArea::Condition:
        default:
            return nullptr;
        }
    }
};

} // namespace

class QmlDesigner::ConnectionEditorEvaluatorPrivate
{
    friend class ConnectionEditorEvaluator;
    using Status = ConnectionEditorEvaluator::Status;

public:
    bool checkValidityAndReturn(const bool &valid, const QString &parseError = {});

    void setStatus(Status status) { m_checkStatus = status; }

    NodeStatus parentNodeStatus() const { return nodeStatus(1); }

    NodeStatus nodeStatus(int reverseLevel = 0) const;

    TrackingArea trackingArea() const { return m_trackingArea; }

    void setTrackingArea(bool ifFound, int ifCurrentChildren)
    {
        if (!ifFound) {
            m_trackingArea = TrackingArea::No;
            return;
        }
        switch (ifCurrentChildren) {
        case 1:
            m_trackingArea = TrackingArea::Condition;
            break;
        case 2:
            m_trackingArea = TrackingArea::Ok;
            break;
        case 3:
            m_trackingArea = TrackingArea::Ko;
            break;
        default:
            m_trackingArea = TrackingArea::No;
        }
    }

    int childLevelOfTheClosestItem(Kind kind)
    {
        for (const NodeStatus &status : m_nodeHierarchy | Utils::views::reverse) {
            if (status == kind)
                return status.childId();
        }
        return -1;
    }

    bool isInIfCondition() const { return m_trackingArea == TrackingArea::Condition; }

    CES::MatchedStatement *curStatement()
    {
        return std::visit(StatementReply{m_trackingArea}, m_handler);
    }

    CES::MatchedCondition *currentCondition()
    {
        if (std::holds_alternative<CES::ConditionalStatement>(m_handler))
            return &std::get<CES::ConditionalStatement>(m_handler).condition;
        return nullptr;
    }

    void addVariableCondition(Node *node)
    {
        if (CES::MatchedCondition *currentCondition = this->currentCondition()) {
            if (currentCondition->statements.isEmpty()) {
                RightHandVisitor varVisitor;
                node->accept(&varVisitor);
                if (varVisitor.couldBeVariable())
                    currentCondition->statements.append(varVisitor.variable());
            }
        }
    }

private:
    int m_ifStatement = 0;
    int m_consoleLogCount = 0;
    int m_consoleIdentifierCount = 0;
    bool m_acceptLogArgument = false;
    TrackingArea m_trackingArea = TrackingArea::No;
    QString m_errorString;
    Status m_checkStatus = Status::UnStarted;
    QList<NodeStatus> m_nodeHierarchy;
    CES::Handler m_handler;
};

ConnectionEditorEvaluator::ConnectionEditorEvaluator()
    : d(new ConnectionEditorEvaluatorPrivate)
{}

ConnectionEditorEvaluator::~ConnectionEditorEvaluator()
{
    delete d;
}

ConnectionEditorEvaluator::Status ConnectionEditorEvaluator::status() const
{
    return d->m_checkStatus;
}

CES::Handler ConnectionEditorEvaluator::resultNode() const
{
    return d->m_handler;
}

bool ConnectionEditorEvaluator::preVisit(Node *node)
{
    QString out;
    for (int i = 0; i < d->m_nodeHierarchy.count(); i++)
        out.append("    ");

    int child = 0;
    if (d->m_nodeHierarchy.size()) {
        NodeStatus &parentNode = d->m_nodeHierarchy.last();
        child = parentNode.increaseChildNo();
        if (parentNode == Kind::Kind_IfStatement)
            d->setTrackingArea(true, parentNode.children());
    }

    d->m_nodeHierarchy.append(node);

    out.append(QString::number(child) + ": ");
    out.append(name(node));
    //    qDebug().noquote() << out;
    switch (node->kind) {
    case Kind::Kind_Program:
        return true;
    case Kind::Kind_StatementList:
    case Kind::Kind_IfStatement:
    case Kind::Kind_Block:
    case Kind::Kind_IdentifierExpression:
    case Kind::Kind_BinaryExpression:
    case Kind::Kind_FieldMemberExpression:
    case Kind::Kind_ExpressionStatement:
    case Kind::Kind_Expression:
    case Kind::Kind_CallExpression:
    case Kind::Kind_TrueLiteral:
    case Kind::Kind_FalseLiteral:
    case Kind::Kind_StringLiteral:
    case Kind::Kind_NumericLiteral:
    case Kind::Kind_ArgumentList:
        return status() == UnFinished;
    default:
        return false;
    }
}

void ConnectionEditorEvaluator::postVisit(QmlJS::AST::Node *node)
{
    if (!d->m_nodeHierarchy.size()) {
        d->checkValidityAndReturn(false, "Unexpected post visiting");
        return;
    }
    if (d->m_nodeHierarchy.last() == node->kind) {
        d->m_nodeHierarchy.removeLast();
    } else {
        d->checkValidityAndReturn(false, "Post visiting kind does not match");
        return;
    }

    if (node->kind == Kind::Kind_IfStatement) {
        bool ifFound = false;
        int closestIfChildren = 0;
        for (const NodeStatus &nodeStatus : d->m_nodeHierarchy | Utils::views::reverse) {
            if (nodeStatus == Kind::Kind_IfStatement) {
                ifFound = true;
                closestIfChildren = nodeStatus.children();
                break;
            }
        }
        d->setTrackingArea(ifFound, closestIfChildren);
    }
}

bool ConnectionEditorEvaluator::visit(QmlJS::AST::Program *program)
{
    d->setStatus(UnFinished);
    d->setTrackingArea(false, 0);
    d->m_ifStatement = 0;
    d->m_consoleLogCount = 0;
    d->m_consoleIdentifierCount = 0;
    d->m_handler = CES::EmptyBlock{};
    return true;
}

bool ConnectionEditorEvaluator::visit(QmlJS::AST::StatementList *statementList)
{
    return d->checkValidityAndReturn(true);
}

bool ConnectionEditorEvaluator::visit([[maybe_unused]] QmlJS::AST::IfStatement *ifStatement)
{
    if (d->m_ifStatement++)
        return d->checkValidityAndReturn(false, "Nested if conditions are not supported");

    if (ifStatement->ok->kind != Kind::Kind_Block)
        return d->checkValidityAndReturn(false, "True block should be in a curly bracket.");

    if (ifStatement->ko && ifStatement->ko->kind != Kind::Kind_Block)
        return d->checkValidityAndReturn(false, "False block should be in a curly bracket.");

    d->m_handler = CES::ConditionalStatement{};
    return d->checkValidityAndReturn(true);
}

bool ConnectionEditorEvaluator::visit(QmlJS::AST::IdentifierExpression *identifier)
{
    if (d->parentNodeStatus() == Kind::Kind_FieldMemberExpression)
        if (d->m_consoleLogCount)
            d->m_consoleIdentifierCount++;

    d->addVariableCondition(identifier);

    return d->checkValidityAndReturn(true);
}

bool ConnectionEditorEvaluator::visit(QmlJS::AST::ExpressionStatement *expressionStatement)
{
    return d->checkValidityAndReturn(true);
}

bool ConnectionEditorEvaluator::visit(QmlJS::AST::BinaryExpression *binaryExpression)
{
    if (binaryExpression->left->kind == Kind::Kind_StringLiteral)
        return d->checkValidityAndReturn(false, "Left hand string literal");

    if (binaryExpression->left->kind == Kind::Kind_NumericLiteral)
        return d->checkValidityAndReturn(false, "Left hand numeric literal");

    if (binaryExpression->op == QSOperator::Equal
        && binaryExpression->left->kind == Kind::Kind_FieldMemberExpression
        && binaryExpression->right->kind == Kind::Kind_FieldMemberExpression)
        return d->checkValidityAndReturn(false,
                                         "Use \"===\" for comparing two field member expressions.");

    if (binaryExpression->op == QSOperator::NotEqual
        && binaryExpression->left->kind == Kind::Kind_FieldMemberExpression
        && binaryExpression->right->kind == Kind::Kind_FieldMemberExpression)
        return d->checkValidityAndReturn(false,
                                         "Use \"!==\" for comparing two field member expressions.");

    if (d->isInIfCondition()) {
        if (binaryExpression->op == QSOperator::Assign)
            return d->checkValidityAndReturn(false, "Assignment as a condition.");

        if (!isAcceptedIfBinaryOperator(binaryExpression->op))
            return d->checkValidityAndReturn(false, "Operator is not supported.");

        CES::MatchedCondition *matchedCondition = d->currentCondition();
        if (!matchedCondition)
            return d->checkValidityAndReturn(false, "Matched condition is not available.");

        BoolCondition conditionVisitor;
        binaryExpression->accept(&conditionVisitor);

        if (conditionVisitor.isValid())
            *matchedCondition = conditionVisitor.matchedCondition();
        else
            return d->checkValidityAndReturn(false, "Matched condition is not valid.");

        return false;
    } else {
        CES::MatchedStatement *currentStatement = d->curStatement();
        if (currentStatement && CES::isEmptyStatement(*currentStatement)
            && d->parentNodeStatus().childId() == 0) {
            if (binaryExpression->op == QSOperator::Assign) {
                RightHandVisitor variableVisitor;
                binaryExpression->left->accept(&variableVisitor);

                if (!variableVisitor.couldBeLHS())
                    return d->checkValidityAndReturn(false, "Invalid left hand.");

                CES::BindingProperty lhs = variableVisitor.lhs();

                variableVisitor.reset();
                binaryExpression->right->accept(&variableVisitor);

                if (variableVisitor.couldBeLHS()) {
                    CES::Assignment assignment{lhs, variableVisitor.variable()};
                    *currentStatement = assignment;
                } else if (variableVisitor.isLiteralType()) {
                    CES::PropertySet propSet{lhs, variableVisitor.literal()};
                    *currentStatement = propSet;
                } else {
                    return d->checkValidityAndReturn(false, "Invalid RHS");
                }

                *currentStatement = checkForStateSet(*currentStatement);
            }
        }
    }

    return d->checkValidityAndReturn(true);
}

bool ConnectionEditorEvaluator::visit(QmlJS::AST::FieldMemberExpression *fieldExpression)
{
    if (d->parentNodeStatus() == Kind::Kind_CallExpression)
        if (!fieldExpression->name.compare("log"))
            d->m_consoleLogCount++;

    d->addVariableCondition(fieldExpression);

    return d->checkValidityAndReturn(true);
}

bool ConnectionEditorEvaluator::visit(QmlJS::AST::CallExpression *callExpression)
{
    if (d->isInIfCondition())
        d->checkValidityAndReturn(false, "Functions are not allowd in the expressions");

    CES::MatchedStatement *curStatement = d->curStatement();
    if (!curStatement)
        d->checkValidityAndReturn(false, "Invalid place to call an expression");

    if (CES::isEmptyStatement(*curStatement)) {
        if (d->parentNodeStatus().childId() == 0) {
            ConsoleLogEvaluator logEvaluator;
            callExpression->accept(&logEvaluator);
            if (logEvaluator.isValid()) {
                *curStatement = logEvaluator.expression(); // Console Log
            } else {
                RightHandVisitor callVisitor;

                callExpression->accept(&callVisitor);
                if (callVisitor.isValid()) {
                    CES::RightHandSide rhs = callVisitor.rhs();
                    if (std::holds_alternative<CES::MatchedFunction>(rhs))
                        *curStatement = std::get<CES::MatchedFunction>(rhs);
                    else
                        return d->checkValidityAndReturn(false, "Invalid Matched Function type.");
                } else {
                    return d->checkValidityAndReturn(false, "Invalid Matched Function");
                }
            }
        }
    }
    return d->checkValidityAndReturn(true);
}

bool ConnectionEditorEvaluator::visit(QmlJS::AST::Block *block)
{
    Kind parentKind = d->parentNodeStatus();

    if (parentKind == Kind::Kind_IfStatement) {
        return d->checkValidityAndReturn(true);
    } else if (d->parentNodeStatus() == Kind::Kind_StatementList) {
        if (d->nodeStatus(2) == Kind::Kind_Program)
            return d->checkValidityAndReturn(true);
    }

    return d->checkValidityAndReturn(false, "Block count ptoblem");
}

bool ConnectionEditorEvaluator::visit(QmlJS::AST::ArgumentList *arguments)
{
    if (d->trackingArea() == TrackingArea::Condition)
        return d->checkValidityAndReturn(false, "Arguments are not supported in if condition");

    auto currentStatement = d->curStatement();
    if (!currentStatement)
        return d->checkValidityAndReturn(false, "No statement found for argument");

    if (!CES::isConsoleLog(*currentStatement))
        return d->checkValidityAndReturn(false, "Arguments are only supported for console.log");

    if (d->m_acceptLogArgument && !arguments->next)
        return d->checkValidityAndReturn(true);

    return d->checkValidityAndReturn(false, "The only supported argument is in console.log");
}

void ConnectionEditorEvaluator::endVisit(QmlJS::AST::Program *program)
{
    if (status() == UnFinished)
        d->setStatus(Succeeded);
}

void ConnectionEditorEvaluator::endVisit(QmlJS::AST::FieldMemberExpression *fieldExpression)
{
    if (status() != UnFinished)
        return;

    if (!fieldExpression->name.compare("log")) {
        if (d->m_consoleIdentifierCount != d->m_consoleLogCount) {
            d->m_acceptLogArgument = false;
        } else {
            d->m_consoleIdentifierCount--;
            d->m_acceptLogArgument = true;
        }
        --(d->m_consoleLogCount);
    }
}

void ConnectionEditorEvaluator::endVisit(QmlJS::AST::CallExpression *callExpression)
{
    d->m_acceptLogArgument = false;
}

void ConnectionEditorEvaluator::throwRecursionDepthError()
{
    qDebug() << Q_FUNC_INFO << this;
}

bool ConnectionEditorEvaluatorPrivate::checkValidityAndReturn(const bool &valid,
                                                              const QString &parseError)
{
    if (!valid) {
        if (m_checkStatus != Status::Failed) {
            setStatus(Status::Failed);
            m_errorString = parseError;
            qDebug() << Q_FUNC_INFO << "Error: " << parseError;
        }
    }

    return m_checkStatus;
}

NodeStatus ConnectionEditorEvaluatorPrivate::nodeStatus(int reverseLevel) const
{
    if (m_nodeHierarchy.size() > reverseLevel)
        return m_nodeHierarchy.at(m_nodeHierarchy.size() - reverseLevel - 1);
    return {};
}
