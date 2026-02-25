// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "displaysettings.h"

#include "fontsettings.h"
#include "marginsettings.h"
#include "texteditorconstants.h"
#include "texteditorsettings.h"
#include "texteditortr.h"

#include <coreplugin/icore.h>

#include <utils/guiutils.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcsettings.h>
#include <utils/tooltip/tooltip.h>

#include <QApplication>
#include <QCheckBox>
#include <QGroupBox>
#include <QRadioButton>
#include <QSpinBox>

using namespace Utils;

namespace TextEditor {

DisplaySettings &displaySettings()
{
    static DisplaySettings theDisplaySettings;
    return theDisplaySettings;
}

const char displayLineNumbersKey[] = "DisplayLineNumbers";
const char textWrappingKey[] = "TextWrapping";
const char visualizeWhitespaceKey[] = "VisualizeWhitespace";
const char visualizeIndentKey[] = "VisualizeIndent";
const char displayFoldingMarkersKey[] = "DisplayFoldingMarkers";
const char highlightCurrentLineKey[] = "HighlightCurrentLine2Key";
const char highlightBlocksKey[] = "HighlightBlocksKey";
const char animateMatchingParenthesesKey[] = "AnimateMatchingParenthesesKey";
const char highlightMatchingParenthesesKey[] = "HightlightMatchingParenthesesKey";
const char markTextChangesKey[] = "MarkTextChanges";
const char autoFoldFirstCommentKey[] = "AutoFoldFirstComment";
const char centerCursorOnScrollKey[] = "CenterCursorOnScroll";
const char openLinksInNextSplitKey[] = "OpenLinksInNextSplitKey";
const char displayFileEncodingKey[] = "DisplayFileEncoding";
const char displayFileLineEndingKey[] = "DisplayFileLineEnding";
const char displayTabSettingsKey[] = "DisplayTabSettings";
const char scrollBarHighlightsKey[] = "ScrollBarHighlights";
const char animateNavigationWithinFileKey[] = "AnimateNavigationWithinFile";
const char animateWithinFileTimeMaxKey[] = "AnimateWithinFileTimeMax";
const char displayAnnotationsKey[] = "DisplayAnnotations";
const char annotationAlignmentKey[] = "AnnotationAlignment";
const char minimalAnnotationContentKey[] = "MinimalAnnotationContent";
const char highlightSelectionKey[] = "HighlightSelection";
const char displayMinimapKey[] = "DisplayMinimap";
const char groupPostfix[] = "textDisplaySettings";

void DisplaySettings::toSettings(QtcSettings *s) const
{
    s->beginGroup(groupPostfix);
    s->setValue(displayLineNumbersKey, m_displayLineNumbers);
    s->setValue(textWrappingKey, m_textWrapping);
    s->setValue(visualizeWhitespaceKey, m_visualizeWhitespace);
    s->setValue(visualizeIndentKey, m_visualizeIndent);
    s->setValue(displayFoldingMarkersKey, m_displayFoldingMarkers);
    s->setValue(highlightCurrentLineKey, m_highlightCurrentLine);
    s->setValue(highlightBlocksKey, m_highlightBlocks);
    s->setValue(animateMatchingParenthesesKey, m_animateMatchingParentheses);
    s->setValue(highlightMatchingParenthesesKey, m_highlightMatchingParentheses);
    s->setValue(markTextChangesKey, m_markTextChanges);
    s->setValue(autoFoldFirstCommentKey, m_autoFoldFirstComment);
    s->setValue(centerCursorOnScrollKey, m_centerCursorOnScroll);
    s->setValue(openLinksInNextSplitKey, m_openLinksInNextSplit);
    s->setValue(displayFileEncodingKey, m_displayFileEncoding);
    s->setValue(displayFileLineEndingKey, m_displayFileLineEnding);
    s->setValue(displayTabSettingsKey, m_displayTabSettings);
    s->setValue(scrollBarHighlightsKey, m_scrollBarHighlights);
    s->setValue(animateNavigationWithinFileKey, m_animateNavigationWithinFile);
    s->setValue(displayAnnotationsKey, m_displayAnnotations);
    s->setValue(annotationAlignmentKey, static_cast<int>(m_annotationAlignment));
    s->setValue(highlightSelectionKey, m_highlightSelection);
    s->setValue(displayMinimapKey, m_displayMinimap);
    s->endGroup();
}

void DisplaySettings::fromSettings(QtcSettings *s)
{
    s->beginGroup(groupPostfix);
    *this = DisplaySettings(); // Assign defaults

    m_displayLineNumbers = s->value(displayLineNumbersKey, m_displayLineNumbers).toBool();
    m_textWrapping = s->value(textWrappingKey, m_textWrapping).toBool();
    m_visualizeWhitespace = s->value(visualizeWhitespaceKey, m_visualizeWhitespace).toBool();
    m_visualizeIndent = s->value(visualizeIndentKey, m_visualizeIndent).toBool();
    m_displayFoldingMarkers = s->value(displayFoldingMarkersKey, m_displayFoldingMarkers).toBool();
    m_highlightCurrentLine = s->value(highlightCurrentLineKey, m_highlightCurrentLine).toBool();
    m_highlightBlocks = s->value(highlightBlocksKey, m_highlightBlocks).toBool();
    m_animateMatchingParentheses = s->value(animateMatchingParenthesesKey, m_animateMatchingParentheses).toBool();
    m_highlightMatchingParentheses = s->value(highlightMatchingParenthesesKey, m_highlightMatchingParentheses).toBool();
    m_markTextChanges = s->value(markTextChangesKey, m_markTextChanges).toBool();
    m_autoFoldFirstComment = s->value(autoFoldFirstCommentKey, m_autoFoldFirstComment).toBool();
    m_centerCursorOnScroll = s->value(centerCursorOnScrollKey, m_centerCursorOnScroll).toBool();
    m_openLinksInNextSplit = s->value(openLinksInNextSplitKey, m_openLinksInNextSplit).toBool();
    m_displayFileEncoding = s->value(displayFileEncodingKey, m_displayFileEncoding).toBool();
    m_displayFileLineEnding = s->value(displayFileLineEndingKey, m_displayFileLineEnding).toBool();
    m_displayTabSettings = s->value(displayTabSettingsKey, m_displayTabSettings).toBool();
    m_scrollBarHighlights = s->value(scrollBarHighlightsKey, m_scrollBarHighlights).toBool();
    m_animateNavigationWithinFile = s->value(animateNavigationWithinFileKey, m_animateNavigationWithinFile).toBool();
    m_animateWithinFileTimeMax = s->value(animateWithinFileTimeMaxKey, m_animateWithinFileTimeMax).toInt();
    m_displayAnnotations = s->value(displayAnnotationsKey, m_displayAnnotations).toBool();
    m_annotationAlignment = static_cast<TextEditor::AnnotationAlignment>(
                s->value(annotationAlignmentKey,
                         static_cast<int>(m_annotationAlignment)).toInt());
    m_minimalAnnotationContent = s->value(minimalAnnotationContentKey, m_minimalAnnotationContent).toInt();
    m_highlightSelection = s->value(highlightSelectionKey, m_highlightSelection).toBool();
    m_displayMinimap = s->value(displayMinimapKey, m_displayMinimap).toBool();
    s->endGroup();
}

bool DisplaySettings::equals(const DisplaySettings &ds) const
{
    return m_displayLineNumbers == ds.m_displayLineNumbers
        && m_textWrapping == ds.m_textWrapping
        && m_visualizeWhitespace == ds.m_visualizeWhitespace
        && m_visualizeIndent == ds.m_visualizeIndent
        && m_displayFoldingMarkers == ds.m_displayFoldingMarkers
        && m_highlightCurrentLine == ds.m_highlightCurrentLine
        && m_highlightBlocks == ds.m_highlightBlocks
        && m_animateMatchingParentheses == ds.m_animateMatchingParentheses
        && m_highlightMatchingParentheses == ds.m_highlightMatchingParentheses
        && m_markTextChanges == ds.m_markTextChanges
        && m_autoFoldFirstComment== ds.m_autoFoldFirstComment
        && m_centerCursorOnScroll == ds.m_centerCursorOnScroll
        && m_openLinksInNextSplit == ds.m_openLinksInNextSplit
        && m_forceOpenLinksInNextSplit == ds.m_forceOpenLinksInNextSplit
        && m_displayFileEncoding == ds.m_displayFileEncoding
        && m_displayFileLineEnding == ds.m_displayFileLineEnding
        && m_displayTabSettings == ds.m_displayTabSettings
        && m_scrollBarHighlights == ds.m_scrollBarHighlights
        && m_animateNavigationWithinFile == ds.m_animateNavigationWithinFile
        && m_animateWithinFileTimeMax == ds.m_animateWithinFileTimeMax
        && m_displayAnnotations == ds.m_displayAnnotations
        && m_annotationAlignment == ds.m_annotationAlignment
        && m_minimalAnnotationContent == ds.m_minimalAnnotationContent
        && m_highlightSelection == ds.m_highlightSelection
        && m_displayMinimap == ds.m_displayMinimap
            ;
}

QLabel *DisplaySettings::createAnnotationSettingsLink()
{
    auto label = new QLabel("<small><i><a href>Annotation Settings</a></i></small>");
    QObject::connect(label, &QLabel::linkActivated, []() {
        Utils::ToolTip::hideImmediately();
        Core::ICore::showSettings(Constants::TEXT_EDITOR_DISPLAY_SETTINGS);
    });
    return label;
}


class DisplaySettingsPagePrivate
{
public:
    DisplaySettingsPagePrivate();

