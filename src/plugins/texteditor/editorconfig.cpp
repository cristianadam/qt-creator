// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "editorconfig.h"

#include "extraencodingsettings.h"
#include "marginsettings.h"
#include "storagesettings.h"
#include "tabsettings.h"
#include "texteditortr.h"

#include <utils/filepath.h>
#include <utils/layoutbuilder.h>

#include <QRegularExpression>

using namespace Utils;

namespace TextEditor {

namespace Internal {

struct NumericRange
{
    int min = 0;
    int max = 0;
};

struct EditorConfigSection
{
    QString glob;
    QList<std::pair<QString, QString>> properties;
};

struct EditorConfigFile
{
    bool root = false;
    QList<EditorConfigSection> sections;
};

static int findClosingBracket(const QString &glob, int open)
{
    for (int i = open + 1; i < glob.size(); ++i) {
        const QChar c = glob.at(i);
        if (c == '\\')
            ++i;
        else if (c == ']')
            return i;
    }
    return -1;
}

static int findClosingBrace(const QString &glob, int open)
{
    int depth = 0;
    for (int i = open; i < glob.size(); ++i) {
        const QChar c = glob.at(i);
        if (c == '\\') {
            ++i;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            if (--depth == 0)
                return i;
        }
    }
    return -1;
}

static QStringList splitAlternatives(const QString &content)
{
    QStringList result;
    int depth = 0;
    int start = 0;
    for (int i = 0; i < content.size(); ++i) {
        const QChar c = content.at(i);
        if (c == '\\') {
            ++i;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
        } else if (c == ',' && depth == 0) {
            result.append(content.mid(start, i - start));
            start = i + 1;
        }
    }
    result.append(content.mid(start));
    return result;
}

static QString bracketToRegExp(const QString &content)
{
    QString result = "[";
    int i = 0;
    const bool negated = content.startsWith('!') || content.startsWith('^');
    if (negated) {
        result += '^';
        i = 1;
    }
    for (; i < content.size(); ++i) {
        QChar c = content.at(i);
        if (c == '-') {
            result += c;
            continue;
        }
        if (c == '\\' && i + 1 < content.size())
            c = content.at(++i);
        if (c == '\\' || c == ']' || c == '[' || c == '^' || c == '-')
            result += '\\';
        result += c;
    }
    if (negated)
        result += '/';
    result += ']';
    return result;
}

static QString globToRegExp(const QString &glob, QList<NumericRange> &ranges)
{
    static const QRegularExpression rangeRegExp(R"(^([+-]?\d+)\.\.([+-]?\d+)$)");

    QString result;
    for (int i = 0; i < glob.size(); ++i) {
        const QChar c = glob.at(i);
        if (c == '\\') {
            if (i + 1 < glob.size())
                ++i;
            result += QRegularExpression::escape(glob.mid(i, 1));
        } else if (c == '*') {
            if (i + 1 < glob.size() && glob.at(i + 1) == '*') {
                const bool afterSlash = i > 0 && glob.at(i - 1) == '/';
                ++i;
                if (afterSlash && i + 1 < glob.size() && glob.at(i + 1) == '/') {
                    // "a/**/b" also matches "a/b".
                    ++i;
                    result += "(?:.*/)?";
                } else {
                    result += ".*";
                }
            } else {
                result += "[^/]*";
            }
        } else if (c == '?') {
            result += "[^/]";
        } else if (c == '[') {
            const int close = findClosingBracket(glob, i);
            const QString content = close < 0 ? QString() : glob.mid(i + 1, close - i - 1);
            if (content.isEmpty() || content.contains('/')) {
                result += "\\[";
            } else {
                result += bracketToRegExp(content);
                i = close;
            }
        } else if (c == '{') {
            const int close = findClosingBrace(glob, i);
            if (close < 0) {
                result += "\\{";
                continue;
            }
            const QString content = glob.mid(i + 1, close - i - 1);
            i = close;
            const QRegularExpressionMatch match = rangeRegExp.match(content);
            if (match.hasMatch()) {
                const int first = match.captured(1).toInt();
                const int second = match.captured(2).toInt();
                ranges.append({qMin(first, second), qMax(first, second)});
                result += "([+-]?\\d+)";
                continue;
            }
            const QStringList alternatives = splitAlternatives(content);
            if (alternatives.size() < 2) {
                result += "\\{" + globToRegExp(content, ranges) + "\\}";
                continue;
            }
            QStringList converted;
            for (const QString &alternative : alternatives)
                converted.append(globToRegExp(alternative, ranges));
            result += "(?:" + converted.join('|') + ')';
        } else {
            result += QRegularExpression::escape(QString(c));
        }
    }
    return result;
}

/*!
    Returns whether \a relativeFilePath, given relative to the directory
    of an \c .editorconfig file, matches the section name \a glob of that file.

    A glob without a slash matches the file name in any subdirectory, one with
    a slash is anchored at the directory of the \c .editorconfig file.
*/
bool editorConfigGlobMatches(const QString &glob, const QString &relativeFilePath)
{
    QString pattern = glob;
    QString prefix;
    if (pattern.contains('/')) {
        if (pattern.startsWith('/'))
            pattern.remove(0, 1);
    } else {
        prefix = "(?:.*/)?";
    }

    QList<NumericRange> ranges;
    const QRegularExpression regExp(
        QRegularExpression::anchoredPattern(prefix + globToRegExp(pattern, ranges)));
    if (!regExp.isValid())
        return false;

    const QRegularExpressionMatch match = regExp.match(relativeFilePath);
    if (!match.hasMatch())
        return false;
    for (int i = 0; i < ranges.size(); ++i) {
        bool ok = false;
        const int number = match.captured(i + 1).toInt(&ok);
        if (!ok || number < ranges.at(i).min || number > ranges.at(i).max)
            return false;
    }
    return true;
}

static EditorConfigFile parseEditorConfig(const QByteArray &contents)
{
    EditorConfigFile result;
    QString text = QString::fromUtf8(contents);
    if (text.startsWith(QChar(0xfeff)))
        text.remove(0, 1);

    int currentSection = -1;
    bool inInvalidSection = false;
    for (const QString &rawLine : text.split('\n')) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';'))
            continue;
        if (line.startsWith('[')) {
            inInvalidSection = !line.endsWith(']');
            if (!inInvalidSection) {
                result.sections.append({line.mid(1, line.size() - 2), {}});
                currentSection = result.sections.size() - 1;
            }
            continue;
        }
        const int equals = line.indexOf('=');
        if (equals <= 0 || inInvalidSection)
            continue;
        const QString key = line.left(equals).trimmed().toLower();
        const QString value = line.mid(equals + 1).trimmed();
        if (currentSection < 0) {
            if (key == "root")
                result.root = value.toLower() == "true";
            continue;
        }
        result.sections[currentSection].properties.append({key, value});
    }
    return result;
}

