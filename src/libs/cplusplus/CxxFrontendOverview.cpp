// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "CxxFrontendOverview.h"

namespace CPlusPlus {

QStringList CxxFrontendOverview::unsupported()
{
    // Overview's knobs that CxxFrontendDocument does not answer to yet.
    // Everything here is a way Qt Creator formats a symbol that the
    // cxx-frontend printer has no notion of, so each one is a piece of
    // TypePrettyPrinter to bring across.
    return {
        "showArgumentNames",
        "showDefaultArguments",
        "showTemplateParameters",
        "showEnclosingTemplate",
        "includeWhiteSpaceInOperatorName",
        "trailingReturnType",
        "combineAutoAndName",
        "markedArgument",
        "BindToLeftSpecifier",
        "BindToRightSpecifier",
    };
}

} // namespace CPlusPlus
