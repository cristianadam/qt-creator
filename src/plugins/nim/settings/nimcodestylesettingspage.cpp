// Copyright (C) Filippo Cucchetto <filippocucchetto@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "nimcodestylesettingspage.h"

#include "../editor/nimeditorfactory.h"
#include "../editor/nimindenter.h"
#include "../nimconstants.h"
#include "../nimtr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/icore.h>

#include <texteditor/codestyleeditor.h>
#include <texteditor/codestyleselectorwidget.h>
#include <texteditor/codestylepool.h>
#include <texteditor/displaysettings.h>
#include <texteditor/fontsettings.h>
#include <texteditor/icodestylepreferencesfactory.h>
#include <texteditor/icodestylepreferences.h>
#include <texteditor/indenter.h>
#include <texteditor/snippets/snippeteditor.h>
#include <texteditor/tabsettings.h>
#include <texteditor/textdocument.h>

#include <utils/aspects.h>
#include <utils/id.h>
#include <utils/layoutbuilder.h>
#include <utils/shutdownguard.h>

#include <QTextDocument>

using namespace TextEditor;
using namespace Utils;

namespace Nim {

class NimCodeStylePreferencesWidget : public QWidget
{
public:
    NimCodeStylePreferencesWidget(ICodeStylePreferences *preferences, QWidget *parent)
        : QWidget(parent)
        , m_preferences(preferences)
    {
        m_tabSettingsWidget.setPreferences(preferences);

        m_previewTextEdit.setPlainText(Nim::Constants::C_NIMCODESTYLEPREVIEWSNIPPET);
        DisplaySettingsData displaySettings = m_previewTextEdit.displaySettings();
        displaySettings.m_visualizeWhitespace = true;
        m_previewTextEdit.setDisplaySettings(displaySettings);

        using namespace Layouting;
        Row {
            Column {
                &m_tabSettingsWidget,
                st,
            },
            &m_previewTextEdit,
            noMargin,
        }.attachTo(this);

        decorateEditor();
        connect(&globalFontSettings(), &FontSettings::changed,
                this, &NimCodeStylePreferencesWidget::decorateEditor);

        connect(m_preferences, &ICodeStylePreferences::currentTabSettingsChanged,
                this, &NimCodeStylePreferencesWidget::updatePreview);

        updatePreview();
    }

private:
    void decorateEditor();
    void updatePreview();

    ICodeStylePreferences *m_preferences;
    TabSettings m_tabSettingsWidget;
    SnippetEditorWidget m_previewTextEdit;
};

void NimCodeStylePreferencesWidget::decorateEditor()
{
    m_previewTextEdit.textDocument()->setFontSettings(globalFontSettings().data());
    NimEditorFactory::decorateEditor(&m_previewTextEdit);
}

void NimCodeStylePreferencesWidget::updatePreview()
{
    QTextDocument *doc = m_previewTextEdit.document();

    const TabSettingsData &ts = m_preferences
            ? m_preferences->currentTabSettings()
            : globalCodeStyle().tabSettings();
    m_previewTextEdit.textDocument()->setTabSettings(ts);

    QTextBlock block = doc->firstBlock();
    QTextCursor tc = m_previewTextEdit.textCursor();
    tc.beginEditBlock();
    while (block.isValid()) {
        m_previewTextEdit.textDocument()->indenter()->indentBlock(block, QChar::Null, ts);
        block = block.next();
    }
    tc.endEditBlock();
}

// NimCodeStyleEditor

class NimCodeStyleEditor final : public CodeStyleEditor
{
public:
    explicit NimCodeStyleEditor(ICodeStylePreferences *codeStyle)
        : m_selector{{}, this}
        , m_widget{codeStyle, this}
    {
        m_selector.setCodeStyle(codeStyle);
        addSelector(&m_selector);
        addInfoLabel();
        addEditorWidget(&m_widget);
    }

private:
    CodeStyleSelectorWidget m_selector;
    NimCodeStylePreferencesWidget m_widget;
};

// NimCodeStylePreferencesFactory

class NimCodeStylePreferencesFactory final : public ICodeStylePreferencesFactory
{
public:
    NimCodeStylePreferencesFactory()
        : ICodeStylePreferencesFactory(Constants::C_NIMLANGUAGE_ID)
    {}

private:
    CodeStyleEditor *createSettingsEditor(ICodeStylePreferences *codeStyle) const final
    {
        return new NimCodeStyleEditor{codeStyle};
    }

    QString snippetGroupId() const final
    {
        return Constants::C_NIMSNIPPETSGROUP_ID;
    }

    QString previewText() const final
    {
        return QString::fromLatin1(Constants::C_NIMCODESTYLEPREVIEWSNIPPET);
    }

    QString displayName() final
    {
        return Tr::tr(Constants::C_NIMLANGUAGE_NAME);
    }

    ICodeStylePreferences *createCodeStyle() const final
    {
        auto prefs = new ICodeStylePreferences();
        prefs->setSettingsSuffix("TabPreferences");
        return prefs;
    }

