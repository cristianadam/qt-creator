// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qttest_utils.h"
#include "qttesttreeitem.h"

#include <cplusplus/ASTVisitor.h>
#include <cplusplus/CppDocument.h>
#include <cplusplus/Overview.h>
#include <cplusplus/Scope.h>

#include <QMap>
#include <QSet>

namespace Autotest::Internal {

class TestDataFunctionVisitor : public CPlusPlus::ASTVisitor
{
public:
    explicit TestDataFunctionVisitor(CPlusPlus::Document::Ptr doc);

    bool visit(CPlusPlus::UsingDirectiveAST *ast) override;
    bool visit(CPlusPlus::FunctionDefinitionAST *ast) override;
    bool visit(CPlusPlus::CallAST *ast) override;
    bool preVisit(CPlusPlus::AST *ast) override;
    void postVisit(CPlusPlus::AST *ast) override;
    QHash<QString, QtTestCodeLocationList> dataTags() const { return m_dataTags; }

private:
    QString extractNameFromAST(CPlusPlus::StringLiteralAST *ast, bool *ok) const;
    bool newRowCallFound(CPlusPlus::CallAST *ast, unsigned *firstToken) const;

    CPlusPlus::Document::Ptr m_currentDoc;
    CPlusPlus::Overview m_overview;
    QString m_currentFunction;
    QHash<QString, QtTestCodeLocationList> m_dataTags;
    QtTestCodeLocationList m_currentTags;
    unsigned m_currentAstDepth = 0;
    unsigned m_insideUsingQTestDepth = 0;
    bool m_insideUsingQTest = false;
};

} // namespace Autotest::Internal
