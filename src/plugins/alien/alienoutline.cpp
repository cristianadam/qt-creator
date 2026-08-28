// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "alienoutline.h"

#include "alientr.h"
#include "extensionhost.h"

#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <utils/navigationtreeview.h>
#include <utils/utilsicons.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QFont>
#include <QStandardItemModel>
#include <QVBoxLayout>

using namespace Utils;

namespace Alien::Internal {

enum { RangeLineRole = Qt::UserRole + 1, RangeCharacterRole };

// One symbol per row, children under their parent. Only what is needed to show
// it and to go there: the extension is asked again whenever the text changes.
static QList<QStandardItem *> itemsFor(const QJsonArray &symbols)
{
    QList<QStandardItem *> items;
    for (const QJsonValue &value : symbols) {
        const QJsonObject symbol = value.toObject();
        const QString detail = symbol.value("detail").toString();
        const QString name = symbol.value("name").toString();
        auto item = new QStandardItem(detail.isEmpty() ? name : name + ' ' + detail);
        item->setEditable(false);

        // A symbol that still works but should not be used any more.
        for (const QJsonValue &tag : symbol.value("tags").toArray()) {
            if (tag.toInt() != 1)
                continue;
            QFont font = item->font();
            font.setStrikeOut(true);
            item->setFont(font);
        }

        const QJsonObject selection = symbol.value("selectionRange").toObject()
                                          .value("start").toObject();
        item->setData(selection.value("line").toInt(), RangeLineRole);
        item->setData(selection.value("character").toInt(), RangeCharacterRole);

        const QList<QStandardItem *> children = itemsFor(symbol.value("children").toArray());
        for (QStandardItem *child : children)
            item->appendRow(child);
        items.append(item);
    }
    return items;
}

class AlienOutlineWidget final : public TextEditor::IOutlineWidget
{
public:
    AlienOutlineWidget(ExtensionHost *host, TextEditor::TextEditorWidget *editorWidget)
        : m_host(host)
        , m_editorWidget(editorWidget)
    {
        m_view.setModel(&m_model);
        m_view.setHeaderHidden(true);
        m_view.setExpandsOnDoubleClick(false);
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(&m_view);

        connect(&m_view, &Utils::NavigationTreeView::activated,
                this, &AlienOutlineWidget::goToSymbol);
        if (m_editorWidget) {
            connect(m_editorWidget->textDocument(), &TextEditor::TextDocument::contentsChanged,
                    this, &AlienOutlineWidget::refresh);
        }
        refresh();
    }

private:
    QList<QAction *> filterMenuActions() const final { return {}; }
    void setCursorSynchronization(bool) final {}

    void refresh()
    {
        if (!m_host || !m_editorWidget)
            return;
        m_host->requestDocumentSymbols(
            m_editorWidget->textDocument()->filePath(),
            [this](const QJsonArray &symbols) {
                m_model.clear();
                for (QStandardItem *item : itemsFor(symbols))
                    m_model.appendRow(item);
                m_view.expandAll();
            });
    }

    void goToSymbol(const QModelIndex &index)
    {
        if (!m_editorWidget || !index.isValid())
            return;
        m_editorWidget->gotoLine(index.data(RangeLineRole).toInt() + 1,
                                 index.data(RangeCharacterRole).toInt());
        m_editorWidget->setFocus();
    }

    QPointer<ExtensionHost> m_host;
    QPointer<TextEditor::TextEditorWidget> m_editorWidget;
    QStandardItemModel m_model;
    Utils::NavigationTreeView m_view;
};

class AlienOutlineFactory final : public TextEditor::IOutlineWidgetFactory
{
public:
    explicit AlienOutlineFactory(ExtensionHost *host)
        : m_host(host)
    {}

    bool supportsEditor(Core::IEditor *editor) const final
    {
        if (!m_host)
            return false;
        TextEditor::TextEditorWidget *widget = TextEditor::TextEditorWidget::fromEditor(editor);
        return widget && m_host->providesSymbolsFor(widget->textDocument()->filePath());
    }

    TextEditor::IOutlineWidget *createWidget(Core::IEditor *editor) final
    {
        TextEditor::TextEditorWidget *widget = TextEditor::TextEditorWidget::fromEditor(editor);
        return widget ? new AlienOutlineWidget(m_host, widget) : nullptr;
    }

private:
    QPointer<ExtensionHost> m_host;
};

void setupAlienOutline(ExtensionHost *host)
{
    // Owned by the host: the factory answers for it, and both go when the
    // plugin does.
    new AlienOutlineFactory(host);
}

} // namespace Alien::Internal
