// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cmake_global.h"

#include <utils/filepath.h>
#include <utils/result.h>

#include <QList>
#include <QString>
#include <QStringList>

namespace CMakeProjectManager {

class CMakeFunctionCall
{
public:
    QString name;
    QStringList arguments;
    int line = -1;
    int lineEnd = -1;
};

class CMAKE_EXPORT CMakeListFile
{
public:
    QString content;
    QList<CMakeFunctionCall> functions;
    QList<CMakeFunctionCall> functionsNamed(const QString &lowerCaseName) const;
};

CMAKE_EXPORT Utils::Result<CMakeListFile> parseCMakeText(const QString &content,
                                                      const QString &fileName = {});

CMAKE_EXPORT Utils::Result<CMakeListFile> parseCMakeFile(const Utils::FilePath &filePaht);

#ifdef WITH_TESTS
namespace Internal { QObject *createCMakeParserTest(); }
#endif

}   // namespace CMakeProjectManager
