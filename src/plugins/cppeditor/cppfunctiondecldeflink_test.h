// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>

namespace CppEditor::Internal::Tests {

class DeclDefLinkTest : public QObject
{
    Q_OBJECT

private slots:
    void testSyncsTheOtherSide_data();
    void testSyncsTheOtherSide();

    void testSyncsTheDeclaration();
    void testNoChangesWhereTheSignaturesAgree();
    void testNoLinkOffAFunction();
};

} // namespace CppEditor::Internal::Tests
