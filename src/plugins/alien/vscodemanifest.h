// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/filepath.h>
#include <utils/result.h>

#include <QJsonArray>
#include <QPair>
#include <QJsonObject>

#include <limits>
#include <optional>
#include <QString>
#include <QStringList>

namespace Alien::Internal {

// A "languages" contribution entry from a VS Code manifest.
class VscodeLanguage
{
public:
    QString id;
    QStringList extensions; // e.g. { ".qml" }
    QStringList filenames; // whole names, e.g. { "qmldir" }
    QStringList filenamePatterns; // globs, e.g. { "Dockerfile.*" }
    QStringList aliases;
    Utils::FilePath configuration; // language-configuration.json, if any

    // What that file says about the language, once it has been read: how it is
    // commented, which characters come in pairs, and what marks a foldable
    // region.
    QList<QPair<QString, QString>> brackets;
    QList<QPair<QString, QString>> autoClosingPairs;
    QList<QPair<QString, QString>> surroundingPairs;
    QString foldingStartMarker;
    QString foldingEndMarker;

    // The name to show, which is the first alias when there is one.
    QString displayName() const { return aliases.isEmpty() ? id : aliases.first(); }
};

// A "commands" contribution entry.
class VscodeCommand
{
public:
    QString command;
    QString title;
    QString category;
};

// A "keybindings" contribution entry: the shortcut an extension asks for, per
// platform, for one of its commands.
class VscodeKeybinding
{
public:
    QString command;
    QString key;
    QString mac;
    QString linux;
    QString windows;
    QString when; // the clause that has to hold for the key to do anything
    QJsonValue arguments; // what the command is handed
};

// A "debuggers" contribution entry: a debug type the extension can serve, and
// what the manifest already says about how its adapter is started. Most name
// no program and answer with a descriptor factory once running.
class VscodeDebugger
{
public:
    QString type;
    QString label;
    QString program; // relative to the extension, if given
    QString runtime; // "node", "python", ... if the program needs one
    QStringList languages;
};

// One setting an extension declares, with what it takes to edit it.
class VscodeSetting
{
public:
    QString key; // dotted, as the extension reads it
    QString section; // the heading the extension groups it under
    QString type; // "boolean", "string", "number", "integer", ...
    QString description;
    QJsonValue defaultValue;
    QStringList enumValues;
    QStringList enumDescriptions;
    QStringList enumItemLabels;
    QJsonArray enumRawValues; // the values themselves, which need not be strings
    QString deprecationMessage;
    bool descriptionIsMarkdown = false;
    int order = std::numeric_limits<int>::max(); // where the extension wants it
    std::optional<double> minimum;
    std::optional<double> maximum;
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
    QStringList extensionDependencies; // e.g. { "theqtcompany.qt-core" }

    // Contributions (subset)
    QList<VscodeLanguage> languages;
    QList<VscodeCommand> commands;
    bool hasGrammars = false;
    bool hasDebuggers = false;

    // The full parsed package.json, passed to the host as the extension's
    // packageJSON.
    QJsonObject rawPackageJson;

    // Default values from contributes.configuration, keyed by dotted setting
    // name (e.g. "qt-qml.qmlls.customExePath").
    QJsonObject configurationDefaults;
    QStringList semanticTokenTypes; // token types only this extension knows

    // The same settings with what an editor for them needs.
    QList<VscodeSetting> configurationSettings;

    // Commands the manifest keeps out of the command palette outright.
    QStringList paletteHiddenCommands;

    QList<VscodeDebugger> debuggers;
    QList<VscodeKeybinding> keybindings;

    QString qualifiedId() const; // "publisher.name"
    Utils::FilePath mainPath() const; // rootDir / main, or empty
    bool hasLanguageServer() const; // heuristic: has a main and contributes languages

    static Utils::Result<VscodeManifest> fromPackageJson(const Utils::FilePath &packageJson);
};

} // namespace Alien::Internal
