// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "highlightersettings.h"

#include "highlighterhelper.h"
#include "highlightersettings.h"
#include "texteditorconstants.h"
#include "texteditortr.h"

#include <coreplugin/icore.h>

#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>
#include <utils/qtcsettings.h>

#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>

using namespace Utils;

namespace TextEditor {

const char kDefinitionFilesPath[] = "UserDefinitionFilesPath";
const char kIgnoredFilesPatterns[] = "IgnoredFilesPatterns";

const Key kSettingsGroup{Key("Text") + Key(Constants::HIGHLIGHTER_SETTINGS_CATEGORY)};

void HighlighterSettingsData::toSettings() const
{
    QtcSettings *s = Core::ICore::settings();
    s->beginGroup(kSettingsGroup);
    s->setValue(kDefinitionFilesPath, m_definitionFilesPath.toSettings());
    s->setValue(kIgnoredFilesPatterns, ignoredFilesPatterns());
    s->endGroup();
}

void HighlighterSettingsData::fromSettings()
{
    QtcSettings *s = Core::ICore::settings();
    s->beginGroup(kSettingsGroup);
    m_definitionFilesPath = FilePath::fromSettings(s->value(kDefinitionFilesPath));
    if (!s->contains(kDefinitionFilesPath))
        assignDefaultDefinitionsPath();
    else
        m_definitionFilesPath = FilePath::fromSettings(s->value(kDefinitionFilesPath));
    if (!s->contains(kIgnoredFilesPatterns))
        assignDefaultIgnoredPatterns();
    else
        setIgnoredFilesPatterns(s->value(kIgnoredFilesPatterns, QString()).toString());
    s->endGroup();
}

void HighlighterSettingsData::setIgnoredFilesPatterns(const QString &patterns)
{
    setExpressionsFromList(patterns.split(',', Qt::SkipEmptyParts));
}

QString HighlighterSettingsData::ignoredFilesPatterns() const
{
    return listFromExpressions().join(',');
}

void HighlighterSettingsData::assignDefaultIgnoredPatterns()
{
    setExpressionsFromList({"*.txt",
                            "LICENSE*",
                            "README",
                            "INSTALL",
                            "COPYING",
                            "NEWS",
                            "qmldir"});
}

void HighlighterSettingsData::assignDefaultDefinitionsPath()
{
    const FilePath path = Core::ICore::userResourcePath("generic-highlighter");
    if (path.exists() || path.ensureWritableDir())
        m_definitionFilesPath = path;
}

bool HighlighterSettingsData::isIgnoredFilePattern(const QString &fileName) const
{
    for (const QRegularExpression &regExp : m_ignoredFiles)
        if (fileName.indexOf(regExp) != -1)
            return true;

    return false;
}

bool HighlighterSettingsData::equals(const HighlighterSettingsData &highlighterSettings) const
{
    return m_definitionFilesPath == highlighterSettings.m_definitionFilesPath &&
           m_ignoredFiles == highlighterSettings.m_ignoredFiles;
}

void HighlighterSettingsData::setExpressionsFromList(const QStringList &patterns)
{
    m_ignoredFiles.clear();
    QRegularExpression regExp;
    regExp.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    for (const QString &pattern : patterns) {
        regExp.setPattern(QRegularExpression::wildcardToRegularExpression(pattern));
        m_ignoredFiles.append(regExp);
    }
}

QStringList HighlighterSettingsData::listFromExpressions() const
{
    QStringList patterns;
    for (const QRegularExpression &regExp : m_ignoredFiles)
        patterns.append(regExp.pattern());
    return patterns;
}

class HighlighterSettingsPageWidget;

class HighlighterSettingsPagePrivate
{
public:
    void ensureInitialized()
    {
        if (m_initialized)
            return;
        m_initialized = true;
        m_settings.fromSettings();
    }

    bool m_initialized = false;

    HighlighterSettingsData m_settings;

