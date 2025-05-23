// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "moveobjectvisitor.h"

#include <qmljs/parser/qmljsast_p.h>

#include <QDebug>

using namespace QmlJS;
using namespace QmlJS::AST;

namespace QmlDesigner {
namespace Internal {

class Inserter: public QMLRewriter
{
public:
    Inserter(TextModifier &modifier,
             quint32 targetParentObjectLocation,
             PropertyNameView targetPropertyName,
             bool targetIsArrayBinding,
             TextModifier::MoveInfo moveInfo,
             Utils::span<const PropertyNameView> propertyOrder)
        : QMLRewriter(modifier)
        , targetParentObjectLocation(targetParentObjectLocation)
        , targetPropertyName(targetPropertyName)
        , targetIsArrayBinding(targetIsArrayBinding)
        , moveInfo(moveInfo)
        , propertyOrder(propertyOrder)
    {}

protected:
    bool visit(UiObjectBinding *ast) override
    {
        if (didRewriting())
            return false;

        if (ast->qualifiedTypeNameId->identifierToken.offset == targetParentObjectLocation)
            insertInto(ast->initializer);

        return !didRewriting();
    }

    bool visit(UiObjectDefinition *ast) override
    {
        if (didRewriting())
            return false;

        if (ast->firstSourceLocation().offset == targetParentObjectLocation)
            insertInto(ast->initializer);

        return !didRewriting();
    }

private:
    void insertInto(UiObjectInitializer *ast)
    {
        if (targetPropertyName.isEmpty()) {
            // insert as UiObjectDefinition:
            UiObjectMemberList *insertAfter = searchMemberToInsertAfter(ast->members, propertyOrder);

            if (insertAfter && insertAfter->member) {
                moveInfo.destination = insertAfter->member->lastSourceLocation().end();
                moveInfo.prefixToInsert = QStringLiteral("\n\n");
            } else {
                moveInfo.destination = ast->lbraceToken.end();
                moveInfo.prefixToInsert = QStringLiteral("\n");
            }

            move(moveInfo);

            setDidRewriting(true);
            return;
        }

        // see if we need to insert into an UiArrayBinding:
        for (UiObjectMemberList *iter = ast->members; iter; iter = iter->next) {
            UiObjectMember *member = iter->member;

            if (auto arrayBinding = cast<UiArrayBinding*>(member)) {
                if (toString(arrayBinding->qualifiedId) == QString::fromUtf8(targetPropertyName)) {
                    appendToArray(arrayBinding);

                    setDidRewriting(true);
                    return;
                }
            }
        }

        { // insert (create) a UiObjectBinding:
            UiObjectMemberList *insertAfter = searchMemberToInsertAfter(ast->members, targetPropertyName, propertyOrder);
            moveInfo.prefixToInsert = QStringLiteral("\n") + QString::fromUtf8(targetPropertyName) + (targetIsArrayBinding ? QStringLiteral(": [") : QStringLiteral(": "));
            moveInfo.suffixToInsert = targetIsArrayBinding ? QStringLiteral("\n]") : QStringLiteral("");

            if (insertAfter && insertAfter->member)
                moveInfo.destination = insertAfter->member->lastSourceLocation().end();
            else
                moveInfo.destination = ast->lbraceToken.end();

            move(moveInfo);

            setDidRewriting(true);
        }
    }

