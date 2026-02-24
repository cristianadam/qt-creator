// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/filepath.h>

#include <QString>
#include <QStringList>
#include <QList>
#include <QRegularExpression>

namespace TextEditor {

class HighlighterSettingsData final
{
public:
    HighlighterSettingsData() = default;

    void toSettings() const;
    void fromSettings();

    void setDefinitionFilesPath(const Utils::FilePath &path) { m_definitionFilesPath = path; }
    const Utils::FilePath &definitionFilesPath() const { return m_definitionFilesPath; }

    void setIgnoredFilesPatterns(const QString &patterns);
    QString ignoredFilesPatterns() const;
    bool isIgnoredFilePattern(const QString &fileName) const;

    bool equals(const HighlighterSettingsData &highlighterSettings) const;

    friend bool operator==(const HighlighterSettingsData &a, const HighlighterSettingsData &b)
    { return a.equals(b); }

    friend bool operator!=(const HighlighterSettingsData &a, const HighlighterSettingsData &b)
    { return !a.equals(b); }

private:
    void assignDefaultIgnoredPatterns();
    void assignDefaultDefinitionsPath();

    void setExpressionsFromList(const QStringList &patterns);
    QStringList listFromExpressions() const;

    Utils::FilePath m_definitionFilesPath;
    QList<QRegularExpression> m_ignoredFiles;
};


class HighlighterSettingsPage final : public Core::IOptionsPage
{
public:
    HighlighterSettingsPage();
    ~HighlighterSettingsPage() override;

    const HighlighterSettingsData &highlighterSettings() const;

private:
    class HighlighterSettingsPagePrivate *d;
};

} // namespace TextEditor
