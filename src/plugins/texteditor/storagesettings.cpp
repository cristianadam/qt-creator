// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "storagesettings.h"

#include "texteditorsettings.h"

#include <coreplugin/icore.h>

#include <utils/hostosinfo.h>

#include <QRegularExpression>

using namespace Utils;

namespace TextEditor {

const char defaultTrailingWhitespaceBlacklist[] = "*.md, *.MD, Makefile";

StorageSettingsData::StorageSettingsData()
    : m_ignoreFileTypes(defaultTrailingWhitespaceBlacklist),
      m_cleanWhitespace(true),
      m_inEntireDocument(false),
      m_addFinalNewLine(false),
      m_cleanIndentation(true),
      m_skipTrailingWhitespace(true)
{
}

void StorageSettings::setData(const StorageSettingsData &data)
{
    cleanWhitespace.setValue(data.m_cleanWhitespace);
    inEntireDocument.setValue(data.m_inEntireDocument);
    addFinalNewLine.setValue(data.m_addFinalNewLine);
    cleanIndentation.setValue(data.m_cleanIndentation);
    skipTrailingWhitespace.setValue(data.m_skipTrailingWhitespace);
    ignoreFileTypes.setValue(data.m_ignoreFileTypes);
}

StorageSettings::StorageSettings()
{
    setAutoApply(false);

    setSettingsGroup("textStorageSettings");

    cleanWhitespace.setSettingsKey("cleanWhitespace");
    cleanWhitespace.setDefaultValue(true);

    inEntireDocument.setSettingsKey("inEntireDocument");
    inEntireDocument.setDefaultValue(false);

    addFinalNewLine.setSettingsKey("addFinalNewLine");
    addFinalNewLine.setDefaultValue(false);

    cleanIndentation.setSettingsKey("cleanIndentation");
    cleanIndentation.setDefaultValue(true);

    skipTrailingWhitespace.setSettingsKey("skipTrailingWhitespace");
    skipTrailingWhitespace.setDefaultValue(true);

    ignoreFileTypes.setSettingsKey("ignoreFileTypes");
    ignoreFileTypes.setDefaultValue("*.md, *.MD, Makefile");
}

StorageSettingsData StorageSettings::data() const
{
    StorageSettingsData d;
    d.m_cleanWhitespace = cleanWhitespace();
    d.m_inEntireDocument = inEntireDocument();
    d.m_addFinalNewLine = addFinalNewLine();
    d.m_cleanIndentation = cleanIndentation();
    d.m_skipTrailingWhitespace = skipTrailingWhitespace();
    d.m_ignoreFileTypes = ignoreFileTypes();
    return d;
}

bool StorageSettingsData::removeTrailingWhitespace(const QString &fileName) const
{
    // if the user has elected not to trim trailing whitespace altogether, then
    // early out here
    if (!m_skipTrailingWhitespace) {
        return true;
    }

    const QString ignoreFileTypesRegExp(R"(\s*((?>\*\.)?[\w\d\.\*]+)[,;]?\s*)");

    // use the ignore-files regex to extract the specified file patterns
    static const QRegularExpression re(ignoreFileTypesRegExp);
    QRegularExpressionMatchIterator iter = re.globalMatch(m_ignoreFileTypes);

    while (iter.hasNext()) {
        QRegularExpressionMatch match = iter.next();
        QString pattern = match.captured(1);

        QRegularExpression patternRegExp(QRegularExpression::wildcardToRegularExpression(pattern));
        QRegularExpressionMatch patternMatch = patternRegExp.match(fileName);
        if (patternMatch.hasMatch()) {
            // if the filename has a pattern we want to ignore, then we need to return
            // false ("don't remove trailing whitespace")
            return false;
        }
    }

    // the supplied pattern does not match, so we want to remove trailing whitespace
    return true;
}

bool StorageSettingsData::equals(const StorageSettingsData &ts) const
{
    return m_addFinalNewLine == ts.m_addFinalNewLine
        && m_cleanWhitespace == ts.m_cleanWhitespace
        && m_inEntireDocument == ts.m_inEntireDocument
        && m_cleanIndentation == ts.m_cleanIndentation
        && m_skipTrailingWhitespace == ts.m_skipTrailingWhitespace
        && m_ignoreFileTypes == ts.m_ignoreFileTypes;
}

StorageSettings &globalStorageSettings()
{
    static StorageSettings theGlobalStorageSettings;
    return theGlobalStorageSettings;
}


void updateGlobalStorageSettings(const StorageSettingsData &newStorageSettings)
{
    if (newStorageSettings.equals(globalStorageSettings().data()))
        return;

    globalStorageSettings().setData(newStorageSettings);
    globalStorageSettings().writeSettings();

    emit TextEditorSettings::instance()->storageSettingsChanged(newStorageSettings);
}

void setupStorageSettings()
{
    globalStorageSettings().readSettings();
}

} // namespace TextEditor