/*!
    Returns the EditorConfig properties that apply to \a filePath, collected
    from the \c .editorconfig files in its directory and the directories above
    it up to the first one declaring \c {root = true}.

    Property names are lower case. A property whose value is \c unset is
    not contained in the result.
*/
QMap<QString, QString> editorConfigPropertiesForFile(const FilePath &filePath)
{
    QList<std::pair<FilePath, EditorConfigFile>> files;
    for (FilePath dir = filePath.parentDir(); !dir.isEmpty();) {
        if (const Result<QByteArray> contents = dir.pathAppended(".editorconfig").fileContents()) {
            EditorConfigFile file = parseEditorConfig(*contents);
            const bool root = file.root;
            files.prepend({dir, file});
            if (root)
                break;
        }
        const FilePath parent = dir.parentDir();
        if (parent == dir)
            break;
        dir = parent;
    }

    QMap<QString, QString> properties;
    for (const auto &[dir, file] : std::as_const(files)) {
        const QString relativePath = filePath.relativeChildPath(dir).path();
        if (relativePath.isEmpty())
            continue;
        for (const EditorConfigSection &section : file.sections) {
            if (!editorConfigGlobMatches(section.glob, relativePath))
                continue;
            for (const auto &[key, value] : section.properties) {
                if (value.toLower() == "unset")
                    properties.remove(key);
                else
                    properties.insert(key, value);
            }
        }
    }
    return properties;
}

} // namespace Internal

/*!
    \class TextEditor::EditorConfigProperties
    \brief The EditorConfigProperties class holds the settings an \c .editorconfig
    file defines for a file.

    Properties that are not set leave the corresponding editor settings untouched.

    \sa {https://editorconfig.org}
*/

EditorConfigProperties EditorConfigProperties::forFile(const FilePath &filePath)
{
    if (filePath.isEmpty() || !globalEditorConfigSettings().enabled())
        return {};
    return fromProperties(Internal::editorConfigPropertiesForFile(filePath));
}

static std::optional<int> positiveNumber(const QString &value)
{
    bool ok = false;
    const int number = value.toInt(&ok);
    if (ok && number > 0)
        return number;
    return {};
}

static std::optional<bool> boolean(const QString &value)
{
    if (value == "true")
        return true;
    if (value == "false")
        return false;
    return {};
}

