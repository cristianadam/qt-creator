// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
#include "connectioneditorstatements.h"

using namespace QmlDesigner;
using namespace CES;

namespace {
template<typename... Ts>
struct Overload : Ts...
{
    using Ts::operator()...;
};
template<class... Ts>
Overload(Ts...) -> Overload<Ts...>;

struct StringVisitor
{
    QString operator()(const bool &bVal) const { return bVal ? QString("true") : QString("false"); }

    QString operator()(const double &dVal) const { return QString::number(dVal); }

    QString operator()(const QString &str) const { return "\"" + str + "\""; }

    QString operator()(const Variable &var)
    {
        QString propertyName;
        if (var.propertyName.size())
            propertyName = ".";
        propertyName.append(var.propertyName);
        return "Variable{" + var.nodeId + propertyName + "}";
    }

    QString operator()(const CES::MatchedFunction &func)
    {
        return "MatchedFunction{" + func.nodeId + "." + func.functionName + "}";
    }

    QString operator()(const CES::Assignment &assignment)
    {
        return "Assignment{" + assignment.lhs.expression() + " = " + StringVisitor()(assignment.rhs)
               + "}";
    }

    QString operator()(const CES::PropertySet &propertySet)
    {
        return "PropertySet{" + propertySet.lhs.expression() + " = "
               + std::visit(StringVisitor{}, propertySet.rhs) + "}";
    }

    QString operator()(const CES::StateSet &stateSet)
    {
        return "StateSet{" + stateSet.nodeId + " = " + stateSet.stateName + "}";
    }

    QString operator()(const CES::EmptyBlock &) { return "EmptyBlock{}"; }

    QString operator()(const CES::ConsoleLog &consoleLog)
    {
        return "ConsoleLog{" + std::visit(StringVisitor{}, consoleLog.argument) + "}";
    }

    QString operator()(const ConditionToken &token)
    {
        static const QHash<ConditionToken, QString> tokenStrings = {
            {ConditionToken::Not, "Not"},
            {ConditionToken::And, "And"},
            {ConditionToken::Or, "Or"},
            {ConditionToken::LargerThan, "LargerThan"},
            {ConditionToken::LargerEqualsThan, "LargerEuqalsThan"},
            {ConditionToken::SmallerThan, "SmallerThan"},
            {ConditionToken::SmallerEqualsThan, "SmallerEqualsThan"},
            {ConditionToken::Equals, "Equals"}};
        return tokenStrings.value(token, "Unknown");
    }

    QString operator()(const CES::MatchedCondition &matched)
    {
        if (!matched.statements.size() && !matched.tokens.size())
            return "MatchedCondition{}";

        if (matched.statements.size() != matched.tokens.size() + 1)
            return "MatchedCondition{Invalid}";

        QString value = "MatchedCondition{";
        int i = 0;
        for (i = 0; i < matched.tokens.size(); i++) {
            const ComparativeStatement &statement = matched.statements[i];
            const ConditionToken &token = matched.tokens[i];
            value += std::visit(StringVisitor{}, statement) + " ";
            value += StringVisitor()(token) + " ";
        }
        value += std::visit(StringVisitor{}, matched.statements[i]);
        value += "}";
        return value;
    }

    QString operator()(const CES::ConditionalStatement &conditional)
    {
        QString value = "IF (";
        value += StringVisitor()(conditional.condition);
        value += ") {\n";
        value += std::visit(StringVisitor{}, conditional.ok);
        if (!std::holds_alternative<EmptyBlock>(conditional.ko)) {
            value += "\n} ELSE {\n";
            value += std::visit(StringVisitor{}, conditional.ko);
        }
        value += "\n}";

        return value;
    }

    QString operator()(const CES::MatchedStatement &conditional)
    {
        return std::visit(StringVisitor{}, conditional);
    }
};
} // namespace

bool CES::isEmptyStatement(const MatchedStatement &stat)
{
    return std::holds_alternative<EmptyBlock>(stat);
}

QString CES::toString(const ComparativeStatement &stat)
{
    return std::visit(StringVisitor{}, stat);
}

QString CES::toString(const RightHandSide &rhs)
{
    return std::visit(StringVisitor{}, rhs);
}

QString CES::toString(const Literal &literal)
{
    return std::visit(StringVisitor{}, literal);
}

QString CES::toString(const MatchedStatement &statement)
{
    return std::visit(StringVisitor{}, statement);
}

QString CES::toString(const Handler &handler)
{
    return std::visit(StringVisitor{}, handler);
}

bool CES::isConsoleLog(const MatchedStatement &curState)
{
    return std::holds_alternative<ConsoleLog>(curState);
}

bool CES::isLiteralType(const RightHandSide &var)
{
    return std::visit(Overload{[](const double &) { return true; },
                               [](const bool &) { return true; },
                               [](const QString &) { return true; },
                               [](const auto &) { return false; }},
                      var);
}
