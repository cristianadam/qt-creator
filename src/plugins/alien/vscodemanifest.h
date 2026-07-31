// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/filepath.h>
#include <utils/result.h>

#include <QString>
#include <QStringList>

namespace Alien::Internal {

// A "languages" contribution entry from a VS Code manifest.
class VscodeLanguage
{
public:
    QString id;
    QStringList extensions; // e.g. { ".qml" }
    QStringList aliases;
    Utils::FilePath configuration; // language-configuration.json, if any
};

// A "commands" contribution entry.
class VscodeCommand
{
public:
    QString command;
    QString title;
    QString category;
};

// The subset of a VS Code extension package.json that we model so far.
class VscodeManifest
{
public:
    // Identity
    QString name;
    QString publisher;
    QString version;
    QString displayName;
    QString description;

    // Runtime
    Utils::FilePath rootDir; // install location (the folder holding package.json)
    QString main;            // relative JS entry point, may be empty (declarative-only)
    QStringList activationEvents;

    // Contributions (subset)
    QList<VscodeLanguage> languages;
    QList<VscodeCommand> commands;
    bool hasGrammars = false;
    bool hasDebuggers = false;

    QString qualifiedId() const; // "publisher.name"
    Utils::FilePath mainPath() const; // rootDir / main, or empty
    bool hasLanguageServer() const; // heuristic: has a main and contributes languages

    static Utils::Result<VscodeManifest> fromPackageJson(const Utils::FilePath &packageJson);
};

} // namespace Alien::Internal
