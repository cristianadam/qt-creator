// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "editortoolbar.h"

#include "actionmanager/actionmanager.h"
#include "actionmanager/command.h"
#include "coreplugintr.h"
#include "editormanager/documentmodel.h"
#include "editormanager/editormanager.h"
#include "editormanager/editormanager_p.h"
#include "editormanager/ieditor.h"
#include "icore.h"

#include <utils/filepath.h>
#include <utils/fsengine/fileiconprovider.h>
#include <utils/hostosinfo.h>
#include <utils/qtcassert.h>
#include <utils/stylehelper.h>
#include <utils/utilsicons.h>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDrag>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <QDebug>

enum {
    debug = false
};

namespace Core {

class ButtonWithMenu : public QToolButton
{
public:
    ButtonWithMenu(QWidget *parent = nullptr)
        : QToolButton(parent)
    {}

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::RightButton) {
            showMenu();
            return;
        }
        QToolButton::mousePressEvent(e);
    }
};

struct EditorToolBarPrivate
{
    explicit EditorToolBarPrivate(QWidget *parent, EditorToolBar *q);

    QComboBox *m_editorList;
    QToolButton *m_closeEditorButton;
    QToolButton *m_lockButton;
    QToolButton *m_dragHandle;
    QMenu *m_dragHandleMenu;
    EditorToolBar::MenuProvider m_menuProvider;
    QAction *m_goBackAction;
    QAction *m_goForwardAction;
    ButtonWithMenu *m_backButton;
    ButtonWithMenu *m_forwardButton;
    QToolButton *m_splitButton;
    QAction *m_horizontalSplitAction;
    QAction *m_verticalSplitAction;
    QAction *m_splitNewWindowAction;
    QToolButton *m_closeSplitButton;

    QWidget *m_activeToolBar;
    QWidget *m_toolBarPlaceholder;
    QWidget *m_defaultToolBar;

    QPoint m_dragStartPosition;

    bool m_isStandalone;
};

EditorToolBarPrivate::EditorToolBarPrivate(QWidget *parent, EditorToolBar *q)
    : m_editorList(new QComboBox(q))
    , m_closeEditorButton(new QToolButton(q))
    , m_lockButton(new QToolButton(q))
    , m_dragHandle(new QToolButton(q))
    , m_dragHandleMenu(nullptr)
    , m_goBackAction(new QAction(Utils::Icons::PREV_TOOLBAR.icon(), Tr::tr("Go Back"), parent))
    , m_goForwardAction(new QAction(Utils::Icons::NEXT_TOOLBAR.icon(), Tr::tr("Go Forward"), parent))
    , m_backButton(new ButtonWithMenu(q))
    , m_forwardButton(new ButtonWithMenu(q))
    , m_splitButton(new QToolButton(q))
    , m_horizontalSplitAction(
          new QAction(Utils::Icons::SPLIT_HORIZONTAL.icon(), Tr::tr("Split"), parent))
    , m_verticalSplitAction(
          new QAction(Utils::Icons::SPLIT_VERTICAL.icon(), Tr::tr("Split Side by Side"), parent))
    , m_splitNewWindowAction(new QAction(Tr::tr("Open in New Window"), parent))
    , m_closeSplitButton(new QToolButton(q))
    , m_activeToolBar(nullptr)
    , m_toolBarPlaceholder(new QWidget(q))
    , m_defaultToolBar(new QWidget(q))
    , m_isStandalone(false)
{
}

/*!
  Mimic the look of the text editor toolbar as defined in e.g. EditorView::EditorView
  */
