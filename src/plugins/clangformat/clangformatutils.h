// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "clangformatsettings.h"

#include <utils/filepath.h>
#include <utils/id.h>

#include <clang/Format/Format.h>

#include <QFile>

namespace TextEditor {
class ICodeStylePreferences;
class TabSettings;
}
namespace ProjectExplorer { class Project; }
namespace CppEditor { class CppCodeStyleSettings; }

namespace ClangFormat {

QString projectUniqueId(const Utils::FilePath &projectFile);

bool getProjectUseGlobalSettings(const Utils::FilePath &projectFile);

bool getProjectCustomSettings(const Utils::FilePath &projectFile);
bool getCurrentCustomSettings(const Utils::FilePath &projectFile);

ClangFormatSettings::Mode getProjectIndentationOrFormattingSettings(const Utils::FilePath &projectFile);
ClangFormatSettings::Mode getCurrentIndentationOrFormattingSettings(const Utils::FilePath &projectFile);

TextEditor::ICodeStylePreferences *preferencesForFile(const Utils::FilePath &filePath);
Utils::FilePath configForFile(const Utils::FilePath &filePath);
Utils::FilePath findConfig(const Utils::FilePath &filePath);

void fromTabSettings(clang::format::FormatStyle &style, const TextEditor::TabSettings &settings);
void fromCppCodeStyleSettings(clang::format::FormatStyle &style,
                              const CppEditor::CppCodeStyleSettings &settings);

bool getProjectCustomSettings(const ProjectExplorer::Project *project);

void addQtcStatementMacros(clang::format::FormatStyle &style);
clang::format::FormatStyle qtcStyle();
clang::format::FormatStyle currentQtStyle(const TextEditor::ICodeStylePreferences *codeStyle);

Utils::FilePath filePathToCurrentSettings(const TextEditor::ICodeStylePreferences *codeStyle);

Utils::Result<> parseConfigurationContent(const std::string &fileContent,
                                                    clang::format::FormatStyle &style,
                                                    bool allowUnknownOptions = false);
Utils::Result<> parseConfigurationFile(const Utils::FilePath &filePath,
                                                 clang::format::FormatStyle &style);

} // ClangFormat
