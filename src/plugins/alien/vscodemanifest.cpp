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

#include <algorithm>

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

// These files are written with comments in them, which JSON does not have. A
// scanner rather than a pattern, because "//" inside a string is not one.
static QString withoutComments(const QString &text)
{
    QString result;
    result.reserve(text.size());
    bool inString = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (inString) {
            result += c;
            if (c == '\\' && i + 1 < text.size()) {
                result += text.at(++i);
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
            result += c;
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text.at(i + 1) == '/') {
            while (i < text.size() && text.at(i) != '\n')
                ++i;
            result += '\n';
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text.at(i + 1) == '*') {
            i += 2;
            while (i + 1 < text.size() && !(text.at(i) == '*' && text.at(i + 1) == '/'))
                ++i;
            ++i;
            continue;
        }
        result += c;
    }
    return result;
}

static QList<QPair<QString, QString>> parsePairs(const QJsonArray &array)
{
    QList<QPair<QString, QString>> pairs;
    for (const QJsonValue &value : array) {
        // Either ["(", ")"] or {"open": "(", "close": ")", "notIn": [...]}.
        if (value.isArray()) {
            const QJsonArray pair = value.toArray();
            if (pair.size() >= 2)
                pairs.append({pair.at(0).toString(), pair.at(1).toString()});
        } else if (value.isObject()) {
            const QJsonObject pair = value.toObject();
            if (pair.contains("open"))
                pairs.append({pair.value("open").toString(), pair.value("close").toString()});
        }
    }
    return pairs;
}

// How a language is written, as the extension describes it: which characters
// come in pairs, and what marks a foldable region.
// Comments in the file itself are allowed, so it is not quite JSON.
static void readLanguageConfiguration(VscodeLanguage &language)
{
    const Result<QByteArray> contents = language.configuration.fileContents();
    if (!contents)
        return;
    const QJsonObject root
        = QJsonDocument::fromJson(withoutComments(QString::fromUtf8(*contents)).toUtf8()).object();

    language.brackets = parsePairs(root.value("brackets").toArray());
    language.autoClosingPairs = parsePairs(root.value("autoClosingPairs").toArray());
    language.surroundingPairs = parsePairs(root.value("surroundingPairs").toArray());

    const QJsonObject markers = root.value("folding").toObject().value("markers").toObject();
    language.foldingStartMarker = markers.value("start").toString();
    language.foldingEndMarker = markers.value("end").toString();
}

static QList<VscodeLanguage> parseLanguages(const QJsonArray &array, const FilePath &rootDir)
{
    QList<VscodeLanguage> result;
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        VscodeLanguage language;
        language.id = object.value("id").toString();
        language.extensions = toStringList(object.value("extensions"));
        language.filenames = toStringList(object.value("filenames"));
        language.filenamePatterns = toStringList(object.value("filenamePatterns"));
        language.aliases = toStringList(object.value("aliases"));
        const QString configuration = object.value("configuration").toString();
        if (!configuration.isEmpty()) {
            language.configuration = rootDir.resolvePath(configuration);
            readLanguageConfiguration(language);
        }
        result << language;
    }
    return result;
}

static void collectConfiguration(const QJsonValue &configuration, QJsonObject &defaults,
                                 QList<VscodeSetting> &settings)
{
    // "configuration" is a single object or an array of them; each has a
    // "properties" map of dotted-key -> {"type": ..., "default": ...}.
    const QJsonArray sections = configuration.isArray()
                                    ? configuration.toArray()
                                    : QJsonArray{configuration};
    for (const QJsonValue &section : sections) {
        const QString sectionTitle = section.toObject().value("title").toString();
        const QJsonObject properties = section.toObject().value("properties").toObject();
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            const QJsonObject property = it.value().toObject();
            if (property.contains("default"))
                defaults.insert(it.key(), property.value("default"));

            VscodeSetting setting;
            setting.key = it.key();
            setting.section = sectionTitle;
            // A union type ("string" or null, say) is listed; the first one is
            // what the setting is really about.
            setting.type = property.value("type").isArray()
                               ? property.value("type").toArray().first().toString()
                               : property.value("type").toString();
            setting.description = property.value("description").toString();
            if (setting.description.isEmpty()) {
                setting.description = property.value("markdownDescription").toString();
                setting.descriptionIsMarkdown = !setting.description.isEmpty();
            }
            setting.defaultValue = property.value("default");
            setting.enumValues = toStringList(property.value("enum"));
            setting.enumRawValues = property.value("enum").toArray();
            setting.enumDescriptions = toStringList(property.value("enumDescriptions"));
            if (setting.enumDescriptions.isEmpty()) {
                setting.enumDescriptions
                    = toStringList(property.value("markdownEnumDescriptions"));
            }
            setting.enumItemLabels = toStringList(property.value("enumItemLabels"));
            setting.deprecationMessage = property.value("deprecationMessage").toString();
            if (setting.deprecationMessage.isEmpty()) {
                setting.deprecationMessage
                    = property.value("markdownDeprecationMessage").toString();
            }
            if (property.contains("order"))
                setting.order = property.value("order").toInt();
            if (property.contains("minimum"))
                setting.minimum = property.value("minimum").toDouble();
            if (property.contains("maximum"))
                setting.maximum = property.value("maximum").toDouble();
            settings.append(setting);
        }
    }
    // The order the extension asked for; the rest keep the order they came in.
    std::stable_sort(settings.begin(), settings.end(),
                     [](const VscodeSetting &a, const VscodeSetting &b) {
                         return a.order < b.order;
                     });
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

// Translatable manifest strings are kept out of package.json: a value of
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
            // An unknown key keeps the placeholder.
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
    for (const QJsonValue &value : contributes.value("semanticTokenTypes").toArray())
        manifest.semanticTokenTypes << value.toObject().value("id").toString();
    manifest.hasDebuggers = contributes.contains("debuggers");
    collectConfiguration(contributes.value("configuration"), manifest.configurationDefaults,
                         manifest.configurationSettings);

    for (const QJsonValue &value : contributes.value("keybindings").toArray()) {
        const QJsonObject object = value.toObject();
        VscodeKeybinding binding;
        binding.command = object.value("command").toString();
        binding.key = object.value("key").toString();
        binding.mac = object.value("mac").toString();
        binding.linux = object.value("linux").toString();
        binding.windows = object.value("win").toString();
        binding.when = object.value("when").toString();
        binding.arguments = object.value("args");
        if (!binding.command.isEmpty())
            manifest.keybindings.append(binding);
    }

    for (const QJsonValue &value : contributes.value("debuggers").toArray()) {
        const QJsonObject object = value.toObject();
        VscodeDebugger debugger;
        debugger.type = object.value("type").toString();
        debugger.label = object.value("label").toString();
        debugger.program = object.value("program").toString();
        debugger.runtime = object.value("runtime").toString();
        debugger.languages = toStringList(object.value("languages"));
        if (!debugger.type.isEmpty())
            manifest.debuggers.append(debugger);
    }

    const QJsonArray paletteRules
        = contributes.value("menus").toObject().value("commandPalette").toArray();
    for (const QJsonValue &value : paletteRules) {
        const QJsonObject rule = value.toObject();
        const QString when = rule.value("when").toString().trimmed();
        if (when == "false" || when == "!true")
            manifest.paletteHiddenCommands.append(rule.value("command").toString());
    }

    return manifest;
}

} // namespace Alien::Internal