    DisplaySettings m_displaySettings;
};

DisplaySettingsPagePrivate::DisplaySettingsPagePrivate()
{
    m_displaySettings.fromSettings(Core::ICore::settings());
}

class DisplaySettingsWidget final : public Core::IOptionsPageWidget
{
public:
    DisplaySettingsWidget()
    {
        enableTextWrapping = new QCheckBox(Tr::tr("Enable text &wrapping"));

        enableTextWrappingHintLabel = new QLabel(Tr::tr("<i>Set <a href=\"font zoom\">font line spacing</a> "
                                                    "to 100% to enable text wrapping option.</i>"));

        auto updateWrapping = [this] {
            const bool normalLineSpacing = TextEditorSettings::fontSettings().relativeLineSpacing() == 100;
            if (!normalLineSpacing)
                enableTextWrapping->setChecked(false);
            enableTextWrapping->setEnabled(normalLineSpacing);
            enableTextWrappingHintLabel->setVisible(!normalLineSpacing);
        };

        updateWrapping();

        connect(TextEditorSettings::instance(), &TextEditorSettings::fontSettingsChanged,
                this, updateWrapping);

        connect(enableTextWrappingHintLabel, &QLabel::linkActivated, [] {
            Core::ICore::showSettings(Constants::TEXT_EDITOR_FONT_SETTINGS); } );


        animateMatchingParentheses = new QCheckBox(Tr::tr("&Animate matching parentheses"));
        scrollBarHighlights = new QCheckBox(Tr::tr("Highlight search results on the scrollbar"));
        displayLineNumbers = new QCheckBox(Tr::tr("Display line &numbers"));
        animateNavigationWithinFile = new QCheckBox(Tr::tr("Animate navigation within file"));
        highlightCurrentLine = new QCheckBox(Tr::tr("Highlight current &line"));
        highlightBlocks = new QCheckBox(Tr::tr("Highlight &blocks"));
        markTextChanges = new QCheckBox(Tr::tr("Mark &text changes"));
        autoFoldFirstComment = new QCheckBox(Tr::tr("Auto-fold first &comment"));
        displayFoldingMarkers = new QCheckBox(Tr::tr("Display &folding markers"));
        centerOnScroll = new QCheckBox(Tr::tr("Center &cursor on scroll"));
        visualizeIndent = new QCheckBox(Tr::tr("Visualize indent"));
        displayFileLineEnding = new QCheckBox(Tr::tr("Display file line ending"));
        displayFileEncoding = new QCheckBox(Tr::tr("Display file encoding"));
        displayTabSettings = new QCheckBox(Tr::tr("Display tab settings"));
        openLinksInNextSplit = new QCheckBox(Tr::tr("Always open links in another split"));
        highlightMatchingParentheses = new QCheckBox(Tr::tr("&Highlight matching parentheses"));

        visualizeWhitespace = new QCheckBox(Tr::tr("&Visualize whitespace"));
        visualizeWhitespace->setToolTip(Tr::tr("Shows tabs and spaces."));

        highlightSelection = new QCheckBox(Tr::tr("&Highlight selection"));
        highlightSelection->setToolTip(Tr::tr("Adds a colored background and a marker to the "
                                              "scrollbar to occurrences of the selected text."));

        leftAligned = new QRadioButton(Tr::tr("Next to editor content"));
        atMargin = new QRadioButton(Tr::tr("Next to right margin"));
        rightAligned = new QRadioButton(Tr::tr("Aligned at right side"));
        rightAligned->setChecked(true);
        betweenLines = new QRadioButton(Tr::tr("Between lines"));

        displayAnnotations = new QGroupBox(Tr::tr("Line Annotations")),
        displayAnnotations->setCheckable(true);

        enableMinimap = new QCheckBox(Tr::tr("Enable minimap"));

        using namespace Layouting;

        Column {
            leftAligned,
            atMargin,
            rightAligned,
            betweenLines,
        }.attachTo(displayAnnotations);

        MarginSettings &m = marginSettings();
        Column {
            Group {
                title(Tr::tr("Margin")),
                Column {
                    Row { m.showMargin, m.marginColumn, st },
                    Row { m.useIndenter, m.tintMarginArea, st },
                    Row { m.centerEditorContentWidthPercent, st }
                }
            },
            Group {
                title(Tr::tr("Wrapping")),
                Column {
                    enableTextWrapping,
                    Row { enableTextWrappingHintLabel, st}
                }
            },
            Group {
                title(Tr::tr("Display")),
                Row {
                    Column {
                        displayLineNumbers,
                        displayFoldingMarkers,
                        markTextChanges,
                        visualizeWhitespace,
                        centerOnScroll,
                        autoFoldFirstComment,
                        scrollBarHighlights,
                        animateNavigationWithinFile,
                        highlightSelection,
                    },
                    Column {
                        highlightCurrentLine,
                        highlightBlocks,
                        animateMatchingParentheses,
                        visualizeIndent,
                        highlightMatchingParentheses,
                        openLinksInNextSplit,
                        displayFileEncoding,
                        displayFileLineEnding,
                        displayTabSettings,
                        enableMinimap,
                    }
                }
            },
            displayAnnotations,

            st
        }.attachTo(this);

        settingsToUI();

        Utils::installMarkSettingsDirtyTriggerRecursively(this);
    }