    void appendToArray(UiArrayBinding *ast)
    {
        UiObjectMember *lastMember = nullptr;

        for (UiArrayMemberList *iter = ast->members; iter; iter = iter->next) {
            if (iter->member)
                lastMember = iter->member;
        }

        if (!lastMember)
            Q_ASSERT(!"Invalid QML: empty array found.");

        moveInfo.destination = lastMember->lastSourceLocation().end();
        moveInfo.prefixToInsert = QStringLiteral(",\n");
        moveInfo.suffixToInsert = QStringLiteral("\n");
        move(moveInfo);
    }

private:
    quint32 targetParentObjectLocation;
    PropertyNameView targetPropertyName;
    bool targetIsArrayBinding;
    TextModifier::MoveInfo moveInfo;
    Utils::span<const PropertyNameView> propertyOrder;
};

MoveObjectVisitor::MoveObjectVisitor(TextModifier &modifier,
                                     quint32 objectLocation,
                                     PropertyNameView targetPropertyName,
                                     bool targetIsArrayBinding,
                                     quint32 targetParentObjectLocation,
                                     Utils::span<const PropertyNameView> propertyOrder)
    : QMLRewriter(modifier)
    , objectLocation(objectLocation)
    , targetPropertyName(targetPropertyName)
    , targetIsArrayBinding(targetIsArrayBinding)
    , targetParentObjectLocation(targetParentObjectLocation)
    , propertyOrder(propertyOrder)
{
}

bool MoveObjectVisitor::operator()(UiProgram *ast)
{
    program = ast;

    return QMLRewriter::operator()(ast);
}

bool MoveObjectVisitor::visit(UiArrayBinding *ast)
{
    if (didRewriting())
        return false;

    UiArrayMemberList *currentMember = nullptr;

    for (UiArrayMemberList *it = ast->members; it; it = it->next) {
        if (it->member->firstSourceLocation().offset == objectLocation) {
            currentMember = it;
            break;
        }
    }

    if (currentMember) {
        TextModifier::MoveInfo moveInfo;
        moveInfo.objectStart = currentMember->member->firstSourceLocation().begin();
        moveInfo.objectEnd = currentMember->member->lastSourceLocation().end();

        if (currentMember == ast->members && !currentMember->next) {
            // array with 1 element, which is moved away, so also remove the array binding
            int start = ast->firstSourceLocation().offset;
            int end = ast->lastSourceLocation().end();
            includeSurroundingWhitespace(start, end);
            moveInfo.leadingCharsToRemove = objectLocation - start;
            moveInfo.trailingCharsToRemove = end - moveInfo.objectEnd;
        } else if (currentMember->next) {
            // we're not the last element, so remove the trailing comma too
            Q_ASSERT(currentMember->next->commaToken.isValid());

            int start = objectLocation;
            int end = currentMember->next->commaToken.end();
            includeSurroundingWhitespace(start, end);
            moveInfo.leadingCharsToRemove = objectLocation - start;
            moveInfo.trailingCharsToRemove = end - moveInfo.objectEnd;
        } else {
            // we are the last member, but not the only member, so remove the preceding comma, too
            Q_ASSERT(currentMember->commaToken.isValid());

            int start = currentMember->commaToken.offset;
            int end = moveInfo.objectEnd;
            includeSurroundingWhitespace(start, end);
            moveInfo.leadingCharsToRemove = objectLocation - start;
            moveInfo.trailingCharsToRemove = end - moveInfo.objectEnd;
        }

        doMove(moveInfo);
    }

    return !didRewriting();
}

bool MoveObjectVisitor::visit(UiObjectBinding *ast)
{
    if (didRewriting())
        return false;

    if (ast->qualifiedTypeNameId->identifierToken.offset == objectLocation) {
        TextModifier::MoveInfo moveInfo;
        moveInfo.objectStart = objectLocation;
        moveInfo.objectEnd = ast->lastSourceLocation().end();

        // remove leading indentation and property name:
        int start = ast->firstSourceLocation().offset;
        int end = moveInfo.objectEnd;
        includeSurroundingWhitespace(start, end);
        includeLeadingEmptyLine(start);
        moveInfo.leadingCharsToRemove = objectLocation - start;

        // remove trailing indentation
        moveInfo.trailingCharsToRemove = end - moveInfo.objectEnd;

        doMove(moveInfo);
    }

    return !didRewriting();
}

bool MoveObjectVisitor::visit(UiObjectDefinition *ast)
{
    if (didRewriting())
        return false;

    if (ast->qualifiedTypeNameId->identifierToken.offset == objectLocation) {
        TextModifier::MoveInfo moveInfo;
        moveInfo.objectStart = objectLocation;
        moveInfo.objectEnd = ast->lastSourceLocation().end();

        // remove leading indentation:
        int start = objectLocation;
        int end = moveInfo.objectEnd;
        includeSurroundingWhitespace(start, end);
        includeLeadingEmptyLine(start);
        moveInfo.leadingCharsToRemove = objectLocation - start;
        moveInfo.trailingCharsToRemove = end - moveInfo.objectEnd;

        doMove(moveInfo);
    }

    return !didRewriting();
}

void MoveObjectVisitor::doMove(const TextModifier::MoveInfo &moveInfo)
{
    if (moveInfo.objectEnd > moveInfo.objectStart) {
        Inserter findTargetAndInsert(*textModifier(),
                                     targetParentObjectLocation,
                                     targetPropertyName,
                                     targetIsArrayBinding,
                                     moveInfo,
                                     propertyOrder);
        setDidRewriting(findTargetAndInsert(program));
    }
}

} // namespace Internal
} // namespace QmlDesigner
