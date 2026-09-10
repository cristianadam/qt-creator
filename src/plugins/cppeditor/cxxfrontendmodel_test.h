// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>

namespace CppEditor::Internal {

class CxxFrontendModelTest : public QObject
{
    Q_OBJECT

private slots:
    void testRunsOverAFileAndItsIncludes();
    void testReadsWhatIsBeingTyped();
    void testResolvesANameDeclaredInAnInclude();
    void testTakesTheProjectsDefines();
    void testWithoutTheProjectsDefines();
    void testFollowsANameToItsDeclaration();
    void testFollowsNothingItCannotAnswerFor();
    void testDeclinesAForwardDeclaration();
    void testDeclinesANameFromAUsingDeclaration();
    void testLocalUses_data();
    void testLocalUses();
    void testOutline_data();
    void testOutline();
};

} // namespace CppEditor::Internal
