// Copyright (C) Filippo Cucchetto <filippocucchetto@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "nimcodestylepreferencesfactory.h"

#include "nim/editor/nimindenter.h"
#include "nim/nimconstants.h"
#include "nim/nimtr.h"
#include "nimcodestylepreferenceswidget.h"

#include <texteditor/codestyleeditor.h>
#include <texteditor/indenter.h>
#include <texteditor/simplecodestylepreferences.h>
#include <texteditor/codestyleeditor.h>
#include <texteditor/icodestylepreferencesfactory.h>

#include <utils/id.h>

#include <QTextDocument>

using namespace TextEditor;
using namespace Utils;

namespace Nim {

class NimCodeStyleEditor final : public CodeStyleEditor
{
public:
    static NimCodeStyleEditor *create(
        const ICodeStylePreferencesFactory *factory,
        const FilePath &projectFile,
        ICodeStylePreferences *codeStyle,
        QWidget *parent )
    {
        auto editor = new NimCodeStyleEditor{parent};
        editor->init(factory, projectFile, codeStyle);
        return editor;
    }

private:
    NimCodeStyleEditor(QWidget *parent)
        : CodeStyleEditor{parent}
    {}

    CodeStyleEditorWidget *createEditorWidget(
        const FilePath & /*projectFile*/,
        ICodeStylePreferences *codeStyle,
        QWidget *parent) const final
    {
        return new NimCodeStylePreferencesWidget(codeStyle, parent);
    }

    QString previewText() const final
    {
        return Constants::C_NIMCODESTYLEPREVIEWSNIPPET;
    }

    QString snippetProviderGroupId() const final
    {
        return Constants::C_NIMSNIPPETSGROUP_ID;
    }
};

// NimCodeStylePreferencesFactory

class NimCodeStylePreferencesFactory final : public ICodeStylePreferencesFactory
{
public:
    NimCodeStylePreferencesFactory() = default;

private:
    CodeStyleEditorWidget *createCodeStyleEditor(
            const FilePath &projectFile,
            ICodeStylePreferences *codeStyle,
            QWidget *parent) const final
    {
        return NimCodeStyleEditor::create(this, projectFile, codeStyle, parent);
    }

    Utils::Id languageId() final
    {
        return Constants::C_NIMLANGUAGE_ID;
    }

    QString displayName() final
    {
        return Tr::tr(Constants::C_NIMLANGUAGE_NAME);
    }

    ICodeStylePreferences *createCodeStyle() const final
    {
        return new SimpleCodeStylePreferences();
    }

    Indenter *createIndenter(QTextDocument *doc) const final
    {
        return createNimIndenter(doc);
    }
};

ICodeStylePreferencesFactory *createNimCodeStylePreferencesFactory()
{
    return new NimCodeStylePreferencesFactory;
}

} // Nim
