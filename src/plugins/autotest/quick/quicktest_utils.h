// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/filepath.h>

#include <QHash>

namespace Autotest {

class ITestFramework;

namespace Internal::QuickTestUtils {

bool isQuickTestMacro(const QByteArray &macro);

// The macros a QML test's main() is written with, for asking a file which
// of them it uses.
QStringList macroNames();
QHash<Utils::FilePath, Utils::FilePath> proFilesForQmlFiles(ITestFramework *framework,
                                                            const QSet<Utils::FilePath> &files);

} // namespace Internal::QuickTestUtils
} // namespace Autotest
