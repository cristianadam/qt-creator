// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pythonproject.h"

#include "pythonbuildsystem.h"
#include "pythonconstants.h"

#include <coreplugin/icontext.h>

#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/target.h>

using namespace Core;
using namespace ProjectExplorer;
using namespace Utils;

namespace Python::Internal {

PythonProject::PythonProject(const FilePath &fileName)
    : Project(Constants::C_PY_PROJECT_MIME_TYPE_TOML, fileName)
{
    setType(PythonProjectId);
    setProjectLanguages(Context(ProjectExplorer::Constants::PYTHON_LANGUAGE_ID));
    setDisplayName(fileName.completeBaseName());
    setBuildSystemCreator<PythonBuildSystem>("python");
}

PythonProjectNode::PythonProjectNode(const FilePath &path)
    : ProjectNode(path)
{
    setDisplayName(path.completeBaseName());
    setAddFileFilter("*.py");
}

PythonFileNode::PythonFileNode(const FilePath &filePath,
                               const QString &nodeDisplayName,
                               FileType fileType)
    : FileNode(filePath, fileType)
    , m_displayName(nodeDisplayName)
{}

QString PythonFileNode::displayName() const
{
    return m_displayName;
}

} // namespace Python::Internal
