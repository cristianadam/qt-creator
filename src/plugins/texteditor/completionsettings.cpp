// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "completionsettings.h"

#include "texteditorsettings.h"
#include "texteditorconstants.h"
#include "texteditortr.h"

#include <cppeditor/cpptoolssettings.h>

#include <coreplugin/icore.h>

#include <utils/qtcsettings.h>
#include <utils/layoutbuilder.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QSpacerItem>
#include <QSpinBox>
#include <QWidget>

using namespace CppEditor;
using namespace Utils;

namespace TextEditor {

CompletionSettings &completionSettings()
{
    static CompletionSettings theCompletionSettings;
    return theCompletionSettings;
}

const char settingsGroup[]               = "CppTools/Completion";
const char caseSensitivityKey[]          = "CaseSensitivity";
const char completionTriggerKey[]        = "CompletionTrigger";
const char automaticProposalTimeoutKey[] = "AutomaticProposalTimeout";
const char characterThresholdKey[]       = "CharacterThreshold";
const char autoInsertBracesKey[]         = "AutoInsertBraces";
const char surroundingAutoBracketsKey[]  = "SurroundingAutoBrackets";
const char autoInsertQuotesKey[]         = "AutoInsertQuotes";
const char surroundingAutoQuotesKey[]    = "SurroundingAutoQuotes";
const char partiallyCompleteKey[]        = "PartiallyComplete";
const char spaceAfterFunctionNameKey[]   = "SpaceAfterFunctionName";
const char autoSplitStringsKey[]         = "AutoSplitStrings";
const char animateAutoCompleteKey[]      = "AnimateAutoComplete";
const char highlightAutoCompleteKey[]    = "HighlightAutoComplete";
const char skipAutoCompleteKey[]         = "SkipAutoComplete";
const char autoRemoveKey[]               = "AutoRemove";
const char overwriteClosingCharsKey[]    = "OverwriteClosingChars";

void CompletionSettings::toSettings(QtcSettings *s) const
{
    s->beginGroup(settingsGroup);
    s->setValue(caseSensitivityKey, (int) m_caseSensitivity);
    s->setValue(completionTriggerKey, (int) m_completionTrigger);
    s->setValue(automaticProposalTimeoutKey, m_automaticProposalTimeoutInMs);
    s->setValue(characterThresholdKey, m_characterThreshold);
    s->setValue(autoInsertBracesKey, m_autoInsertBrackets);
    s->setValue(surroundingAutoBracketsKey, m_surroundingAutoBrackets);
    s->setValue(autoInsertQuotesKey, m_autoInsertQuotes);
    s->setValue(surroundingAutoQuotesKey, m_surroundingAutoQuotes);
    s->setValue(partiallyCompleteKey, m_partiallyComplete);
    s->setValue(spaceAfterFunctionNameKey, m_spaceAfterFunctionName);
    s->setValue(autoSplitStringsKey, m_autoSplitStrings);
    s->setValue(animateAutoCompleteKey, m_animateAutoComplete);
    s->setValue(highlightAutoCompleteKey, m_highlightAutoComplete);
    s->setValue(skipAutoCompleteKey, m_skipAutoCompletedText);
    s->setValue(autoRemoveKey, m_autoRemove);
    s->setValue(overwriteClosingCharsKey, m_overwriteClosingChars);
    s->endGroup();
}

void CompletionSettings::fromSettings(QtcSettings *s)
{
    *this = CompletionSettings(); // Assign defaults

    s->beginGroup(settingsGroup);
    m_caseSensitivity = (CaseSensitivity)
            s->value(caseSensitivityKey, m_caseSensitivity).toInt();
    m_completionTrigger = (CompletionTrigger)
            s->value(completionTriggerKey, m_completionTrigger).toInt();
    m_automaticProposalTimeoutInMs =
            s->value(automaticProposalTimeoutKey, m_automaticProposalTimeoutInMs).toInt();
    m_characterThreshold =
            s->value(characterThresholdKey, m_characterThreshold).toInt();
    m_autoInsertBrackets =
            s->value(autoInsertBracesKey, m_autoInsertBrackets).toBool();
    m_surroundingAutoBrackets =
            s->value(surroundingAutoBracketsKey, m_surroundingAutoBrackets).toBool();
    m_autoInsertQuotes =
            s->value(autoInsertQuotesKey, m_autoInsertQuotes).toBool();
    m_surroundingAutoQuotes =
            s->value(surroundingAutoQuotesKey, m_surroundingAutoQuotes).toBool();
    m_partiallyComplete =
            s->value(partiallyCompleteKey, m_partiallyComplete).toBool();
    m_spaceAfterFunctionName =
            s->value(spaceAfterFunctionNameKey, m_spaceAfterFunctionName).toBool();
    m_autoSplitStrings =
            s->value(autoSplitStringsKey, m_autoSplitStrings).toBool();
    m_animateAutoComplete =
            s->value(animateAutoCompleteKey, m_animateAutoComplete).toBool();
    m_highlightAutoComplete =
            s->value(highlightAutoCompleteKey, m_highlightAutoComplete).toBool();
    m_skipAutoCompletedText =
            s->value(skipAutoCompleteKey, m_skipAutoCompletedText).toBool();
    m_autoRemove =
            s->value(autoRemoveKey, m_autoRemove).toBool();
    m_overwriteClosingChars =
            s->value(overwriteClosingCharsKey, m_overwriteClosingChars).toBool();
    s->endGroup();
}

bool CompletionSettings::equals(const CompletionSettings &cs) const
{
    return m_caseSensitivity                == cs.m_caseSensitivity
        && m_completionTrigger              == cs.m_completionTrigger
        && m_automaticProposalTimeoutInMs   == cs.m_automaticProposalTimeoutInMs
        && m_characterThreshold             == cs.m_characterThreshold
        && m_autoInsertBrackets             == cs.m_autoInsertBrackets
        && m_surroundingAutoBrackets        == cs.m_surroundingAutoBrackets
        && m_autoInsertQuotes               == cs.m_autoInsertQuotes
        && m_surroundingAutoQuotes          == cs.m_surroundingAutoQuotes
        && m_partiallyComplete              == cs.m_partiallyComplete
        && m_spaceAfterFunctionName         == cs.m_spaceAfterFunctionName
        && m_autoSplitStrings               == cs.m_autoSplitStrings
        && m_animateAutoComplete            == cs.m_animateAutoComplete
        && m_highlightAutoComplete          == cs.m_highlightAutoComplete
        && m_skipAutoCompletedText          == cs.m_skipAutoCompletedText
        && m_autoRemove                     == cs.m_autoRemove
        && m_overwriteClosingChars          == cs.m_overwriteClosingChars
        ;
}

namespace Internal {

class CompletionSettingsPageWidget final : public Core::IOptionsPageWidget
{
public:
    CompletionSettingsPageWidget();

private:
    void apply() final;

