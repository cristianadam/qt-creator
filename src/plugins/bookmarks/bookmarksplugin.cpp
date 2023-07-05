// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "bookmarksplugin.h"

#include "bookmarkfilter.h"
#include "bookmarkmanager.h"
#include "bookmarks_global.h"
#include "bookmarkstr.h"

#include <coreplugin/icore.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/actioncontainer.h>

#include <texteditor/texteditor.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditorconstants.h>

#include <utils/utilsicons.h>

#include <QMenu>

using namespace Core;
using namespace TextEditor;
using namespace Utils;

using namespace Bookmarks::Constants;

namespace Bookmarks::Internal {

class BookmarkMenu : public Menu
{
public:
    BookmarkMenu()
    {
        setId(BOOKMARKS_MENU);
        setTitle(Tr::tr("&Bookmarks"));
        setContainer(Core::Constants::M_TOOLS);
    }
};

class ToggleAction : public Action
{
public:
    ToggleAction()
    {
        setId(BOOKMARKS_TOGGLE_ACTION);
        setContext(Core::Constants::C_EDITORMANAGER);
        setText(Tr::tr("Toggle Bookmark"));
        setDefaultKeySequence(Tr::tr("Meta+M"), Tr::tr("Ctrl+M"));
        setTouchBarIcon(Icons::MACOS_TOUCHBAR_BOOKMARK.icon());
        setContainer(BOOKMARKS_MENU);
    }
};

class EditAction : public Action
{
public:
    EditAction()
    {
        setId(BOOKMARKS_EDIT_ACTION);
        setContext(Core::Constants::C_EDITORMANAGER);
        setText(Tr::tr("Edit Bookmark"));
        setDefaultKeySequence(Tr::tr("Meta+Shift+M"), Tr::tr("Ctrl+Shift+M"));
        setContainer(BOOKMARKS_MENU);
    }
};

class PrevAction : public Action
{
public:
    PrevAction()
    {
        setId(BOOKMARKS_PREV_ACTION);
        setContext(Core::Constants::C_EDITORMANAGER);
        setText(Tr::tr("Previous Bookmark"));
        setDefaultKeySequence(Tr::tr("Meta+,"), Tr::tr("Ctrl+,"));
        setContainer(BOOKMARKS_MENU);
        setIcon(Icons::PREV_TOOLBAR.icon());
        setIconVisibleInMenu(false);
    }
};

class NextAction : public Action
{
public:
    NextAction()
    {
        setId(BOOKMARKS_NEXT_ACTION);
        setContext(Core::Constants::C_EDITORMANAGER);
        setText(Tr::tr("Next Bookmark"));
        setIcon(Icons::NEXT_TOOLBAR.icon());
        setIconVisibleInMenu(false);
        setDefaultKeySequence(Tr::tr("Meta+."), Tr::tr("Ctrl+."));
        setContainer(BOOKMARKS_MENU);
    }
};

class DocPrevAction : public Action
{
public:
    DocPrevAction()
    {
        setId(BOOKMARKS_PREVDOC_ACTION);
        setContext(Core::Constants::C_EDITORMANAGER);
        setText(Tr::tr("Previous Bookmark in Document"));
        setContainer(BOOKMARKS_MENU);
    }
};

class DocNextAction : public Action
{
public:
    DocNextAction()
    {
        setId(BOOKMARKS_NEXTDOC_ACTION);
        setContext(Core::Constants::C_EDITORMANAGER);
        setText(Tr::tr("Next Bookmark in Document"));
        setContainer(BOOKMARKS_MENU);
    }
};

class BookmarksPluginPrivate : public QObject
{
public:
    BookmarksPluginPrivate();

    void updateActions(bool enableToggle, int stateMask);
    void editorOpened(Core::IEditor *editor);
    void editorAboutToClose(Core::IEditor *editor);

    void requestContextMenu(TextEditor::TextEditorWidget *widget,
                            int lineNumber, QMenu *menu);

    BookmarkManager m_bookmarkManager;
    BookmarkFilter m_bookmarkFilter;
    BookmarkViewFactory m_bookmarkViewFactory;

    BookmarkMenu m_bookmarkMenu;
    ToggleAction m_toggleAction;
    EditAction m_editAction;
    ActionSeparator m_separator1{BOOKMARKS_MENU};

    PrevAction m_prevAction;
    NextAction m_nextAction;
    ActionSeparator m_separator2{BOOKMARKS_MENU};

    DocPrevAction m_docPrevAction;
    DocNextAction m_docNextAction;

    QAction m_editBookmarkAction{Tr::tr("Edit Bookmark"), nullptr};
    QAction m_bookmarkMarginAction{Tr::tr("Toggle Bookmark"), nullptr};