    QPointer<HighlighterSettingsPageWidget> m_widget;
};

class HighlighterSettingsPageWidget : public Core::IOptionsPageWidget
{
public:
    HighlighterSettingsPageWidget(HighlighterSettingsPagePrivate *d) : d(d)
    {
        d->ensureInitialized();

        auto definitionsInfolabel = new QLabel(this);
        definitionsInfolabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        definitionsInfolabel->setTextFormat(Qt::RichText);
        definitionsInfolabel->setAlignment(Qt::AlignLeading|Qt::AlignLeft|Qt::AlignVCenter);
        definitionsInfolabel->setWordWrap(true);
        definitionsInfolabel->setOpenExternalLinks(true);
        definitionsInfolabel->setText(
            "<html><head/><body><p>"
            + Tr::tr("Highlight definitions are provided by the %1 engine.")
                  .arg(
                      "<a "
                      "href=\"https://invent.kde.org/frameworks/"
                      "syntax-highlighting\">KSyntaxHighlighting</a>")
            + "</p></body></html>");

        auto downloadDefinitions = new QPushButton(Tr::tr("Download Definitions"));
        downloadDefinitions->setToolTip(Tr::tr("Download missing and update existing syntax definition files."));

        auto updateStatus = new QLabel;
        updateStatus->setObjectName("updateStatus");

        m_definitionFilesPath = new PathChooser;
        m_definitionFilesPath->setFilePath(d->m_settings.definitionFilesPath());
        m_definitionFilesPath->setExpectedKind(PathChooser::ExistingDirectory);
        m_definitionFilesPath->setHistoryCompleter("TextEditor.Highlighter.History");

        auto reloadDefinitions = new QPushButton(Tr::tr("Reload Definitions"));
        reloadDefinitions->setToolTip(Tr::tr("Reload externally modified definition files."));

        auto resetCache = new QPushButton(Tr::tr("Reset Remembered Definitions"));
        resetCache->setToolTip(Tr::tr("Reset definitions remembered for files that can be "
                                      "associated with more than one highlighter definition."));

        m_ignoreEdit = new QLineEdit(d->m_settings.ignoredFilesPatterns());

        using namespace Layouting;
        Column {
            definitionsInfolabel,
            Space(3),
            Group {
                title(Tr::tr("Syntax Highlight Definition Files")),
                Column {
                    Row { downloadDefinitions, updateStatus, st },
                    Row { Tr::tr("User Highlight Definition Files"),
                                m_definitionFilesPath, reloadDefinitions },
                    Row { st, resetCache }
                }
            },
            Row { Tr::tr("Ignored file patterns:"), m_ignoreEdit },
            st
        }.attachTo(this);

        connect(downloadDefinitions, &QPushButton::pressed,
                [label = QPointer<QLabel>(updateStatus)]() {
                    HighlighterHelper::downloadDefinitions([label] {
                        if (label)
                            label->setText(Tr::tr("Download finished"));
                    });
                });

        connect(reloadDefinitions, &QPushButton::pressed, this, [] {
            HighlighterHelper::reload();
        });
        connect(resetCache, &QPushButton::clicked, this, [] {
            HighlighterHelper::clearDefinitionForDocumentCache();
        });

        installMarkSettingsDirtyTriggerRecursively(this);
    }

    void apply() final
    {
        bool changed = d->m_settings.definitionFilesPath() != m_definitionFilesPath->filePath()
                    || d->m_settings.ignoredFilesPatterns() != m_ignoreEdit->text();

        if (changed) {
            d->m_settings.setDefinitionFilesPath(m_definitionFilesPath->filePath());
            d->m_settings.setIgnoredFilesPatterns(m_ignoreEdit->text());
            d->m_settings.toSettings();
        }
    }

    PathChooser *m_definitionFilesPath;
    QLineEdit *m_ignoreEdit;
    HighlighterSettingsPagePrivate *d;
};

// HighlighterSettingsPage

HighlighterSettingsPage::HighlighterSettingsPage()
    : d(new HighlighterSettingsPagePrivate)
{
    setId(Constants::TEXT_EDITOR_HIGHLIGHTER_SETTINGS);
    setDisplayName(Tr::tr("Generic Highlighter"));
    setCategory(TextEditor::Constants::TEXT_EDITOR_SETTINGS_CATEGORY);
    setWidgetCreator([this] { return new HighlighterSettingsPageWidget(d); });
}

HighlighterSettingsPage::~HighlighterSettingsPage()
{
    delete d;
}

const HighlighterSettingsData &HighlighterSettingsPage::highlighterSettings() const
{
    d->ensureInitialized();
    return d->m_settings;
}

} // TextEditor
