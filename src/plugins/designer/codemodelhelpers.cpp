// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "codemodelhelpers.h"
#include "designertr.h"

#include <cppeditor/cppcodemodelqueries.h>
#include <cppeditor/cppmodelmanager.h>

#include <projectexplorer/buildsystem.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/target.h>

#include <QCoreApplication>
#include <QDebug>

// Debug helpers for code model. @todo: Move to some CppEditor library?

using namespace ProjectExplorer;
using namespace Utils;

static const char setupUiC[] = "setupUi";

// Find the generated "ui_form.h" header of the form via project.
static FilePath generatedHeaderOf(const FilePath &uiFileName)
{
    if (BuildSystem *bs = activeBuildSystem(ProjectManager::projectForFile(uiFileName))) {
        FilePaths files = bs->filesGeneratedFrom(uiFileName);
        if (!files.isEmpty()) // There should be at most one header generated from a .ui
            return files.front();
    }
    return {};
}

// How many functions of a name a file declares. What says whether a header
// uic generated is the one it claims to be: uic writes setupUi as a member of
// the Ui_ class it generates, and writes it once.
static int functionsNamed(const CppEditor::CodeModelQueries &code,
                          const FilePath &filePath, const QString &name)
{
    int found = 0;
    for (const CppEditor::WrittenClass &klass : code.classesDeclaredIn(filePath)) {
        for (const CppEditor::WrittenFunction &member : code.memberFunctionsOf(klass)) {
            if (member.name == name)
                ++found;
        }
    }
    return found;
}

namespace Designer::Internal {

// Goto slot invoked by the designer context menu. Either navigates
// to an existing slot function or create a new one.
bool navigateToSlot(const QString &uiFileName,
                    const QString & /* objectName */,
                    const QString & /* signalSignature */,
                    const QStringList & /* parameterNames */,
                    QString *errorMessage)
{

    // Find the generated header.
    const FilePath generatedHeaderFile = generatedHeaderOf(FilePath::fromString(uiFileName));
    if (generatedHeaderFile.isEmpty()) {
        *errorMessage = Tr::tr("The generated header of the form \"%1\" could not be found.\nRebuilding the project might help.").arg(uiFileName);
        return false;
    }
    const CPlusPlus::Snapshot snapshot = CppEditor::CppModelManager::snapshot();
    if (!snapshot.contains(generatedHeaderFile)) {
        *errorMessage = Tr::tr("The generated header \"%1\" could not be found in the code model.\nRebuilding the project might help.").arg(generatedHeaderFile.toUserOutput());
        return false;
    }

    // Look for setupUi
    const CppEditor::CodeModelQueries code(snapshot,
                                           CppEditor::CppModelManager::workingCopy());
    if (functionsNamed(code, generatedHeaderFile, QLatin1String(setupUiC)) != 1) {
        *errorMessage = QString::fromLatin1(
                            "Internal error: The function \"%1\" could not be found in %2")
                            .arg(QLatin1String(setupUiC), generatedHeaderFile.toUserOutput());
        return false;
    }
    return true;
}

} // namespace Designer::Internal
