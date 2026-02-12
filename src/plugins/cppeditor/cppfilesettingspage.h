// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <coreplugin/dialogs/ioptionspage.h>
#include <utils/aspects.h>

namespace ExtensionSystem { class IPlugin; }
namespace ProjectExplorer { class Project; }

namespace CppEditor::Internal {

class CppFileSettings : public Utils::AspectContainer
{
public:
    CppFileSettings();
    CppFileSettings(const CppFileSettings &other);

    void operator=(const CppFileSettings &other);

    Utils::StringListAspect headerPrefixes{this};
    Utils::StringAspect headerSuffix{this};
    Utils::StringListAspect headerSearchPaths{this};

    Utils::StringListAspect sourcePrefixes{this};
    Utils::StringAspect sourceSuffix{this};
    Utils::StringListAspect sourceSearchPaths{this};

    Utils::FilePathAspect licenseTemplatePath{this};
    Utils::StringAspect headerGuardTemplate{this};

    Utils::BoolAspect headerPragmaOnce{this};
    Utils::BoolAspect lowerCaseFiles{this};

    void addMimeInitializer() const;
    bool applySuffixesToMimeDB();

    // Convenience to return a license template completely formatted.
    QString licenseTemplate() const;

    // Expanded headerGuardTemplate.
    QString headerGuard(const Utils::FilePath &headerFilePath) const;

    bool operator==(const CppFileSettings &s) const { return equals(s); }
    bool operator!=(const CppFileSettings &s) const { return !equals(s); }
};

CppFileSettings &globalCppFileSettings();

CppFileSettings cppFileSettingsForProject(ProjectExplorer::Project *project);

void setupCppFileSettings(ExtensionSystem::IPlugin &plugin);

} // namespace CppEditor::Internal
