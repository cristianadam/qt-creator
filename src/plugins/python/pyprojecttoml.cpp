// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pyprojecttoml.h"
#include "pythontr.h"

#include <3rdparty/toml11/toml.hpp>

#include <QString>
#include <QStringList>

namespace Python::Internal {

/*!
    \brief Appends a new error to the error list
    \param text The error text
    \param line The line number. Can be -1 if unknown
*/
void PyProjectTomlParseResult::addError(const QString &text, int line)
{
    errors.append({text, line});
}

/*!
    \brief Returns the value associated with a given key from a TOML table node, if it exists.
    The value can be a TOML table, array or primitive value.
    \tparam ExpectedType The type of the value to fetch. Must be a TOML value type.
    \param expected_type_name The name of the expected type
    \param tableName The name of the table to fetch the value from
    \param table The table to fetch the value from
    \param key The key to fetch the value from. May be a dotted key.
    \return The value if found, otherwise an error string
    \note The \a expected_type_name and \a tableName parameters are only used for error reporting
*/
template<typename ExpectedType>
Utils::expected_str<ExpectedType> getNodeByKey(
    const std::string expected_type_name,
    const std::string tableName,
    const toml::ordered_value &table,
    const std::string key)
{
    if (!table.is_table()) {
        return Utils::make_unexpected(
            Tr::tr("Type error: \"%1\" must be a table, not a \"%2\"")
                .arg(QString::fromStdString(tableName))
                .arg(QString::fromStdString(toml::to_string(table.type()))));
    }
    try {
        const auto &node = toml::find(table, key);
        return getNodeValue<ExpectedType>(expected_type_name, key, node);
    } catch (const std::out_of_range &) {
        return Utils::make_unexpected(
            Tr::tr("Missing key error: \"%1\" table must contain a \"%2\" node")
                .arg(QString::fromStdString(tableName))
                .arg(QString::fromStdString(key)));
    }
}

/*!
    \brief Fetches a value of certain type from a TOML node by key if existing

    \tparam ExpectedType The type of the value to fetch. Must be a TOML primitive value type.
    \param expected_type_name The name of the expected type
    \param nodeName The name of the node to fetch the value from
    \param node The node to fetch the value from

    \return The value if found, otherwise an error string

    \note The \a expected_type_name and \a nodeName parameters are only used for error reporting
*/
template<typename ExpectedType>
Utils::expected_str<ExpectedType> getNodeValue(
    const std::string expected_type_name,
    const std::string nodeName,
    const toml::ordered_value &node)
{
    if (node.is_empty())
        return Utils::make_unexpected(
            Tr::tr("Node \"%1\" is empty").arg(QString::fromStdString(nodeName)));
    try {
        if constexpr (std::is_same_v<ExpectedType, toml::table>) {
            if (!node.is_table())
                return Utils::make_unexpected(
                    Tr::tr("Type error: Node \"%1\" must be a \"TOML table\"")
                        .arg(QString::fromStdString(nodeName)));
            return node.as_table();
        } else if constexpr (std::is_same_v<ExpectedType, toml::array>) {
            if (!node.is_array())
                return Utils::make_unexpected(
                    Tr::tr("Type error: Node \"%1\" must be a \"TOML array\"")
                        .arg(QString::fromStdString(nodeName)));
            return node.as_array();
        }
        return toml::get<ExpectedType>(node);
    } catch (const toml::type_error &) {
        return Utils::make_unexpected(
            Tr::tr("Type error: Node \"%1\" value type must be a \"%2\". Type found: \"%3\"")
                .arg(QString::fromStdString(nodeName))
                .arg(QString::fromStdString(expected_type_name))
                .arg(QString::fromStdString(toml::to_string(node.type()))));
    }
}

PyProjectTomlParseResult parsePyProjectToml(Utils::FilePath pyProjectTomlPath)
{
    PyProjectTomlParseResult result;
    if (!pyProjectTomlPath.exists()) {
        result.addError(Tr::tr("File \"%1\" does not exist").arg(pyProjectTomlPath.toUserOutput()));
        return result;
    }

    auto contents = pyProjectTomlPath.fileContents();
    if (!contents) {
        result.addError(contents.error());
        return result;
    }

    toml::ordered_value rootTable;
    try {
        rootTable = toml::parse_str<toml::ordered_type_config>(contents->data());
    } catch (const toml::syntax_error &syntaxErrors) {
        for (const auto &error : syntaxErrors.errors()) {
            auto errorLine = static_cast<int>(error.locations().at(0).first.first_line_number());
            result.addError(
                Tr::tr("Parsing error: %1").arg(QString::fromStdString(error.title())), errorLine);
            return result;
        }
    }

    auto projectTable
        = getNodeByKey<toml::ordered_value>("TOML table", "root", rootTable, "project");
    if (!projectTable) {
        result.addError(projectTable.error(), -1);
        return result;
    }

    auto projectName
        = getNodeByKey<std::string>("TOML table", "project", projectTable.value(), "name");
    if (!projectName) {
        auto line = static_cast<int>(projectTable.value().location().first_line_number());
        result.addError(projectName.error(), line);
        return result;
    }
    result.projectName = QString::fromStdString(projectName.value());

    auto projectVersion
        = getNodeByKey<std::string>("string", "project", projectTable.value(), "version");
    if (!projectVersion) {
        auto line = static_cast<int>(projectTable.value().location().first_line_number());
        result.addError(projectVersion.error(), line);
        return result;
    }
    result.projectVersion = QString::fromStdString(projectVersion.value());

    auto toolTable = getNodeByKey<toml::ordered_value>("TOML table", "root", rootTable, "tool");
    if (!toolTable) {
        auto line = static_cast<int>(rootTable.location().first_line_number());
        result.addError(toolTable.error(), line);
        return result;
    }

    auto pysideTable = getNodeByKey<toml::ordered_value>(
        "TOML table", "tool", toolTable.value(), "pyside6-project");
    if (!pysideTable) {
        auto line = static_cast<int>(toolTable.value().location().first_line_number());
        result.addError(pysideTable.error(), line);
        return result;
    }

    auto files = getNodeByKey<toml::ordered_array>(
        "TOML array", "pyside6-project", pysideTable.value(), "files");
    if (!files) {
        auto line = static_cast<int>(pysideTable.value().location().first_line_number());
        result.addError(files.error(), line);
        return result;
    }

    const auto &filesArray = files.value();
    result.projectFiles.reserve(filesArray.size());

    for (const auto &fileNode : filesArray) {
        auto possibleFile = getNodeValue<std::string>("string", "files", fileNode);
        if (!possibleFile) {
            auto line = static_cast<int>(fileNode.location().first_line_number());
            result.addError(possibleFile.error(), line);
            continue;
        }

        auto file = QString::fromStdString(possibleFile.value());
        auto path = pyProjectTomlPath.parentDir().pathAppended(file);
        if (!path.exists()) {
            auto line = static_cast<int>(fileNode.location().first_line_number());
            result.addError(Tr::tr("File \"%1\" does not exist.").arg(file), line);
            continue;
        }
        result.projectFiles.append(file);
    }
    return result;
}

/*!
    \brief Given an existing pyproject.toml file, update it with the given \a projectFiles.
    If successful, returns the new contents of the file. Otherwise, returns an error.
*/
Utils::expected_str<QString> updatedPyProjectTomlContents(
    const Utils::FilePath &pyProjectTomlPath, const QStringList &projectFiles)
{
    auto contents = pyProjectTomlPath.fileContents();
    toml::ordered_value rootTable;
    try {
        rootTable = toml::parse_str<toml::ordered_type_config>(contents->data());
    } catch (const toml::exception &error) {
        return Utils::make_unexpected(QString::fromStdString(error.what()));
    }

    auto &toolTable = rootTable["tool"];
    if (!toolTable.is_table()) {
        toolTable = toml::ordered_table{};
    }

    auto &pysideTable = toolTable.as_table()["pyside6-project"];
    if (!pysideTable.is_table()) {
        pysideTable = toml::ordered_table{};
    }

    std::vector<std::string> filesArray;
    filesArray.reserve(projectFiles.size());
    for (const QString &qstr : projectFiles) {
        filesArray.push_back(qstr.toStdString());
    }
    std::sort(filesArray.begin(), filesArray.end());

    pysideTable["files"] = filesArray;

    auto result = QString::fromStdString(toml::format(rootTable));
    // For some reason, the TOML library adds two trailing newlines.
    if (result.endsWith("\n\n")) {
        result.chop(1);
    }
    return result;
}

} // namespace Python::Internal