    int m_marginActionLineNumber = 0;
    FilePath m_marginActionFileName;
};

BookmarksPlugin::~BookmarksPlugin()
{
    delete d;
}

void BookmarksPlugin::initialize()
{
    d = new BookmarksPluginPrivate;
}

BookmarksPluginPrivate::BookmarksPluginPrivate()
    : m_bookmarkFilter(&m_bookmarkManager)
    , m_bookmarkViewFactory(&m_bookmarkManager)
{
    ActionContainer *touchBar = ActionManager::actionContainer(Core::Constants::TOUCH_BAR);

    touchBar->addAction(m_toggleAction.command(), Core::Constants::G_TOUCHBAR_EDITOR);

    m_toggleAction.setOnTriggered(this, [this] {
        IEditor *editor = EditorManager::currentEditor();
        auto widget = TextEditorWidget::fromEditor(editor);
        if (widget && editor && !editor->document()->isTemporary())
            m_bookmarkManager.toggleBookmark(editor->document()->filePath(), editor->currentLine());
    });

    m_editAction.setOnTriggered(this, [this] {
        IEditor *editor = EditorManager::currentEditor();
        auto widget = TextEditorWidget::fromEditor(editor);
        if (widget && editor && !editor->document()->isTemporary()) {
            const FilePath filePath = editor->document()->filePath();
            const int line = editor->currentLine();
            if (!m_bookmarkManager.hasBookmarkInPosition(filePath, line))
                m_bookmarkManager.toggleBookmark(filePath, line);
            m_bookmarkManager.editByFileAndLine(filePath, line);
        }
    });

    m_prevAction.setOnTriggered(this, [this] { m_bookmarkManager.prev(); });
    m_nextAction.setOnTriggered(this, [this] { m_bookmarkManager.next(); });
    m_docPrevAction.setOnTriggered(this, [this] { m_bookmarkManager.prevInDocument(); });
    m_docNextAction.setOnTriggered(this, [this] { m_bookmarkManager.nextInDocument(); });

    connect(&m_editBookmarkAction, &QAction::triggered, this, [this] {
            m_bookmarkManager.editByFileAndLine(m_marginActionFileName, m_marginActionLineNumber);
    });

    connect(&m_bookmarkManager, &BookmarkManager::updateActions,
            this, &BookmarksPluginPrivate::updateActions);
    updateActions(false, m_bookmarkManager.state());

    connect(&m_bookmarkMarginAction, &QAction::triggered, this, [this] {
            m_bookmarkManager.toggleBookmark(m_marginActionFileName, m_marginActionLineNumber);
    });

    // EditorManager
    connect(EditorManager::instance(), &EditorManager::editorAboutToClose,
        this, &BookmarksPluginPrivate::editorAboutToClose);
    connect(EditorManager::instance(), &EditorManager::editorOpened,
        this, &BookmarksPluginPrivate::editorOpened);
}

void BookmarksPluginPrivate::updateActions(bool enableToggle, int state)
{
    const bool hasbm    = state >= BookmarkManager::HasBookMarks;
    const bool hasdocbm = state == BookmarkManager::HasBookmarksInDocument;

    m_toggleAction.setEnabled(enableToggle);
    m_editAction.setEnabled(enableToggle);
    m_prevAction.setEnabled(hasbm);
    m_nextAction.setEnabled(hasbm);
    m_docPrevAction.setEnabled(hasdocbm);
    m_docNextAction.setEnabled(hasdocbm);
}

void BookmarksPluginPrivate::editorOpened(IEditor *editor)
{
    if (auto widget = TextEditorWidget::fromEditor(editor)) {
        connect(widget, &TextEditorWidget::markRequested,
                this, [this, editor](TextEditorWidget *, int line, TextMarkRequestKind kind) {
                    if (kind == BookmarkRequest && !editor->document()->isTemporary())
                        m_bookmarkManager.toggleBookmark(editor->document()->filePath(), line);
                });

        connect(widget, &TextEditorWidget::markContextMenuRequested,
                this, &BookmarksPluginPrivate::requestContextMenu);
    }
}

void BookmarksPluginPrivate::editorAboutToClose(IEditor *editor)
{
    if (auto widget = TextEditorWidget::fromEditor(editor)) {
        disconnect(widget, &TextEditorWidget::markContextMenuRequested,
                   this, &BookmarksPluginPrivate::requestContextMenu);
    }
}

void BookmarksPluginPrivate::requestContextMenu(TextEditorWidget *widget,
    int lineNumber, QMenu *menu)
{
    if (widget->textDocument()->isTemporary())
        return;

    m_marginActionLineNumber = lineNumber;
    m_marginActionFileName = widget->textDocument()->filePath();

    menu->addAction(&m_bookmarkMarginAction);
    if (m_bookmarkManager.hasBookmarkInPosition(m_marginActionFileName, m_marginActionLineNumber))
        menu->addAction(&m_editBookmarkAction);
}

} // Bookmarks::Internal
