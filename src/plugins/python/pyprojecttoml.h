// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <3rdparty/toml11/toml.hpp>
#include <projectexplorer/buildsystem.h>

namespace Python::Internal {

struct PyProjectTomlParseError
{
    QString text;
    int line = -1;
};

struct PyProjectTomlParseResult
{
    QList<PyProjectTomlParseError> errors;
    QString projectName;
    QString projectVersion;
    QStringList projectFiles;

    void addError(const QString &text, int line = -1);
};

template<typename ExpectedType>
Utils::expected_str<ExpectedType> getNodeByKey(
    const std::string expected_type_name,
    const std::string tableName,
    const toml::ordered_value &table,
    const std::string key);

template<typename ExpectedType>
Utils::expected_str<ExpectedType> getNodeValue(
    const std::string expected_type_name,
    const std::string nodeName,
    const toml::ordered_value &node);

PyProjectTomlParseResult parsePyProjectToml(Utils::FilePath pyProjectTomlPath);

Utils::expected_str<QString> updatedPyProjectTomlContents(
    const Utils::FilePath &pyProjectTomlPath, const QStringList &projectFiles);

} // namespace Python::Internal