EditorToolBar::EditorToolBar(QWidget *parent) :
        Utils::StyledBar(parent), d(new EditorToolBarPrivate(parent, this))
{
    auto toolBarLayout = new QHBoxLayout(this);
    toolBarLayout->setContentsMargins(0, 0, 0, 0);
    toolBarLayout->setSpacing(0);
    toolBarLayout->addWidget(d->m_defaultToolBar);
    d->m_toolBarPlaceholder->setLayout(toolBarLayout);
    d->m_toolBarPlaceholder->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);

    d->m_defaultToolBar->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
    d->m_activeToolBar = d->m_defaultToolBar;

    d->m_lockButton->setEnabled(false);

    d->m_dragHandle->setProperty(Utils::StyleHelper::C_NO_ARROW, true);
    d->m_dragHandle->setToolTip(Tr::tr("Drag to drag documents between splits"));
    d->m_dragHandle->installEventFilter(this);
    d->m_dragHandleMenu = new QMenu(d->m_dragHandle);
    d->m_dragHandle->setMenu(d->m_dragHandleMenu);

    connect(d->m_goBackAction, &QAction::triggered, this, &EditorToolBar::goBackClicked);
    connect(d->m_goForwardAction, &QAction::triggered, this, &EditorToolBar::goForwardClicked);

    d->m_editorList->setProperty(Utils::StyleHelper::C_HIDE_ICON, true);
    d->m_editorList->setProperty(Utils::StyleHelper::C_NOT_ELIDE_ASTERISK, true);
    d->m_editorList->setProperty(Utils::StyleHelper::C_ELIDE_MODE, Qt::ElideMiddle);
    d->m_editorList->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    d->m_editorList->setMinimumContentsLength(20);
    d->m_editorList->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    d->m_editorList->setModel(DocumentModel::model());
    d->m_editorList->setMaxVisibleItems(40);
    d->m_editorList->setContextMenuPolicy(Qt::CustomContextMenu);

    d->m_closeEditorButton->setIcon(Utils::Icons::CLOSE_TOOLBAR.icon());
    d->m_closeEditorButton->setEnabled(false);
    d->m_closeEditorButton->setProperty(Utils::StyleHelper::C_SHOW_BORDER, true);

    d->m_toolBarPlaceholder->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);

    d->m_backButton->setDefaultAction(d->m_goBackAction);

    d->m_forwardButton->setDefaultAction(d->m_goForwardAction);

    d->m_splitButton->setIcon(Utils::Icons::SPLIT_HORIZONTAL_TOOLBAR.icon());
    d->m_splitButton->setToolTip(Tr::tr("Split"));
    d->m_splitButton->setPopupMode(QToolButton::InstantPopup);
    d->m_splitButton->setProperty(Utils::StyleHelper::C_NO_ARROW, true);
    auto splitMenu = new QMenu(d->m_splitButton);
    splitMenu->addAction(d->m_horizontalSplitAction);
    splitMenu->addAction(d->m_verticalSplitAction);
    splitMenu->addAction(d->m_splitNewWindowAction);
    d->m_splitButton->setMenu(splitMenu);

    d->m_closeSplitButton->setIcon(Utils::Icons::CLOSE_SPLIT_BOTTOM.icon());

    auto toplayout = new QHBoxLayout(this);
    toplayout->setSpacing(0);
    toplayout->setContentsMargins(0, 0, 0, 0);
    toplayout->addWidget(d->m_backButton);
    toplayout->addWidget(d->m_forwardButton);
    toplayout->addWidget(d->m_lockButton);
    toplayout->addWidget(d->m_dragHandle);
    toplayout->addWidget(d->m_editorList);
    toplayout->addWidget(d->m_closeEditorButton);
    toplayout->addWidget(d->m_toolBarPlaceholder, 1); // Custom toolbar stretches
    toplayout->addWidget(d->m_splitButton);
    toplayout->addWidget(d->m_closeSplitButton);

    setLayout(toplayout);

    // this signal is disconnected for standalone toolbars and replaced with
    // a private slot connection
    connect(d->m_editorList, &QComboBox::activated, this, &EditorToolBar::listSelectionActivated);
    connect(d->m_editorList, &QComboBox::customContextMenuRequested, this, [this](QPoint p) {
       QMenu menu;
       fillListContextMenu(&menu);
       menu.exec(d->m_editorList->mapToGlobal(p));
    });
    connect(d->m_dragHandleMenu, &QMenu::aboutToShow, this, [this] {
       fillListContextMenu(d->m_dragHandleMenu);
    });
    connect(d->m_dragHandleMenu, &QMenu::aboutToHide, this, [this] {
        // Remove actions from context menu, to avoid any shortcuts set on them
        // for the display in the menu interfering with global actions
        // This must be queued, so the QActions in the menu aren't deleted before they
        // are triggered
        d->m_dragHandleMenu->clear();
    }, Qt::QueuedConnection);
    connect(d->m_lockButton, &QAbstractButton::clicked, this, &EditorToolBar::makeEditorWritable);
    connect(d->m_closeEditorButton, &QAbstractButton::clicked,
            this, &EditorToolBar::closeEditor, Qt::QueuedConnection);
    connect(d->m_horizontalSplitAction, &QAction::triggered,
            this, &EditorToolBar::horizontalSplitClicked, Qt::QueuedConnection);
    connect(d->m_verticalSplitAction, &QAction::triggered,
            this, &EditorToolBar::verticalSplitClicked, Qt::QueuedConnection);
    connect(d->m_splitNewWindowAction, &QAction::triggered,
            this, &EditorToolBar::splitNewWindowClicked, Qt::QueuedConnection);
    connect(d->m_closeSplitButton, &QAbstractButton::clicked,
            this, &EditorToolBar::closeSplitClicked, Qt::QueuedConnection);


    connect(ActionManager::command(Constants::CLOSE), &Command::keySequenceChanged,
            this, &EditorToolBar::updateActionShortcuts);
    connect(ActionManager::command(Constants::GO_BACK), &Command::keySequenceChanged,
            this, &EditorToolBar::updateActionShortcuts);
    connect(ActionManager::command(Constants::GO_FORWARD), &Command::keySequenceChanged,
            this, &EditorToolBar::updateActionShortcuts);

    updateActionShortcuts();
}

