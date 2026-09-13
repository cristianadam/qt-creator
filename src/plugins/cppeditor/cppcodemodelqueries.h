// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppeditor_global.h"
#include "cpprefactoringchanges.h"
#include "insertionpointlocator.h"

#include <utils/filepath.h>
#include <utils/link.h>

#include <QList>
#include <QString>

namespace CPlusPlus { class Snapshot; }

namespace CppEditor {

// Questions about what the code says, asked and answered without a symbol of
// any front end's: a place goes in and places come out.
//
// That is what lets a plugin outside this one read the code model at all --
// the cxx-frontend model is this plugin's own, so a consumer elsewhere cannot
// choose between the two front ends itself. These answer off whichever one
// has the file, the way the locator's place-based overloads already do, and a
// caller is none the wiser.

// Where a class's name stands in its own body, which is what a front end
// records a class at and what the locator is asked with.
struct CPPEDITOR_EXPORT WrittenClass
{
    QString name;              // its own name, with nothing in front of it
    Utils::FilePath filePath;
    int line = 0;              // counted from one
    int column = 0;

    bool isValid() const { return line > 0; }
};

// A member function a class declares: how it reads, and where its own name
// stands -- which is the place the questions below are asked with.
struct CPPEDITOR_EXPORT WrittenFunction
{
    QString name;              // its own name, with nothing in front of it

    // Its name and the types it takes, as they would be written:
    // "f(int, const QString &)". Two front ends do not write a type the same
    // way, so compare these through QMetaObject::normalizedSignature(),
    // which reduces both to the same.
    QString signature;

    Utils::FilePath filePath;
    int line = 0;
    int column = 0;
};

// The class \a filePath -- or a file it includes, at most \a maxIncludeDepth
// deep -- writes that uses \a className: one that declares a member of that
// type, a value or a pointer to it, or one that derives from it.
// \a className is written out in full, as "Ui::Form" is.
//
// The first such class, the file itself before the files it includes.
// Nothing where none of them writes one.
CPPEDITOR_EXPORT WrittenClass classUsingClass(const CPlusPlus::Snapshot &snapshot,
                                              const Utils::FilePath &filePath,
                                              const QString &className,
                                              int maxIncludeDepth);

// The member functions \a klass declares, in the order it declares them.
CPPEDITOR_EXPORT QList<WrittenFunction> memberFunctionsOf(const CPlusPlus::Snapshot &snapshot,
                                                          const WrittenClass &klass);

// What the locator needs of the function whose own name stands at \a line and
// \a column of \a filePath. Nothing where no function is declared there.
CPPEDITOR_EXPORT DeclarationToDefine declarationToDefineAt(const CppRefactoringChanges &changes,
                                                           const Utils::FilePath &filePath,
                                                           int line, int column);

// Where the project defines the function declared at \a line and \a column of
// \a filePath -- where its own name stands, again -- or nothing where no file
// defines it.
CPPEDITOR_EXPORT Utils::Link definitionOfFunctionAt(const CPlusPlus::Snapshot &snapshot,
                                                    const Utils::FilePath &filePath,
                                                    int line, int column);

} // namespace CppEditor
