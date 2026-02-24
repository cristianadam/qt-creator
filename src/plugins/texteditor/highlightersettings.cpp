// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "highlightersettings.h"

#include "highlighterhelper.h"
#include "texteditorconstants.h"
#include "texteditortr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <utils/layoutbuilder.h>

using namespace Utils;

namespace TextEditor {

HighlighterSettings &highlighterSettings()
{
    static HighlighterSettings theHighlighterSettings;
    return theHighlighterSettings;
}

static QString expressionsFromList(const QStringList &patterns)
{
    QStringList list;
    QRegularExpression regExp;
    regExp.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    for (const QString &pattern : patterns) {
        regExp.setPattern(QRegularExpression::wildcardToRegularExpression(pattern));
        list.append(regExp.pattern());
    }
    return list.join(',');
}

HighlighterSettings::HighlighterSettings()
{
    setSettingsGroup(QString("Text") + Constants::HIGHLIGHTER_SETTINGS_CATEGORY);

    definitionFilesPath.setSettingsKey("UserDefinitionFilesPath");
    definitionFilesPath.setExpectedKind(PathChooser::ExistingDirectory);
    definitionFilesPath.setHistoryCompleter("TextEditor.Highlighter.History");
    const FilePath path = Core::ICore::userResourcePath("generic-highlighter");
    if (path.exists() || path.ensureWritableDir())
        definitionFilesPath.setDefaultPathValue(path);

    ignoredFiles.setSettingsKey("IgnoredFilesPatterns");
    ignoredFiles.setLabelText(Tr::tr("Ignored file patterns:"));
    ignoredFiles.setDisplayStyle(StringAspect::LineEditDisplay);
    ignoredFiles.setDefaultValue(expressionsFromList({"*.txt",
                                                       "LICENSE*",
                                                       "README",
                                                       "INSTALL",
                                                       "COPYING",
                                                       "NEWS",
                                                       "qmldir"}));

    setLayouter([this] {

        using namespace Layouting;

        auto definitionsInfolabel = new QLabel;
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

        auto updateStatus = new QLabel;
        updateStatus->setObjectName("updateStatus");

        return Column {
            definitionsInfolabel,
            Space(3),
            Group {
                title(Tr::tr("Syntax Highlight Definition Files")),
                Column {
                    Row {
                        PushButton {
                            text(Tr::tr("Download Definitions")),
                            Layouting::toolTip(Tr::tr("Download missing and update existing syntax definition files.")),
                            onClicked(updateStatus, [label = QPointer<QLabel>(updateStatus)] {
                                HighlighterHelper::downloadDefinitions([label] {
                                    if (label)
                                        label->setText(Tr::tr("Download finished"));
                                });
                            })
                        },
                        updateStatus,
                        st
                    },
                    Row {
                        Tr::tr("User Highlight Definition Files"),
                        definitionFilesPath,
                        PushButton {
                            text(Tr::tr("Reload Definitions")),
                            Layouting::toolTip(Tr::tr("Reload externally modified definition files.")),
                            onClicked(this, &HighlighterHelper::reload)
                        }
                    },
                    Row {
                        st,
                        PushButton {
                            text(Tr::tr("Reset Remembered Definitions")),
                            Layouting::toolTip(Tr::tr("Reset definitions remembered for files that can be "
                                      "associated with more than one highlighter definition.")),
                            onClicked(this, &HighlighterHelper::clearDefinitionForDocumentCache)
                        }
                    }
                },
            },
            Row { ignoredFiles },
            st
        };

    });

    readSettings();
}

bool HighlighterSettings::isIgnoredFilePattern(const QString &fileName) const
{
    const QStringList list = ignoredFiles().split(',', Qt::SkipEmptyParts);

    for (const QString &pattern : list) {
        const QRegularExpression regExp(pattern);
        if (fileName.indexOf(regExp) != -1)
            return true;
    }

    return false;
}

// HighlighterSettingsPage

class HighlighterSettingsPage final : public Core::IOptionsPage
{
public:
    HighlighterSettingsPage()
    {
        setId(Constants::TEXT_EDITOR_HIGHLIGHTER_SETTINGS);
        setDisplayName(Tr::tr("Generic Highlighter"));
        setCategory(TextEditor::Constants::TEXT_EDITOR_SETTINGS_CATEGORY);
        setSettingsProvider([] { return &highlighterSettings(); });
    }
};

const static HighlighterSettingsPage theHighlighterSettingsPage;

} // TextEditor
