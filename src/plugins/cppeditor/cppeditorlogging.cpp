// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppeditorlogging.h"

namespace CppEditor::Internal {

Q_LOGGING_CATEGORY(highlighterLog, "qtc.cppeditor.syntaxhighlighter", QtWarningMsg)

// At info, because which scanner is producing the editor's tokens is the one
// thing worth being able to read back when comparing the two.
Q_LOGGING_CATEGORY(cxxFrontendLog, "qtc.cppeditor.cxxfrontend", QtInfoMsg)

} // namespace CppEditor::Internal
