// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "vscodemanifest.h"
#include <QRegularExpression>
#include <coreplugin/icore.h>

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

// VS Code keeps translatable manifest strings out of package.json: a value of
// "%some.key%" is looked up in package.nls.json, or in package.nls.<locale>.json
// for the language the IDE runs in. Nothing else in the file is touched.
static QJsonObject translationBundle(const FilePath &dir)
{
    const auto read = [](const FilePath &file) {
        const Result<QByteArray> contents = file.fileContents();
        return contents ? QJsonDocument::fromJson(*contents).object() : QJsonObject();
    };
    QJsonObject bundle = read(dir / "package.nls.json");
    QString language = Core::ICore::userInterfaceLanguage();
    while (!language.isEmpty()) {
        const QJsonObject localized = read(dir / QString("package.nls.%1.json").arg(language));
        if (!localized.isEmpty()) {
            for (auto it = localized.begin(); it != localized.end(); ++it)
                bundle.insert(it.key(), it.value());
            break;
        }
        // "de_DE" also answers to a "de" bundle.
        const qsizetype cut = language.lastIndexOf(QRegularExpression("[-_]"));
        language = cut > 0 ? language.left(cut) : QString();
    }
    return bundle;
}

static QJsonValue translated(const QJsonValue &value, const QJsonObject &bundle)
{
    if (value.isString()) {
        const QString string = value.toString();
        if (string.size() > 2 && string.startsWith('%') && string.endsWith('%')) {
            const QJsonValue message = bundle.value(string.mid(1, string.size() - 2));
            // An unknown key keeps the placeholder, as VS Code does.
            return message.isString() ? message : value;
        }
        return value;
    }
    if (value.isObject()) {
        QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            *it = translated(*it, bundle);
        return object;
    }
    if (value.isArray()) {
        QJsonArray array = value.toArray();
        for (QJsonValueRef item : array)
            item = translated(item, bundle);
        return array;
    }
    return value;
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

    const QJsonObject bundle = translationBundle(packageJson.parentDir());
    const QJsonObject root = bundle.isEmpty()
                                 ? document.object()
                                 : translated(document.object(), bundle).toObject();

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
