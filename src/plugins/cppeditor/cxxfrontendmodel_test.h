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
    void testFindsTheDefinitionInAnotherFile();
    void testFollowsADeclarationToItsDefinitionElsewhere();
    void testFollowsAFreeFunctionToItsDefinitionElsewhere();
    void testTheClassesAFileDeclares();
    void testTheFunctionsAGeneratedHeaderDeclares();
    void testWhichThingANameMeans();
    void testWhatAFileIncludes();
    void testWhichFilesIncludeAHeaderNamed();
    void testWhereWhatADeclarationStandsForIsDefined();
    void testWhereAFunctionIsDefined();
    void testTheFunctionAPlaceIsInside();
    void testTheFunctionANameStandsFor();
    void testAClassPrivateSlotsAndBases();
    void testTheClassesAFileHandsToARunner();
    void testTheCallsWithALiteral();
    void testTheMacroUsesOfAFile();
    void testFindsTheDeclarationOfADefinition();
    void testNoCounterpartWhereThereIsNone();
    void testDeclinesANameFromAUsingDeclaration();
    void testLocalUses_data();
    void testLocalUses();
    void testOutline_data();
    void testOutline();
    void testIcons_data();
    void testIcons();
    void testNames_data();
    void testNames();
    void testNamesAcrossFiles();
    void testHighlightingReachesTheEditor();
};

} // namespace CppEditor::Internal
