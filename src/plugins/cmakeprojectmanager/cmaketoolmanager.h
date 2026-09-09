// Copyright (C) 2016 Canonical Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cmake_global.h"

#include "cmaketool.h"

#include <utils/filepath.h>
#include <utils/id.h>

#include <QObject>

namespace ProjectExplorer {
class Project;
}

namespace CMakeProjectManager {

class CMakeKeywords;

class CMAKE_EXPORT CMakeToolManager : public QObject
{
public:
    CMakeToolManager();
    ~CMakeToolManager();

    static CMakeKeywords defaultProjectOrDefaultCMakeKeyWords();

    static CMakeTool *defaultCMakeTool();
    static CMakeTool *cmakeToolForPath(const Utils::FilePath &executable);
    static Utils::FilePath executableForId(const Utils::Id id);

    static void migrateLegacyTools();

    static void updateDocumentation();

    static QString toolTipForRstHelpFile(const Utils::FilePath &helpFile);

    static Utils::FilePath mappedFilePath(ProjectExplorer::Project *project, const Utils::FilePath &path);
};

namespace Internal { void setupCMakeToolManager(QObject *guard); }

} // namespace CMakeProjectManager

Q_DECLARE_METATYPE(QString *)
