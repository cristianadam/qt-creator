// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QString>
#include <QStringList>

namespace Autotest::Internal::GTestUtils {

bool isGTestMacro(const QString &macro);

// The macros a test is written with, for asking a file which of them it uses.
QStringList macroNames();
bool isGTestParameterized(const QString &macro);
bool isGTestTyped(const QString &macro);
bool isValidGTestFilter(const QString &filterExpression);

} // namespace Autotest::Internal::GTestUtils
