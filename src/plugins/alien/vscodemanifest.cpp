// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "vscodemanifest.h"

#include "alientr.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

using namespace Utils;

namespace Alien::Internal {

QString VscodeManifest::qualifiedId() const
{
    if (publisher.isEmpty())
        return name;
    return publisher + '.' + name;
}

FilePath VscodeManifest::mainPath() const
{
    if (main.isEmpty())
        return {};
    return rootDir.resolvePath(main);
}

bool VscodeManifest::hasLanguageServer() const
{
    return !main.isEmpty() && !languages.isEmpty();
}

static QStringList toStringList(const QJsonValue &value)
{
    QStringList result;
    if (value.isString())
        result << value.toString();
    else if (value.isArray()) {
        for (const QJsonValue &entry : value.toArray())
            result << entry.toString();
    }
    return result;
}

static QList<VscodeLanguage> parseLanguages(const QJsonArray &array, const FilePath &rootDir)
{
    QList<VscodeLanguage> result;
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        VscodeLanguage language;
        language.id = object.value("id").toString();
        language.extensions = toStringList(object.value("extensions"));
        language.aliases = toStringList(object.value("aliases"));
        const QString configuration = object.value("configuration").toString();
        if (!configuration.isEmpty())
            language.configuration = rootDir.resolvePath(configuration);
        result << language;
    }
    return result;
}

static void collectConfigurationDefaults(const QJsonValue &configuration, QJsonObject &out)
{
    // "configuration" is a single object or an array of them; each has a
    // "properties" map of dotted-key -> {"default": ...}.
    const QJsonArray sections = configuration.isArray()
                                    ? configuration.toArray()
                                    : QJsonArray{configuration};
    for (const QJsonValue &section : sections) {
        const QJsonObject properties = section.toObject().value("properties").toObject();
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            const QJsonObject property = it.value().toObject();
            if (property.contains("default"))
                out.insert(it.key(), property.value("default"));
        }
    }
}

static QList<VscodeCommand> parseCommands(const QJsonArray &array)
{
    QList<VscodeCommand> result;
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        VscodeCommand command;
        command.command = object.value("command").toString();
        command.title = object.value("title").toString();
        command.category = object.value("category").toString();
        result << command;
    }
    return result;
}

Result<VscodeManifest> VscodeManifest::fromPackageJson(const FilePath &packageJson)
{
    const Result<QByteArray> contents = packageJson.fileContents();
    if (!contents)
        return make_unexpected(contents.error());

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(*contents, &error);
    if (document.isNull()) {
        return make_unexpected(Tr::tr("Cannot parse \"%1\": %2")
                                   .arg(packageJson.toUserOutput(), error.errorString()));
    }

    const QJsonObject root = document.object();

    VscodeManifest manifest;
    manifest.rootDir = packageJson.parentDir();
    manifest.name = root.value("name").toString();
    manifest.publisher = root.value("publisher").toString();
    manifest.version = root.value("version").toString();
    manifest.displayName = root.value("displayName").toString();
    manifest.description = root.value("description").toString();
    manifest.main = root.value("main").toString();
    manifest.activationEvents = toStringList(root.value("activationEvents"));
    manifest.extensionDependencies = toStringList(root.value("extensionDependencies"));
    manifest.rawPackageJson = root;

    if (manifest.name.isEmpty())
        return make_unexpected(Tr::tr("\"%1\" has no name field.").arg(packageJson.toUserOutput()));

    const QJsonObject contributes = root.value("contributes").toObject();
    manifest.languages = parseLanguages(contributes.value("languages").toArray(), manifest.rootDir);
    manifest.commands = parseCommands(contributes.value("commands").toArray());
    manifest.hasGrammars = contributes.contains("grammars");
    manifest.hasDebuggers = contributes.contains("debuggers");
    collectConfigurationDefaults(contributes.value("configuration"),
                                 manifest.configurationDefaults);

    return manifest;
}

} // namespace Alien::Internal