EditorToolBar::~EditorToolBar()
{
    delete d;
}

void EditorToolBar::removeToolbarForEditor(IEditor *editor)
{
    QTC_ASSERT(editor, return);
    disconnect(editor->document(), &IDocument::changed, this, nullptr);

    QWidget *toolBar = editor->toolBar();
    if (toolBar != nullptr) {
        if (d->m_activeToolBar == toolBar) {
            d->m_activeToolBar = d->m_defaultToolBar;
            d->m_activeToolBar->setVisible(true);
        }
        d->m_toolBarPlaceholder->layout()->removeWidget(toolBar);
        toolBar->setVisible(false);
        toolBar->setParent(nullptr);
    }
}

void EditorToolBar::setCloseSplitEnabled(bool enable)
{
    d->m_closeSplitButton->setVisible(enable);
}

void EditorToolBar::setCloseSplitIcon(const QIcon &icon)
{
    d->m_closeSplitButton->setIcon(icon);
}

void EditorToolBar::closeEditor()
{
    if (d->m_isStandalone)
        EditorManager::slotCloseCurrentEditorOrDocument();
    emit closeClicked();
}

void EditorToolBar::addEditor(IEditor *editor)
{
    QTC_ASSERT(editor, return);
    auto document = editor->document();
    connect(document, &IDocument::changed,
            this, [this, document] { checkDocumentStatus(document); });
    QWidget *toolBar = editor->toolBar();

    if (toolBar && !d->m_isStandalone)
        addCenterToolBar(toolBar);
}

void EditorToolBar::addCenterToolBar(QWidget *toolBar)
{
    QTC_ASSERT(toolBar, return);
    toolBar->setVisible(false); // will be made visible in setCurrentEditor
    d->m_toolBarPlaceholder->layout()->addWidget(toolBar);

    updateToolBar(toolBar);
}

void EditorToolBar::updateToolBar(QWidget *toolBar)
{
    if (!toolBar)
        toolBar = d->m_defaultToolBar;
    if (d->m_activeToolBar == toolBar)
        return;
    toolBar->setVisible(true);
    d->m_activeToolBar->setVisible(false);
    d->m_activeToolBar = toolBar;
}

void EditorToolBar::setToolbarCreationFlags(ToolbarCreationFlags flags)
{
    d->m_isStandalone = flags & FlagsStandalone;
    if (d->m_isStandalone) {
        connect(EditorManager::instance(),
                &EditorManager::currentEditorChanged,
                this,
                &EditorToolBar::setCurrentEditor);

        disconnect(d->m_editorList, &QComboBox::activated,
                   this, &EditorToolBar::listSelectionActivated);
        connect(d->m_editorList, &QComboBox::activated,
                this, &EditorToolBar::changeActiveEditor);
        d->m_splitButton->setVisible(false);
        d->m_closeSplitButton->setVisible(false);
    }
}

void EditorToolBar::setMenuProvider(const EditorToolBar::MenuProvider &provider)
{
    d->m_menuProvider = provider;
}

void EditorToolBar::setCurrentEditor(IEditor *editor)
{
    IDocument *document = editor ? editor->document() : nullptr;
    const std::optional<int> index = DocumentModel::rowOfDocument(document);
    if (QTC_GUARD(index))
        d->m_editorList->setCurrentIndex(*index);

    // If we never added the toolbar from the editor,  we will never change
    // the editor, so there's no need to update the toolbar either.
    if (!d->m_isStandalone)
        updateToolBar(editor ? editor->toolBar() : nullptr);

    updateDocumentStatus(document);
}

void EditorToolBar::changeActiveEditor(int row)
{
    EditorManager::activateEditorForEntry(DocumentModel::entryAtRow(row));
}

void EditorToolBar::fillListContextMenu(QMenu *menu)
{
    if (d->m_menuProvider) {
        d->m_menuProvider(menu);
    } else {
        IEditor *editor = EditorManager::currentEditor();
        DocumentModel::Entry *entry = editor ? DocumentModel::entryForDocument(editor->document())
                                             : nullptr;
        EditorManager::addContextMenuActions(menu, entry, editor);
    }
}

void EditorToolBar::makeEditorWritable()
{
    if (IDocument *current = EditorManager::currentDocument())
        Internal::EditorManagerPrivate::makeFileWritable(current);
}

void EditorToolBar::setCanGoBack(bool canGoBack)
{
    d->m_goBackAction->setEnabled(canGoBack);
}