    void apply() final;

    void settingsFromUI(DisplaySettings &displaySettings) const;
    void settingsToUI();
    void setDisplaySettings(const DisplaySettings &);

    QCheckBox *enableTextWrapping;
    QLabel *enableTextWrappingHintLabel;
    QCheckBox *animateMatchingParentheses;
    QCheckBox *scrollBarHighlights;
    QCheckBox *displayLineNumbers;
    QCheckBox *animateNavigationWithinFile;
    QCheckBox *highlightCurrentLine;
    QCheckBox *highlightBlocks;
    QCheckBox *markTextChanges;
    QCheckBox *autoFoldFirstComment;
    QCheckBox *displayFoldingMarkers;
    QCheckBox *centerOnScroll;
    QCheckBox *visualizeIndent;
    QCheckBox *displayFileLineEnding;
    QCheckBox *displayFileEncoding;
    QCheckBox *displayTabSettings;
    QCheckBox *openLinksInNextSplit;
    QCheckBox *highlightMatchingParentheses;
    QCheckBox *visualizeWhitespace;
    QCheckBox *highlightSelection;
    QGroupBox *displayAnnotations;
    QRadioButton *leftAligned;
    QRadioButton *atMargin;
    QRadioButton *rightAligned;
    QRadioButton *betweenLines;
    QCheckBox *enableMinimap;
};

void DisplaySettingsWidget::apply()
{
    marginSettings().apply();

    DisplaySettings newDisplaySettings;
    settingsFromUI(newDisplaySettings);
    setDisplaySettings(newDisplaySettings);
}

void DisplaySettingsWidget::settingsFromUI(DisplaySettings &displaySettings) const
{
    displaySettings.m_displayLineNumbers = displayLineNumbers->isChecked();
    displaySettings.m_textWrapping = enableTextWrapping->isChecked();
    if (TextEditorSettings::fontSettings().relativeLineSpacing() != 100)
        displaySettings.m_textWrapping = false;
    displaySettings.m_visualizeWhitespace = visualizeWhitespace->isChecked();
    displaySettings.m_visualizeIndent = visualizeIndent->isChecked();
    displaySettings.m_displayFoldingMarkers = displayFoldingMarkers->isChecked();
    displaySettings.m_highlightCurrentLine = highlightCurrentLine->isChecked();
    displaySettings.m_highlightBlocks = highlightBlocks->isChecked();
    displaySettings.m_animateMatchingParentheses = animateMatchingParentheses->isChecked();
    displaySettings.m_highlightMatchingParentheses = highlightMatchingParentheses->isChecked();
    displaySettings.m_markTextChanges = markTextChanges->isChecked();
    displaySettings.m_autoFoldFirstComment = autoFoldFirstComment->isChecked();
    displaySettings.m_centerCursorOnScroll = centerOnScroll->isChecked();
    displaySettings.m_openLinksInNextSplit = openLinksInNextSplit->isChecked();
    displaySettings.m_displayFileEncoding = displayFileEncoding->isChecked();
    displaySettings.m_displayTabSettings = displayTabSettings->isChecked();
    displaySettings.m_displayFileLineEnding = displayFileLineEnding->isChecked();
    displaySettings.m_scrollBarHighlights = scrollBarHighlights->isChecked();
    displaySettings.m_animateNavigationWithinFile = animateNavigationWithinFile->isChecked();
    displaySettings.m_displayAnnotations = displayAnnotations->isChecked();
    displaySettings.m_highlightSelection = highlightSelection->isChecked();
    if (leftAligned->isChecked())
        displaySettings.m_annotationAlignment = AnnotationAlignment::NextToContent;
    else if (atMargin->isChecked())
        displaySettings.m_annotationAlignment = AnnotationAlignment::NextToMargin;
    else if (rightAligned->isChecked())
        displaySettings.m_annotationAlignment = AnnotationAlignment::RightSide;
    else if (betweenLines->isChecked())
        displaySettings.m_annotationAlignment = AnnotationAlignment::BetweenLines;
    displaySettings.m_displayMinimap = enableMinimap->isChecked();
}

void DisplaySettingsWidget::settingsToUI()
{
    const DisplaySettings &displaySettings = TextEditor::displaySettings();

    displayLineNumbers->setChecked(displaySettings.m_displayLineNumbers);
    enableTextWrapping->setChecked(displaySettings.m_textWrapping);
    visualizeWhitespace->setChecked(displaySettings.m_visualizeWhitespace);
    visualizeIndent->setChecked(displaySettings.m_visualizeIndent);
    displayFoldingMarkers->setChecked(displaySettings.m_displayFoldingMarkers);
    highlightCurrentLine->setChecked(displaySettings.m_highlightCurrentLine);
    highlightBlocks->setChecked(displaySettings.m_highlightBlocks);
    animateMatchingParentheses->setChecked(displaySettings.m_animateMatchingParentheses);
    highlightMatchingParentheses->setChecked(displaySettings.m_highlightMatchingParentheses);
    markTextChanges->setChecked(displaySettings.m_markTextChanges);
    autoFoldFirstComment->setChecked(displaySettings.m_autoFoldFirstComment);
    centerOnScroll->setChecked(displaySettings.m_centerCursorOnScroll);
    openLinksInNextSplit->setChecked(displaySettings.m_openLinksInNextSplit);
    displayFileEncoding->setChecked(displaySettings.m_displayFileEncoding);
    displayFileLineEnding->setChecked(displaySettings.m_displayFileLineEnding);
    displayTabSettings->setChecked(displaySettings.m_displayTabSettings);
    scrollBarHighlights->setChecked(displaySettings.m_scrollBarHighlights);
    animateNavigationWithinFile->setChecked(displaySettings.m_animateNavigationWithinFile);
    displayAnnotations->setChecked(displaySettings.m_displayAnnotations);
    highlightSelection->setChecked(displaySettings.m_highlightSelection);
    switch (displaySettings.m_annotationAlignment) {
    case AnnotationAlignment::NextToContent: leftAligned->setChecked(true); break;
    case AnnotationAlignment::NextToMargin: atMargin->setChecked(true); break;
    case AnnotationAlignment::RightSide: rightAligned->setChecked(true); break;
    case AnnotationAlignment::BetweenLines: betweenLines->setChecked(true); break;
    }
    enableMinimap->setChecked(displaySettings.m_displayMinimap);
}

void DisplaySettingsWidget::setDisplaySettings(const DisplaySettings &newDisplaySettings)
{
    if (newDisplaySettings != displaySettings()) {
        displaySettings() = newDisplaySettings;
        displaySettings().toSettings(Core::ICore::settings());

        emit TextEditorSettings::instance()->displaySettingsChanged(newDisplaySettings);
    }
}

class DisplaySettingsPage final : public Core::IOptionsPage
{
public:
    DisplaySettingsPage()
    {
        setId(Constants::TEXT_EDITOR_DISPLAY_SETTINGS);
        setDisplayName(Tr::tr("Display"));
        setCategory(TextEditor::Constants::TEXT_EDITOR_SETTINGS_CATEGORY);
        setWidgetCreator([] { return new DisplaySettingsWidget; });
    }
};

void Internal::setupDisplaySettings()
{
    static DisplaySettingsPage theDisplaySettings;

    displaySettings().fromSettings(Core::ICore::settings());
}

} // TextEditor
