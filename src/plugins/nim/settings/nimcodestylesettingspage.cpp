// Copyright  (C) Filippo Cucchetto <filippocucchetto@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "nimcodestylesettingspage.h"
#include "nimcodestylepreferencesfactory.h"
#include "nimsettings.h"

#include "../nimconstants.h"

#include <texteditor/simplecodestylepreferences.h>
#include <texteditor/codestyleeditor.h>
#include <texteditor/texteditorsettings.h>
#include <texteditor/tabsettings.h>

#include <QVBoxLayout>

using namespace TextEditor;

namespace Nim {

class NimCodeStyleSettingsWidget : public Core::IOptionsPageWidget
{
    Q_DECLARE_TR_FUNCTIONS(Nim::CodeStyleSettings)

public:
    NimCodeStyleSettingsWidget()
    {
        auto originalTabPreferences = NimSettings::globalCodeStyle();
        m_nimCodeStylePreferences = new SimpleCodeStylePreferences(this);
        m_nimCodeStylePreferences->setDelegatingPool(originalTabPreferences->delegatingPool());
        m_nimCodeStylePreferences->setTabSettings(originalTabPreferences->tabSettings());
        m_nimCodeStylePreferences->setCurrentDelegate(originalTabPreferences->currentDelegate());
        m_nimCodeStylePreferences->setId(originalTabPreferences->id());

        auto factory = TextEditorSettings::codeStyleFactory(Nim::Constants::C_NIMLANGUAGE_ID);

        auto editor = new CodeStyleEditor(factory, m_nimCodeStylePreferences);

        auto layout = new QVBoxLayout(this);
        layout->addWidget(editor);
    }

private:
    void apply() final {}
    void finish() final {}

    TextEditor::SimpleCodeStylePreferences *m_nimCodeStylePreferences;
};

NimCodeStyleSettingsPage::NimCodeStyleSettingsPage()
{
    setId(Nim::Constants::C_NIMCODESTYLESETTINGSPAGE_ID);
    setDisplayName(tr(Nim::Constants::C_NIMCODESTYLESETTINGSPAGE_DISPLAY));
    setCategory(Nim::Constants::C_NIMCODESTYLESETTINGSPAGE_CATEGORY);
    setDisplayCategory(NimCodeStyleSettingsWidget::tr("Nim"));
    setCategoryIconPath(":/nim/images/settingscategory_nim.png");
    setWidgetCreator([] { return new NimCodeStyleSettingsWidget; });
}

} // Nim
