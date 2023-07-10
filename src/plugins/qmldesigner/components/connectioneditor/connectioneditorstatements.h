// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <qmljs/parser/qmljsast_p.h>

namespace QmlDesigner {
namespace CES {
using Literal = std::variant<bool, double, QString>;

struct BindingProperty
{
    QString expression() const { return m_expression; }

    void setExpression(const QString &expression) { m_expression = expression; }

    QString m_expression;
};

struct Variable
{ //Only one level for now (.)
    QString nodeId;
    QString propertyName;
};

struct MatchedFunction
{ //First item before "." is considered node
    QString nodeId;
    QString functionName;
};

using ComparativeStatement = std::variant<bool, double, QString, Variable>;
using RightHandSide = std::variant<bool, double, QString, Variable, MatchedFunction>;

struct Assignment
{
    BindingProperty lhs; //There always should be a binding property on the left hand side. The first identifier is considered node
    Variable rhs; //Similar to function but no function call. Just regular FieldExpression/binding
};

struct PropertySet
{
    BindingProperty lhs; //There always should be a binding property on the left hand side. The first identifier is considered node
    Literal rhs;
};

struct StateSet
{
    QString nodeId;
    QString stateName;
};

typedef std::monostate EmptyBlock;

struct ConsoleLog
{
    RightHandSide argument;
};

using MatchedStatement = std::variant<MatchedFunction, Assignment, PropertySet, StateSet, ConsoleLog, EmptyBlock>;

enum class ConditionToken {
    Unknown,
    Not,
    And,
    Or,
    LargerThan,
    LargerEqualsThan,
    SmallerThan,
    SmallerEqualsThan,
    Equals
};

struct MatchedCondition
{
    QList<ConditionToken> tokens;
    QList<ComparativeStatement> statements;
};

struct ConditionalStatement
{
    MatchedStatement ok = EmptyBlock{};
    MatchedStatement ko = EmptyBlock{}; //else statement
    MatchedCondition condition;
};

using Handler = std::variant<MatchedStatement, ConditionalStatement>;

extern bool isEmptyStatement(const MatchedStatement &stat);
extern QString toString(const ComparativeStatement &stat);
extern QString toString(const RightHandSide &rhs);
extern QString toString(const Literal &literal);
extern QString toString(const MatchedStatement &statement);
extern QString toString(const Handler &handler);

extern bool isConsoleLog(const MatchedStatement &curState);
extern bool isLiteralType(const RightHandSide &var);
} // namespace CES

} // namespace QmlDesigner