    CaseSensitivity caseSensitivity() const;
    CompletionTrigger completionTrigger() const;
    void settingsFromUi(CompletionSettings &completion) const;

    QComboBox *m_caseSensitivity;
    QComboBox *m_completionTrigger;
    QSpinBox *m_thresholdSpinBox;
    QSpinBox *m_automaticProposalTimeoutSpinBox;
    QCheckBox *m_partiallyComplete;
    QCheckBox *m_autoSplitStrings;
    QCheckBox *m_insertBrackets;
    QCheckBox *m_insertQuotes;
    QCheckBox *m_surroundBrackets;
    QCheckBox *m_spaceAfterFunctionName;
    QCheckBox *m_surroundQuotes;
    QCheckBox *m_animateAutoComplete;
    QCheckBox *m_highlightAutoComplete;
    QCheckBox *m_skipAutoComplete;
    QCheckBox *m_removeAutoComplete;
    QCheckBox *m_overwriteClosingChars;
};

CompletionSettingsPageWidget::CompletionSettingsPageWidget()
{
    CompletionSettings &s = completionSettings();

    m_caseSensitivity = new QComboBox;
    m_caseSensitivity->addItem(Tr::tr("Full"));
    m_caseSensitivity->addItem(Tr::tr("None", "Case-sensitivity: None"));
    m_caseSensitivity->addItem(Tr::tr("First Letter"));

    auto caseSensitivityLabel = new QLabel(Tr::tr("&Case-sensitivity:"));
    caseSensitivityLabel->setBuddy(m_caseSensitivity);

    m_completionTrigger = new QComboBox;
    m_completionTrigger->addItem(Tr::tr("Manually"));
    m_completionTrigger->addItem(Tr::tr("When Triggered"));
    m_completionTrigger->addItem(Tr::tr("Always"));

    auto completionTriggerLabel = new QLabel(Tr::tr("Activate completion:"));

    auto automaticProposalTimeoutLabel = new QLabel(Tr::tr("Timeout in ms:"));

    m_automaticProposalTimeoutSpinBox = new QSpinBox;
    m_automaticProposalTimeoutSpinBox->setMaximum(2000);
    m_automaticProposalTimeoutSpinBox->setSingleStep(50);
    m_automaticProposalTimeoutSpinBox->setValue(400);

    auto thresholdLabel = new QLabel(Tr::tr("Character threshold:"));

    m_thresholdSpinBox = new QSpinBox;
    m_thresholdSpinBox->setMinimum(1);

    m_partiallyComplete = new QCheckBox(Tr::tr("Autocomplete common &prefix"));
    m_partiallyComplete->setToolTip(Tr::tr("Inserts the common prefix of available completion items."));
    m_partiallyComplete->setChecked(true);

    m_autoSplitStrings = new QCheckBox(Tr::tr("Automatically split strings"));
    m_autoSplitStrings->setToolTip(
        Tr::tr("Splits a string into two lines by adding an end quote at the cursor position "
           "when you press Enter and a start quote to the next line, before the rest "
           "of the string.\n\n"
           "In addition, Shift+Enter inserts an escape character at the cursor position "
           "and moves the rest of the string to the next line."));

    m_insertBrackets = new QCheckBox(Tr::tr("Insert opening or closing brackets"));
    m_insertBrackets->setChecked(true);

    m_insertQuotes = new QCheckBox(Tr::tr("Insert closing quote"));
    m_insertQuotes->setChecked(true);

    m_surroundBrackets = new QCheckBox(Tr::tr("Surround text selection with brackets"));
    m_surroundBrackets->setChecked(true);
    m_surroundBrackets->setToolTip(
        Tr::tr("When typing a matching bracket and there is a text selection, instead of "
           "removing the selection, surrounds it with the corresponding characters."));

    m_spaceAfterFunctionName = new QCheckBox(Tr::tr("Insert &space after function name"));
    m_spaceAfterFunctionName->setEnabled(true);

    m_surroundQuotes = new QCheckBox(Tr::tr("Surround text selection with quotes"));
    m_surroundQuotes->setChecked(true);
    m_surroundQuotes->setToolTip(
        Tr::tr("When typing a matching quote and there is a text selection, instead of "
           "removing the selection, surrounds it with the corresponding characters."));

    m_animateAutoComplete = new QCheckBox(Tr::tr("Animate automatically inserted text"));
    m_animateAutoComplete->setChecked(true);
    m_animateAutoComplete->setToolTip(Tr::tr("Show a visual hint when for example a brace or a quote "
                                       "is automatically inserted by the editor."));

    m_highlightAutoComplete = new QCheckBox(Tr::tr("Highlight automatically inserted text"));
    m_highlightAutoComplete->setChecked(true);

    m_skipAutoComplete = new QCheckBox(Tr::tr("Skip automatically inserted character when typing"));
    m_skipAutoComplete->setToolTip(Tr::tr("Skip automatically inserted character if re-typed manually "
                                    "after completion or by pressing tab."));
    m_skipAutoComplete->setChecked(true);

    m_removeAutoComplete = new QCheckBox(Tr::tr("Remove automatically inserted text on backspace"));
    m_removeAutoComplete->setChecked(true);
    m_removeAutoComplete->setToolTip(Tr::tr("Remove the automatically inserted character if the trigger "
                                      "is deleted by backspace after the completion."));

    m_overwriteClosingChars = new QCheckBox(Tr::tr("Overwrite closing punctuation"));
    m_overwriteClosingChars->setToolTip(Tr::tr("Automatically overwrite closing parentheses and quotes."));

    connect(m_completionTrigger, &QComboBox::currentIndexChanged,
            this, [this, automaticProposalTimeoutLabel] {
        const bool enableTimeoutWidgets = completionTrigger() == AutomaticCompletion;
        automaticProposalTimeoutLabel->setEnabled(enableTimeoutWidgets);
        m_automaticProposalTimeoutSpinBox->setEnabled(enableTimeoutWidgets);
    });

    int caseSensitivityIndex = 0;
    switch (s.m_caseSensitivity) {
    case TextEditor::CaseSensitive:
        caseSensitivityIndex = 0;
        break;
    case TextEditor::CaseInsensitive:
        caseSensitivityIndex = 1;
        break;
    case TextEditor::FirstLetterCaseSensitive:
        caseSensitivityIndex = 2;
        break;
    }

    int completionTriggerIndex = 0;
    switch (s.m_completionTrigger) {
    case TextEditor::ManualCompletion:
        completionTriggerIndex = 0;
        break;
    case TextEditor::TriggeredCompletion:
        completionTriggerIndex = 1;
        break;
    case TextEditor::AutomaticCompletion:
        completionTriggerIndex = 2;
        break;
    }

    m_caseSensitivity->setCurrentIndex(caseSensitivityIndex);
    m_completionTrigger->setCurrentIndex(completionTriggerIndex);
    m_automaticProposalTimeoutSpinBox
            ->setValue(s.m_automaticProposalTimeoutInMs);
    m_thresholdSpinBox->setValue(s.m_characterThreshold);
    m_insertBrackets->setChecked(s.m_autoInsertBrackets);
    m_surroundBrackets->setChecked(s.m_surroundingAutoBrackets);
    m_insertQuotes->setChecked(s.m_autoInsertQuotes);
    m_surroundQuotes->setChecked(s.m_surroundingAutoQuotes);
    m_partiallyComplete->setChecked(s.m_partiallyComplete);
    m_spaceAfterFunctionName->setChecked(s.m_spaceAfterFunctionName);
    m_autoSplitStrings->setChecked(s.m_autoSplitStrings);
    m_animateAutoComplete->setChecked(s.m_animateAutoComplete);
    m_overwriteClosingChars->setChecked(s.m_overwriteClosingChars);
    m_highlightAutoComplete->setChecked(s.m_highlightAutoComplete);
    m_skipAutoComplete->setChecked(s.m_skipAutoCompletedText);
    m_removeAutoComplete->setChecked(s.m_autoRemove);

    m_skipAutoComplete->setEnabled(m_highlightAutoComplete->isChecked());
    m_removeAutoComplete->setEnabled(m_highlightAutoComplete->isChecked());

    using namespace Layouting;
    auto indent = [](QWidget *widget) { return Row { Space(30), widget }; };

    Column {
        Group {
            title(Tr::tr("Behavior")),
            Form {
                caseSensitivityLabel, m_caseSensitivity, st, br,
                completionTriggerLabel, m_completionTrigger, st, br,
                automaticProposalTimeoutLabel, m_automaticProposalTimeoutSpinBox, st, br,
                thresholdLabel, m_thresholdSpinBox, st, br,
                Span(2, m_partiallyComplete), br,
                Span(2, m_autoSplitStrings), br,
            }
        },
        Group {
            title(Tr::tr("&Automatically Insert Matching Characters")),
            Row {
                Column {
                    m_insertBrackets,
                    m_surroundBrackets,
                    m_spaceAfterFunctionName,
                    m_highlightAutoComplete,
                        indent(m_skipAutoComplete),
                        indent(m_removeAutoComplete)
                },
                Column {
                    m_insertQuotes,
                    m_surroundQuotes,
                    m_animateAutoComplete,
                    m_overwriteClosingChars,
                    st,
                }
            }
        },
        st
    }.attachTo(this);

    connect(m_highlightAutoComplete, &QCheckBox::toggled, m_skipAutoComplete, &QCheckBox::setEnabled);
    connect(m_highlightAutoComplete, &QCheckBox::toggled, m_removeAutoComplete, &QCheckBox::setEnabled);

    installMarkSettingsDirtyTriggerRecursively(this);
}

void CompletionSettingsPageWidget::apply()
{
    CompletionSettings settings;

    settingsFromUi(settings);

    if (completionSettings() != settings) {
        completionSettings() = settings;
        completionSettings().toSettings(Core::ICore::settings());
        emit TextEditorSettings::instance()->completionSettingsChanged(settings);
    }
}

CaseSensitivity CompletionSettingsPageWidget::caseSensitivity() const
{
    switch (m_caseSensitivity->currentIndex()) {
    case 0: // Full
        return TextEditor::CaseSensitive;
    case 1: // None
        return TextEditor::CaseInsensitive;
    default: // First letter
        return TextEditor::FirstLetterCaseSensitive;
    }
}

CompletionTrigger CompletionSettingsPageWidget::completionTrigger() const
{
    switch (m_completionTrigger->currentIndex()) {
    case 0:
        return TextEditor::ManualCompletion;
    case 1:
        return TextEditor::TriggeredCompletion;
    default:
        return TextEditor::AutomaticCompletion;
    }
}

void CompletionSettingsPageWidget::settingsFromUi(CompletionSettings &completion) const
{
    completion.m_caseSensitivity = caseSensitivity();
    completion.m_completionTrigger = completionTrigger();
    completion.m_automaticProposalTimeoutInMs
            = m_automaticProposalTimeoutSpinBox->value();
    completion.m_characterThreshold = m_thresholdSpinBox->value();
    completion.m_autoInsertBrackets = m_insertBrackets->isChecked();
    completion.m_surroundingAutoBrackets = m_surroundBrackets->isChecked();
    completion.m_autoInsertQuotes = m_insertQuotes->isChecked();
    completion.m_surroundingAutoQuotes = m_surroundQuotes->isChecked();
    completion.m_partiallyComplete = m_partiallyComplete->isChecked();
    completion.m_spaceAfterFunctionName = m_spaceAfterFunctionName->isChecked();
    completion.m_autoSplitStrings = m_autoSplitStrings->isChecked();
    completion.m_animateAutoComplete = m_animateAutoComplete->isChecked();
    completion.m_overwriteClosingChars = m_overwriteClosingChars->isChecked();
    completion.m_highlightAutoComplete = m_highlightAutoComplete->isChecked();
    completion.m_skipAutoCompletedText = m_skipAutoComplete->isChecked();
    completion.m_autoRemove = m_removeAutoComplete->isChecked();
}

class CompletionSettingsPage final : public Core::IOptionsPage
{
public:
    CompletionSettingsPage()
    {
        setId("P.Completion");
        setDisplayName(Tr::tr("Completion"));
        setCategory(TextEditor::Constants::TEXT_EDITOR_SETTINGS_CATEGORY);
        setWidgetCreator([] { return new CompletionSettingsPageWidget; });
    }
};

void setupCompletionSettings()
{
    static CompletionSettingsPage theCompletionSettingsPage;

    QtcSettings *s = Core::ICore::settings();
    completionSettings().fromSettings(s);
}

} // Internal
} // TextEditor
