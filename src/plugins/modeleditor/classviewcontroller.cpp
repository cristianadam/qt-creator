// Copyright (C) 2016 Jochen Becher
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "classviewcontroller.h"

#include <cppeditor/cppcodemodelqueries.h>
#include <cppeditor/cppmodelmanager.h>
#include <cppeditor/cpptoolsreuse.h>

using namespace Utils;
namespace ModelEditor::Internal {

ClassViewController::ClassViewController(QObject *parent)
    : QObject(parent)
{
}

QSet<QString> ClassViewController::findClassDeclarations(const FilePath &filePath, int line, int column)
{
    // Asked of the code model in places, so whichever front end has the
    // file is the one that answers.
    const CppEditor::CodeModelQueries code(CppEditor::CppModelManager::snapshot(),
                                           CppEditor::CppModelManager::workingCopy());

    QSet<QString> classNames;
    appendClassDeclarationsFrom(code, filePath, line, column, &classNames);

    // Asked about a file rather than about one class in it, the file that
    // goes with this one declares some too.
    if (line <= 0) {
        appendClassDeclarationsFrom(code, CppEditor::correspondingHeaderOrSource(filePath),
                                    -1, -1, &classNames);
    }
    return classNames;
}

void ClassViewController::appendClassDeclarationsFrom(const CppEditor::CodeModelQueries &code,
                                                      const FilePath &filePath,
                                                      int line, int column,
                                                      QSet<QString> *classNames)
{
    for (const CppEditor::WrittenClass &klass : code.classesDeclaredIn(filePath)) {
        // Where a place was given, the one class whose name stands there.
        if (line > 0 && (klass.line != line || klass.column != column + 1))
            continue;
        // Ignore private class created by Q_OBJECT macro
        if (!klass.qualifiedName.endsWith("::QPrivateSignal"))
            classNames->insert(klass.qualifiedName);
    }
}

} // namespace ModelEditor::Internal