void EditorToolBar::setCanGoForward(bool canGoForward)
{
    d->m_goForwardAction->setEnabled(canGoForward);
}

void EditorToolBar::setGoBackMenu(QMenu *menu)
{
    d->m_backButton->setMenu(menu);
}

void EditorToolBar::setGoForwardMenu(QMenu *menu)
{
    d->m_forwardButton->setMenu(menu);
}

void EditorToolBar::setDocumentDropdownVisible(bool visible)
{
    d->m_editorList->setVisible(visible);
    d->m_closeEditorButton->setVisible(visible);
}

void EditorToolBar::updateActionShortcuts()
{
    d->m_closeEditorButton->setToolTip(ActionManager::command(Constants::CLOSE)->stringWithAppendedShortcut(Tr::tr("Close Document")));
    d->m_goBackAction->setToolTip(ActionManager::command(Constants::GO_BACK)->stringWithAppendedShortcut(Tr::tr("Go Back")));
    d->m_goForwardAction->setToolTip(ActionManager::command(Constants::GO_FORWARD)->stringWithAppendedShortcut(Tr::tr("Go Forward")));
    d->m_closeSplitButton->setToolTip(ActionManager::command(Constants::REMOVE_CURRENT_SPLIT)->stringWithAppendedShortcut(Tr::tr("Remove Split")));
}

void EditorToolBar::checkDocumentStatus(IDocument *document)
{
    DocumentModel::Entry *entry = DocumentModel::entryAtRow(
                d->m_editorList->currentIndex());

    if (entry && entry->document && entry->document == document)
        updateDocumentStatus(document);
}

void EditorToolBar::updateDocumentStatus(IDocument *document)
{
    d->m_closeEditorButton->setEnabled(document != nullptr);

    if (!document) {
        d->m_lockButton->setIcon(QIcon());
        d->m_lockButton->setEnabled(false);
        d->m_lockButton->setToolTip(QString());
        d->m_dragHandle->setIcon(QIcon());
        d->m_editorList->setToolTip(QString());
        return;
    }

    if (document->filePath().isEmpty()) {
        d->m_lockButton->setIcon(QIcon());
        d->m_lockButton->setEnabled(false);
        d->m_lockButton->setToolTip(QString());
    } else if (document->isFileReadOnly()) {
        const static QIcon locked = Utils::Icons::LOCKED_TOOLBAR.icon();
        d->m_lockButton->setIcon(locked);
        d->m_lockButton->setEnabled(true);
        d->m_lockButton->setToolTip(Tr::tr("Make Writable"));
    } else {
        const static QIcon unlocked = Utils::Icons::UNLOCKED_TOOLBAR.icon();
        d->m_lockButton->setIcon(unlocked);
        d->m_lockButton->setEnabled(false);
        d->m_lockButton->setToolTip(Tr::tr("File is writable"));
    }

    if (document->filePath().isEmpty())
        d->m_dragHandle->setIcon(QIcon());
    else
        d->m_dragHandle->setIcon(Utils::FileIconProvider::icon(document->filePath()));

    d->m_editorList->setToolTip(document->toolTip());
}

bool EditorToolBar::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == d->m_dragHandle) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto me = static_cast<QMouseEvent *>(event);
            if (me->buttons() == Qt::LeftButton)
                d->m_dragStartPosition = me->pos();
            return true; // do not pop up menu already on press
        } else if (event->type() == QEvent::MouseButtonRelease) {
            d->m_dragHandle->showMenu();
            return true;
        } else if (event->type() == QEvent::MouseMove) {
            auto me = static_cast<QMouseEvent *>(event);
            if (me->buttons() != Qt::LeftButton)
                return Utils::StyledBar::eventFilter(obj, event);
            if ((me->pos() - d->m_dragStartPosition).manhattanLength()
                    < QApplication::startDragDistance())
                return Utils::StyledBar::eventFilter(obj, event);
            DocumentModel::Entry *entry = DocumentModel::entryAtRow(
                        d->m_editorList->currentIndex());
            if (!entry) // no document
                return Utils::StyledBar::eventFilter(obj, event);
            auto drag = new QDrag(this);
            auto data = new Utils::DropMimeData;
            data->addFile(entry->filePath());
            drag->setMimeData(data);
            Qt::DropAction action = drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
            if (action == Qt::MoveAction)
                emit currentDocumentMoved();
            return true;
        }
    }
    return Utils::StyledBar::eventFilter(obj, event);
}

void EditorToolBar::setNavigationVisible(bool isVisible)
{
    d->m_goBackAction->setVisible(isVisible);
    d->m_goForwardAction->setVisible(isVisible);
    d->m_backButton->setVisible(isVisible);
    d->m_forwardButton->setVisible(isVisible);
}

} // Core