    Indenter *createIndenter(QTextDocument *doc) const final
    {
        return createNimIndenter(doc);
    }
};

// Drives the global Nim code style settings page. The selector and tab-settings
// editor operate on a page-local copy of the style; edits are committed to the
// real style only on apply() and discarded on cancel(), giving the page proper
// apply/cancel semantics without an IOptionsPageWidget subclass.
class NimCodeStyleSettings final : public AspectContainer
{
public:
    explicit NimCodeStyleSettings(ICodeStylePreferences *codeStyle)
        : m_codeStyle(codeStyle)
    {
        const auto markDirty = [this] {
            if (m_syncing)
                return;
            m_dirty = true;
            emit volatileValueChanged();
        };
        // Edits may land on the selected delegate rather than on the page-local
        // copy itself, so track any change to the current settings or delegate
        // instead of comparing the copy's own values.
        connect(&m_pageCodeStyle, &ICodeStylePreferences::currentTabSettingsChanged,
                this, markDirty);
        connect(&m_pageCodeStyle, &ICodeStylePreferences::currentDelegateChanged,
                this, markDirty);

        setLayouter([this] {
            m_pageCodeStyle.setDelegatingPool(m_codeStyle->delegatingPool());
            m_pageCodeStyle.setId(m_codeStyle->id());
            syncFromGlobal();

            CodeStyleEditor *editor = codeStyleFactory(Nim::Constants::C_NIMLANGUAGE_ID)
                                          ->createSettingsEditor(&m_pageCodeStyle);
            using namespace Layouting;
            return Column { editor };
        });
    }

    void apply() final
    {
        if (m_codeStyle->tabSettings() != m_pageCodeStyle.tabSettings()) {
            m_codeStyle->setTabSettings(m_pageCodeStyle.tabSettings());
            m_codeStyle->toSettings(Nim::Constants::C_NIMLANGUAGE_ID);
        }
        if (m_codeStyle->currentDelegate() != m_pageCodeStyle.currentDelegate()) {
            m_codeStyle->setCurrentDelegate(m_pageCodeStyle.currentDelegate());
            m_codeStyle->toSettings(Nim::Constants::C_NIMLANGUAGE_ID);
        }
        m_dirty = false;
        emit volatileValueChanged();
    }

    void cancel() final
    {
        syncFromGlobal();
        m_dirty = false;
    }

    bool isDirty() const final { return m_dirty; }

private:
    void syncFromGlobal()
    {
        m_syncing = true;
        m_pageCodeStyle.setTabSettings(m_codeStyle->tabSettings());
        m_pageCodeStyle.setCurrentDelegate(m_codeStyle->currentDelegate());
        m_syncing = false;
    }

    ICodeStylePreferences *m_codeStyle;
    ICodeStylePreferences m_pageCodeStyle;
    bool m_syncing = false;
    bool m_dirty = false;
};

// NimCodeStyleSettingsPage

class NimCodeStyleSettingsPage final : public Core::IOptionsPage
{
public:
    NimCodeStyleSettingsPage()
    {
        setId(Nim::Constants::C_NIMCODESTYLESETTINGSPAGE_ID);
        setDisplayName(Tr::tr("Code Style"));
        setCategory(Nim::Constants::C_NIMCODESTYLESETTINGSPAGE_CATEGORY);
        setSettingsProvider([this] { return &m_settings; });

        m_globalCodeStyle.setSettingsSuffix("TabPreferences");
        m_globalCodeStyle.setDelegatingPool(&m_pool);
        m_globalCodeStyle.setDisplayName(Tr::tr("Global", "Settings"));
        m_globalCodeStyle.setId(Nim::Constants::C_NIMGLOBALCODESTYLE_ID);
        m_pool.addCodeStyle(&m_globalCodeStyle);
        registerCodeStyle(Nim::Constants::C_NIMLANGUAGE_ID, &m_globalCodeStyle);

        m_nimCodeStyle.setId("nim");
        m_nimCodeStyle.setDisplayName(Tr::tr("Nim"));
        m_nimCodeStyle.setReadOnly(true);

        TabSettingsData nimTabSettings;
        nimTabSettings.m_tabPolicy = TabSettingsData::SpacesOnlyTabPolicy;
        nimTabSettings.m_tabSize = 2;
        nimTabSettings.m_indentSize = 2;
        nimTabSettings.m_continuationAlignBehavior = TabSettingsData::ContinuationAlignWithIndent;
        m_nimCodeStyle.setTabSettings(nimTabSettings);

        m_pool.addCodeStyle(&m_nimCodeStyle);
        m_globalCodeStyle.setCurrentDelegate(&m_nimCodeStyle);
        m_pool.loadCustomCodeStyles();

        // load global settings (after built-in settings are added to the pool)
        m_globalCodeStyle.fromSettings(Nim::Constants::C_NIMLANGUAGE_ID);

        registerMimeTypeForLanguageId(Nim::Constants::C_NIM_MIMETYPE,
                                      Nim::Constants::C_NIMLANGUAGE_ID);
        registerMimeTypeForLanguageId(Nim::Constants::C_NIM_SCRIPT_MIMETYPE,
                                      Nim::Constants::C_NIMLANGUAGE_ID);
    }

    ~NimCodeStyleSettingsPage()
    {
        unregisterCodeStyle(Nim::Constants::C_NIMLANGUAGE_ID);
    }

private:
    NimCodeStylePreferencesFactory m_factory;
    CodeStylePool m_pool{&m_factory, Nim::Constants::C_NIMLANGUAGE_ID};
    ICodeStylePreferences m_globalCodeStyle;
    ICodeStylePreferences m_nimCodeStyle;
    NimCodeStyleSettings m_settings{&m_globalCodeStyle};
};

void Internal::setupNimCodeStyle()
{
    static GuardedObject<NimCodeStyleSettingsPage> theNimCodeStyleSettingsPage;
}

} // Nim