EditorConfigProperties EditorConfigProperties::fromProperties(
    const QMap<QString, QString> &properties)
{
    const auto value = [&properties](const QString &key) {
        return properties.value(key).toLower();
    };

    EditorConfigProperties result;

    const QString indentStyle = value("indent_style");
    if (indentStyle == "tab")
        result.indentWithTabs = true;
    else if (indentStyle == "space")
        result.indentWithTabs = false;

    result.tabWidth = positiveNumber(value("tab_width"));

    const QString indentSize = value("indent_size");
    if (indentSize == "tab" || (indentSize.isEmpty() && result.indentWithTabs.value_or(false))) {
        if (result.tabWidth)
            result.indentSize = result.tabWidth;
        else
            result.indentSizeIsTabWidth = true;
    } else {
        result.indentSize = positiveNumber(indentSize);
    }
    if (result.indentSize && !result.tabWidth)
        result.tabWidth = result.indentSize;

    const QString endOfLine = value("end_of_line");
    if (endOfLine == "lf")
        result.lineTerminationMode = TextFileFormat::LFLineTerminator;
    else if (endOfLine == "crlf")
        result.lineTerminationMode = TextFileFormat::CRLFLineTerminator;

    const QString charset = value("charset");
    TextEncoding encoding;
    if (charset == "utf-8" || charset == "utf-8-bom") {
        encoding = TextEncoding(TextEncoding::Utf8);
        result.utf8Bom = charset == "utf-8-bom";
    } else if (charset == "latin1") {
        encoding = TextEncoding(TextEncoding::Latin1);
    } else if (charset == "utf-16be") {
        encoding = TextEncoding("UTF-16BE");
    } else if (charset == "utf-16le") {
        encoding = TextEncoding("UTF-16LE");
    }
    if (encoding.isValid())
        result.encoding = encoding;

    result.trimTrailingWhitespace = boolean(value("trim_trailing_whitespace"));
    result.insertFinalNewline = boolean(value("insert_final_newline"));
    result.maxLineLength = positiveNumber(value("max_line_length"));

    return result;
}

bool EditorConfigProperties::isEmpty() const
{
    return !indentWithTabs && !indentSize && !indentSizeIsTabWidth && !tabWidth
           && !lineTerminationMode && !encoding && !utf8Bom && !trimTrailingWhitespace
           && !insertFinalNewline && !maxLineLength;
}

void EditorConfigProperties::applyTo(TabSettingsData &tabSettings) const
{
    if (indentWithTabs) {
        tabSettings.m_tabPolicy = *indentWithTabs ? TabSettingsData::TabsOnlyTabPolicy
                                                  : TabSettingsData::SpacesOnlyTabPolicy;
        tabSettings.m_autoDetect = false;
    }
    if (tabWidth)
        tabSettings.m_tabSize = *tabWidth;
    if (indentSize) {
        tabSettings.m_indentSize = *indentSize;
        tabSettings.m_autoDetect = false;
    } else if (indentSizeIsTabWidth) {
        tabSettings.m_indentSize = tabSettings.m_tabSize;
        tabSettings.m_autoDetect = false;
    }
}

void EditorConfigProperties::applyTo(StorageSettingsData &storageSettings) const
{
    if (trimTrailingWhitespace) {
        storageSettings.m_cleanWhitespace = *trimTrailingWhitespace;
        if (*trimTrailingWhitespace) {
            storageSettings.m_inEntireDocument = true;
            storageSettings.m_skipTrailingWhitespace = false;
        }
    }
    if (insertFinalNewline)
        storageSettings.m_addFinalNewLine = *insertFinalNewline;
}

void EditorConfigProperties::applyTo(ExtraEncodingSettingsData &extraEncodingSettings) const
{
    if (utf8Bom) {
        extraEncodingSettings.m_utf8BomSetting = *utf8Bom ? ExtraEncodingSettingsData::AlwaysAdd
                                                          : ExtraEncodingSettingsData::AlwaysDelete;
    }
}

void EditorConfigProperties::applyTo(MarginSettingsData &marginSettings) const
{
    if (maxLineLength)
        marginSettings.m_marginColumn = *maxLineLength;
}

EditorConfigSettings::EditorConfigSettings()
{
    setAutoApply(false);

    setSettingsGroup("textEditorConfigSettings");

    enabled.setSettingsKey("enabled");
    enabled.setDefaultValue(true);
    enabled.setLabelText(Tr::tr("Use settings from .editorconfig files"));
    enabled.setToolTip(Tr::tr("Applies the indentation, encoding, line ending and whitespace "
                              "settings of .editorconfig files to files when opening them."));

    setLayouter([this] {
        using namespace Layouting;
        return Column {
            Group {
                title(Tr::tr("EditorConfig")),
                Column { enabled }
            }
        };
    });

    readSettings();
}

void EditorConfigSettings::apply()
{
    AspectContainer::apply();
    AspectContainer::writeSettings();
}

EditorConfigSettings &globalEditorConfigSettings()
{
    static EditorConfigSettings theGlobalEditorConfigSettings;
    return theGlobalEditorConfigSettings;
}

} // namespace TextEditor
