// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "debuggertooltipmanager.h"

#include "debuggeractions.h"
#include "debuggerengine.h"
#include "debuggerinternalconstants.h"
#include "debuggermainwindow.h"
#include "debuggerprotocol.h"
#include "debuggertr.h"
#include "sourceutils.h"
#include "stackhandler.h"
#include "watchhandler.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/modemanager.h>
#include <coreplugin/session.h>

#include <cppeditor/cppprojectfile.h>

#include <texteditor/texteditor.h>
#include <texteditor/textdocument.h>

#include <utils/algorithm.h>
#include <utils/qtcassert.h>
#include <utils/tooltip/tooltip.h>
#include <utils/stringutils.h>
#include <utils/treemodel.h>
#include <utils/utilsicons.h>

#include <QAbstractItemModel>
#include <QDebug>
#include <QGuiApplication>
#include <QLabel>
#include <QScrollBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Core;
using namespace ProjectExplorer;
using namespace TextEditor;
using namespace Utils;

namespace Debugger::Internal {

//#define DEBUG(x) qDebug() << x
#define DEBUG(x)

enum DebuggerTooltipState
{
    New, // All new, widget not shown, not async (yet)
    PendingUnshown, // Widget not (yet) shown, async.
    PendingShown, // Widget shown, async
    Acquired, // Widget shown sync, engine attached
};

class DebuggerToolTip;

class DebuggerToolTipManagerPrivate final : public QObject
{
public:
    explicit DebuggerToolTipManagerPrivate(DebuggerEngine *engine)
        : m_engine(engine)
    {
        connect(ModeManager::instance(), &ModeManager::currentModeChanged,
                this, &DebuggerToolTipManagerPrivate::onModeChanged);
        connect(SessionManager::instance(), &SessionManager::aboutToUnloadSession,
                this, &DebuggerToolTipManagerPrivate::sessionAboutToChange);

        EditorManager *em = EditorManager::instance();
        connect(em, &EditorManager::currentEditorChanged,
                this, &DebuggerToolTipManagerPrivate::updateVisibleToolTips);
        connect(em, &EditorManager::editorOpened,
                this, &DebuggerToolTipManagerPrivate::slotEditorOpened);

        for (IEditor *e : DocumentModel::editorsForOpenedDocuments())
            slotEditorOpened(e);
    }

    void slotTooltipOverrideRequested(TextEditor::TextEditorWidget *editorWidget,
                                      const QPoint &point, int pos, bool *handled);
    void slotEditorOpened(Core::IEditor *e);
    void hideAllToolTips();
    void purgeClosedToolTips();

    void onModeChanged(Id mode)
    {
        Q_UNUSED(mode)
        // if (mode == Constants::MODE_DEBUG)
        updateVisibleToolTips();
    }

    void setupEditors();

    void sessionAboutToChange();

    void updateVisibleToolTips();
    void closeAllToolTips();

    bool eventFilter(QObject *, QEvent *) override;

    bool debugModeActive() const { return ModeManager::currentModeId() == Constants::MODE_DEBUG; }

    DebuggerToolTip *findToolTip(TextEditorWidget *editorWidget,
                                 const DebuggerToolTipContext &context);

public:
    DebuggerEngine *m_engine;
    QList<QPointer<DebuggerToolTip>> m_tooltips;
};

// A label that can be dragged to drag something else.

class DraggableLabel : public QLabel
{
public:
    explicit DraggableLabel(QWidget *target)
        : m_target(target), m_moveStartPos(-1, -1), active(false)
    {}

    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

public:
    QWidget *m_target;
    QPoint m_moveStartPos;
    QPoint m_offset;
    bool active;
};

void DraggableLabel::mousePressEvent(QMouseEvent * event)
{
    if (active && event->button() == Qt::LeftButton) {
        m_moveStartPos = event->globalPosition().toPoint();
        event->accept();
    }
    QLabel::mousePressEvent(event);
}

void DraggableLabel::mouseReleaseEvent(QMouseEvent * event)
{
    if (active && event->button() == Qt::LeftButton)
        m_moveStartPos = QPoint(-1, -1);
    QLabel::mouseReleaseEvent(event);
}

void DraggableLabel::mouseMoveEvent(QMouseEvent * event)
{
    if (active && (event->buttons() & Qt::LeftButton)) {
        if (m_moveStartPos != QPoint(-1, -1)) {
            const QPoint newPos = event->globalPosition().toPoint();
            const QPoint offset = newPos - m_moveStartPos;

            m_target->move(m_target->pos() + offset);
            m_offset += offset;

            m_moveStartPos = newPos;
        }
        event->accept();
    }
    QLabel::mouseMoveEvent(event);
}

/////////////////////////////////////////////////////////////////////////
//
// ToolTipWatchItem
//
/////////////////////////////////////////////////////////////////////////

class ToolTipWatchItem : public TreeItem
{
public:
    ToolTipWatchItem() = default;
    ToolTipWatchItem(TreeItem *item);

    bool hasChildren() const override { return expandable; }
    bool canFetchMore() const override { return childCount() == 0 && expandable && model(); }
    void fetchMore() override {}
    QVariant data(int column, int role) const override;

public:
    QString name;
    QString value;
    QString type;
    QString expression;
    QColor valueColor;
    bool expandable = false;
    QString iname;
};

ToolTipWatchItem::ToolTipWatchItem(TreeItem *item)
{
    const QAbstractItemModel *model = item->model();
    QModelIndex idx = item->index();
    name = model->data(idx.sibling(idx.row(), WatchModelBase::NameColumn), Qt::DisplayRole).toString();
    value = model->data(idx.sibling(idx.row(), WatchModelBase::ValueColumn), Qt::DisplayRole).toString();
    type = model->data(idx.sibling(idx.row(), WatchModelBase::TypeColumn), Qt::DisplayRole).toString();
    iname = model->data(idx.sibling(idx.row(), WatchModelBase::NameColumn), LocalsINameRole).toString();
    valueColor = model->data(idx.sibling(idx.row(), WatchModelBase::ValueColumn), Qt::ForegroundRole).value<QColor>();
    expandable = model->hasChildren(idx);
    expression = model->data(idx.sibling(idx.row(), WatchModelBase::NameColumn), Qt::EditRole).toString();
    for (TreeItem *child : *item)
        appendChild(new ToolTipWatchItem(child));
}

/////////////////////////////////////////////////////////////////////////
//
// ToolTipModel
//
/////////////////////////////////////////////////////////////////////////

class ToolTipModel : public TreeModel<ToolTipWatchItem>
{
public:
    ToolTipModel()
    {
        setHeader({Tr::tr("Name"), Tr::tr("Value"), Tr::tr("Type")});
        m_enabled = true;
        auto item = new ToolTipWatchItem;
        item->expandable = true;
        setRootItem(item);
    }

    void expandNode(const QModelIndex &idx)
    {
        if (!m_engine)
            return;

        m_expandedINames.insert(idx.data(LocalsINameRole).toString());
        if (canFetchMore(idx)) {
            if (!idx.isValid())
                return;

            if (auto item = dynamic_cast<ToolTipWatchItem *>(itemForIndex(idx))) {
                WatchItem *it = m_engine->watchHandler()->findItem(item->iname);
                if (QTC_GUARD(it))
                    it->model()->fetchMore(it->index());
            }
        }
    }

    void collapseNode(const QModelIndex &idx)
    {
        m_expandedINames.remove(idx.data(LocalsINameRole).toString());
    }

    QPointer<DebuggerEngine> m_engine;
    QSet<QString> m_expandedINames;
    bool m_enabled;
};

QVariant ToolTipWatchItem::data(int column, int role) const
{
    switch (role) {
        case Qt::DisplayRole: {
            switch (column) {
                case 0:
                    return name;
                case 1:
                    return value;
                case 2:
                    return type;
            }
            break;
        }

        case LocalsINameRole:
            return iname;

        case Qt::ForegroundRole:
            if (model() && static_cast<ToolTipModel *>(model())->m_enabled) {
                if (column == 1)
                    return valueColor;
                return QVariant();
            }
            return QColor(140, 140, 140);

        default:
            break;
    }
    return QVariant();
}

/*!
    \class Debugger::Internal::DebuggerToolTipTreeView

    \brief The DebuggerToolTipTreeView class is a treeview that adapts its size
    to the model contents (also while expanding)
    to be used within DebuggerTreeViewToolTipWidget.

*/

class DebuggerToolTipTreeView : public QTreeView
{
public:
    explicit DebuggerToolTipTreeView(QWidget *parent)
        : QTreeView(parent)
    {
        setHeaderHidden(true);
        setEditTriggers(NoEditTriggers);
        setUniformRowHeights(true);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }

    QSize sizeHint() const override { return m_size; }

    int sizeHintForColumn(int column) const override
    {
        return QTreeView::sizeHintForColumn(column);
    }

    int computeHeight(const QModelIndex &index) const
    {
        int s = rowHeight(index);
        const int rowCount = model()->rowCount(index);
        for (int i = 0; i < rowCount; ++i)
            s += computeHeight(model()->index(i, 0, index));
        return s;
    }

    QSize m_size;
};


/////////////////////////////////////////////////////////////////////////
//
// DebuggerToolTip
//
/////////////////////////////////////////////////////////////////////////

class DebuggerToolTip : public QWidget
{
public:
    DebuggerToolTip(DebuggerEngine *engine,
                    const DebuggerToolTipContext &context,
                    TextEditorWidget *editorWidget);

    ~DebuggerToolTip() override { DEBUG("DESTROY DEBUGGERTOOLTIP WIDGET"); }

    void positionShow();

    void updateTooltip();

    void setState(DebuggerTooltipState newState);

    void closeEvent(QCloseEvent *) override { DEBUG("CLOSE DEBUGGERTOOLTIP WIDGET"); }

    void enterEvent(QEnterEvent *) override { DEBUG("ENTER DEBUGGERTOOLTIP WIDGET"); }

    void leaveEvent(QEvent *) override
    {
        DEBUG("LEAVE DEBUGGERTOOLTIP WIDGET");
        if (BaseTextEditor *editor = BaseTextEditor::currentTextEditor())
            editor->editorWidget()->activateWindow();
    }

    void pin()
    {
        if (isPinned)
            return;
        isPinned = true;
        pinButton->setIcon(style()->standardIcon(QStyle::SP_DockWidgetCloseButton));

        if (parentWidget()) {
            // We are currently within a text editor tooltip:
            // Rip out of parent widget and re-show as a tooltip
            // Find parent with different window than the tooltip itself:
            QWidget *top = parentWidget();
            while (top->window() == window() && top->parentWidget())
                top = top->parentWidget();
            ToolTip::pinToolTip(this, top->window());
        } else {
            // We have just be restored from session data.
            setWindowFlags(Qt::ToolTip);
        }
        titleLabel->active = true; // User can now drag
    }

    void computeSize();

    void setContents(ToolTipWatchItem *item)
    {
        titleLabel->setText(item->expression);
        //treeView->setEnabled(true);
        model.m_enabled = true;
        if (item) {
            model.rootItem()->removeChildren();
            model.rootItem()->appendChild(item);
        }
        reexpand(QModelIndex());
        computeSize();
    }

    void reexpand(const QModelIndex &idx)
    {
        TreeItem *item = model.itemForIndex(idx);
        QTC_ASSERT(item, return);
        QString iname = item->data(0, LocalsINameRole).toString();
        bool shouldExpand = model.m_expandedINames.contains(iname);
        if (shouldExpand) {
            if (!treeView->isExpanded(idx)) {
                treeView->expand(idx);
                for (int i = 0, n = model.rowCount(idx); i != n; ++i) {
                    QModelIndex idx1 = model.index(i, 0, idx);
                    reexpand(idx1);
                }
            }
        } else {
            if (treeView->isExpanded(idx))
                treeView->collapse(idx);
        }
    }

public:
    QPointer<DebuggerEngine> engine;
    DebuggerToolTipContext context;
    DebuggerTooltipState state;

    bool isPinned;
    QToolButton *pinButton;
    DraggableLabel *titleLabel;
    DebuggerToolTipTreeView *treeView;
    ToolTipModel model;
    QPointer<TextEditorWidget> editorWidget;
};

DebuggerToolTip::DebuggerToolTip(DebuggerEngine *engine,
                                 const DebuggerToolTipContext &context,
                                 TextEditorWidget *editorWidget)
    : engine(engine), context(context), editorWidget(editorWidget)
{
    setObjectName("DebuggerTreeViewToolTipWidget: " + context.iname);
    setAttribute(Qt::WA_DeleteOnClose);

    model.m_engine = engine;
    state = New;

    isPinned = false;
    const QIcon pinIcon = Utils::Icons::PINNED_SMALL.icon();

    pinButton = new QToolButton;
    pinButton->setIcon(pinIcon);
    QObject::connect(pinButton, &QAbstractButton::clicked, this, [this] {
        if (isPinned)
            close();
        else
            pin();
    });

    auto copyButton = new QToolButton;
    copyButton->setToolTip(Tr::tr("Copy Contents to Clipboard"));
    copyButton->setIcon(Utils::Icons::COPY.icon());

    titleLabel = new DraggableLabel(this);
    titleLabel->setMinimumWidth(40); // Ensure a draggable area even if text is empty.
    titleLabel->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);

    auto toolBar = new QToolBar(this);
    toolBar->setProperty("_q_custom_style_disabled", QVariant(true));
    toolBar->setIconSize({12, 12});
    toolBar->addWidget(pinButton);
    toolBar->addWidget(copyButton);
    toolBar->addWidget(titleLabel);

    treeView = new DebuggerToolTipTreeView(this);
    treeView->setFocusPolicy(Qt::NoFocus);
    treeView->setModel(&model);

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(toolBar);
    mainLayout->addWidget(treeView);

    connect(copyButton, &QAbstractButton::clicked, this, [this] {
        QString text;
        QTextStream str(&text);
        model.forAllItems([&str](ToolTipWatchItem *item) {
            str << QString(item->level(), '\t')
                << item->name << '\t' << item->value << '\t' << item->type << '\n';
        });
        setClipboardAndSelection(text);
    });

    connect(treeView, &QTreeView::expanded, &model, &ToolTipModel::expandNode);
    connect(treeView, &QTreeView::collapsed, &model, &ToolTipModel::collapseNode);

    connect(treeView, &QTreeView::collapsed, this, &DebuggerToolTip::computeSize,
        Qt::QueuedConnection);
    connect(treeView, &QTreeView::expanded, this, &DebuggerToolTip::computeSize,
        Qt::QueuedConnection);
    DEBUG("CREATE DEBUGGERTOOLTIP WIDGET");
}

void DebuggerToolTip::computeSize()
{
    int columns = 30; // Decoration
    int rows = 0;
    bool rootDecorated = false;

    reexpand(model.index(0, 0, QModelIndex()));
    const int columnCount = model.columnCount(QModelIndex());
    rootDecorated = model.rowCount() > 0;
    if (rootDecorated) {
        for (int i = 0; i < columnCount; ++i) {
            treeView->resizeColumnToContents(i);
            columns += treeView->sizeHintForColumn(i);
        }
    }
    if (columns < 100)
        columns = 100; // Prevent toolbar from shrinking when displaying 'Previous'
    rows += treeView->computeHeight(QModelIndex());

    // Fit tooltip to screen, showing/hiding scrollbars as needed.
    // Add a bit of space to account for tooltip border, and not
    // touch the border of the screen.
    QPoint pos(x(), y());
    auto screen = QGuiApplication::screenAt(pos);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    QRect desktopRect = screen->availableGeometry();
    const int maxWidth = desktopRect.right() - pos.x() - 5 - 5;
    const int maxHeight = desktopRect.bottom() - pos.y() - 5 - 5;

    if (columns > maxWidth)
        rows += treeView->horizontalScrollBar()->height();

    if (rows > maxHeight) {
        treeView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        rows = maxHeight;
        columns += treeView->verticalScrollBar()->width();
    } else {
        treeView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }

    if (columns > maxWidth) {
        treeView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        columns = maxWidth;
    } else {
        treeView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }

    treeView->m_size = QSize(columns + 5, rows + 5);
    treeView->setMinimumSize(treeView->m_size);
    treeView->setMaximumSize(treeView->m_size);
    treeView->setRootIsDecorated(rootDecorated);
}

/*!
    \class Debugger::Internal::DebuggerToolTipContext

    \brief The DebuggerToolTipContext class specifies the file name and
    position where the tooltip is anchored.

    Uses redundant position or line column information to detect whether
    the underlying file has been changed
    on restoring.
*/

static bool filesMatch(const FilePath &file1, const FilePath &file2)
{
    return file1.canonicalPath() == file2.canonicalPath();
}

bool DebuggerToolTipContext::matchesFrame(const StackFrame &frame) const
{
    return (fileName.isEmpty() || frame.file.isEmpty() || filesMatch(fileName, frame.file))
            //&& (function.isEmpty() || frame.function.isEmpty() || function == frame.function);
            && (frame.line <= 0 || (scopeFromLine <= frame.line && frame.line <= scopeToLine));
}

bool DebuggerToolTipContext::isSame(const DebuggerToolTipContext &other) const
{
    return iname == other.iname
        && scopeFromLine == other.scopeFromLine
        && scopeToLine == other.scopeToLine
        && filesMatch(fileName, other.fileName);
}

QString DebuggerToolTipContext::toolTip() const
{
    return Tr::tr("Expression %1 in function %2 from line %3 to %4")
            .arg(expression).arg(function).arg(scopeFromLine).arg(scopeToLine);
}

QDebug operator<<(QDebug d, const DebuggerToolTipContext &c)
{
    QDebug nsp = d.nospace();
    nsp << c.fileName << '@' << c.line << ',' << c.column << " (" << c.position << ')'
        << "INAME: " << c.iname << " EXP: " << c.expression << " FUNCTION: " << c.function;
    return d;
}

/*!
    \class Debugger::Internal::DebuggerToolTip

    \brief The DebuggerToolTip class is a pinnable debugger tool tip
    widget.

    The debugger tooltip goes from the unpinned state (button
    showing 'Pin') to the pinned state (button showing 'Close').
    It consists of a title toolbar and a vertical main layout.
    With the engine acquired, it sets a filter model (by expression) on
    one of the engine's models (debuggerModel).

    It is associated with file name and position with functionality to
    acquire and release the engine. When the debugger stops at a file, all
    matching tooltips acquire the engine, that is, display the engine data.
    When continuing or switching away from the frame, the tooltips release the
    engine, that is, store the data internally and keep displaying them
    marked as 'previous'.

    The widget is that is first shown by the TextEditor's tooltip
    class and typically closed by it unless the user pins it.
    In that case, it is removed from the tip's layout, added to the DebuggerToolTipManager's
    list of pinned tooltips and re-shown as a global tooltip widget.
    As the debugger stop and continues, it shows the debugger values or a copy
    of them. On closing or session changes, the contents it saved.
*/

// This is called back from the engines after they populated the
// WatchModel. If the populating result from evaluation of this
// tooltip here, we are in "PendingUnshown" state (no Widget show yet),
// or "PendingShown" state (old widget reused).
//
// If we are in "Acquired" or "Released", this is an update
// after normal WatchModel update.

void DebuggerToolTip::updateTooltip()
{
    if (!engine) {
        close();
        return;
    }

    StackFrame frame = engine->stackHandler()->currentFrame();
    WatchItem *item = engine->watchHandler()->findItem(context.iname);

    // FIXME: The engine should decide on whether it likes
    // the context.
    const bool sameFrame = context.matchesFrame(frame) || context.fileName.endsWith(".py");

    if (state == PendingUnshown) {
        setState(PendingShown);
        ToolTip::show(context.mousePosition, this, DebuggerMainWindow::instance());
    }

    if (item && sameFrame) {
        DEBUG("ACQUIRE ENGINE: STATE " << state);
        setContents(new ToolTipWatchItem(item));
    } else {
        close();
    }
    titleLabel->setToolTip(context.toolTip());
}

void DebuggerToolTip::setState(DebuggerTooltipState newState)
{
    bool ok = (state == New && newState == PendingUnshown)
        || (state == New && newState == Acquired)
        || (state == PendingUnshown && newState == PendingShown);

    DEBUG("TRANSITION STATE FROM " << state << " TO " << newState);
    QTC_ASSERT(ok, qDebug() << "Unexpected tooltip state transition from "
                            << state << " to " << newState);

    state = newState;
}

void DebuggerToolTip::positionShow()
{
    // Figure out new position of tooltip using the text edit.
    // If the line changed too much, close this tip.
    QTC_ASSERT(editorWidget, return);
    QTextCursor cursor = editorWidget->textCursor();
    cursor.setPosition(context.position);
    const int line = cursor.blockNumber();
    if (qAbs(context.line - line) > 2) {
        close();
        return ;
    }

    const QPoint screenPos = editorWidget->toolTipPosition(cursor) + titleLabel->m_offset;
    const QRect toolTipArea = QRect(screenPos, QSize(sizeHint()));
    const QRect plainTextArea = QRect(editorWidget->mapToGlobal(QPoint(0, 0)),
                                      editorWidget->size());
    const bool visible = plainTextArea.intersects(toolTipArea);
    //    DEBUG("DebuggerToolTip::positionShow() " << this << m_context
    //             << " line: " << line << " plainTextPos " << toolTipArea
    //             << " offset: " << m_titleLabel->m_offset
    //             << " Area: " << plainTextArea << " Screen pos: "
    //             << screenPos << te.widget << " visible=" << visible);

    if (visible) {
        topLevelWidget()->move(screenPos);
        show();
    } else {
        hide();
    }
}

/*!
    \class Debugger::Internal::DebuggerToolTipManager

    \brief The DebuggerToolTipManager class manages the pinned tooltip widgets,
    listens on editor scroll and main window move
    events and takes care of repositioning the tooltips.

    Listens to editor change and mode change. In debug mode, if there are tooltips
    for the current editor (by file name), positions and shows them.

    In addition, listens on state change and stack frame completed signals
    of the engine. If a stack frame is completed, has all matching tooltips
    (by file name and function) acquire the engine, others release.
*/

DebuggerToolTipManager::DebuggerToolTipManager(DebuggerEngine *engine)
    : d(new DebuggerToolTipManagerPrivate(engine))
{
}

DebuggerToolTipManager::~DebuggerToolTipManager()
{
    delete d;
}

void DebuggerToolTipManagerPrivate::hideAllToolTips()
{
    purgeClosedToolTips();
    for (const auto &tooltip : std::as_const(m_tooltips)) {
        if (tooltip)
            tooltip->hide();
    }
}

void DebuggerToolTipManagerPrivate::updateVisibleToolTips()
{
    purgeClosedToolTips();
    if (m_tooltips.empty())
        return;

    if (!debugModeActive()) {
        hideAllToolTips();
        return;
    }

    const QList<IEditor *> visibleEditors = EditorManager::visibleEditors();

    for (const auto &tooltip : std::as_const(m_tooltips)) {
        QTC_ASSERT(tooltip, continue);
        bool found = false;
        for (const IEditor *editor : visibleEditors) {
            QWidget *w = TextEditorWidget::fromEditor(editor);
            if (w == tooltip->editorWidget) {
                found = true;
                break;
            }
        }

        if (found)
            tooltip->positionShow();
        else
            tooltip->hide();
    }
}

void DebuggerToolTipManager::updateToolTips()
{
    d->purgeClosedToolTips();
    if (d->m_tooltips.empty())
        return;

    // Stack frame changed: All tooltips of that file acquire the engine,
    // all others release (arguable, this could be more precise?)
    for (const auto &tooltip : std::as_const(d->m_tooltips)) {
        if (tooltip)
            tooltip->updateTooltip();
    }
    d->updateVisibleToolTips(); // Move tooltip when stepping in same file.
}

void DebuggerToolTipManager::deregisterEngine()
{
    DEBUG("DEREGISTER ENGINE");

    d->purgeClosedToolTips();

    for (const auto &tooltip : std::as_const(d->m_tooltips)) {
        if (tooltip && tooltip->context.engineType == d->m_engine->objectName())
            tooltip->close();
    }

    // FIXME: For now remove all.
    for (const auto &tooltip : std::as_const(d->m_tooltips)) {
        if (tooltip)
            tooltip->close();
    }
    d->purgeClosedToolTips();
}

bool DebuggerToolTipManager::hasToolTips() const
{
    return !d->m_tooltips.empty();
}

void DebuggerToolTipManagerPrivate::sessionAboutToChange()
{
    closeAllToolTips();
}

void DebuggerToolTipManager::closeAllToolTips()
{
    d->closeAllToolTips();
}

void DebuggerToolTipManagerPrivate::closeAllToolTips()
{
    for (const auto &tooltip : std::as_const(m_tooltips)) {
        if (tooltip)
            tooltip->close();
    }
    m_tooltips.clear();
}

void DebuggerToolTipManager::resetLocation()
{
    d->purgeClosedToolTips();
    for (const auto &tooltip : std::as_const(d->m_tooltips)) {
        if (tooltip)
            tooltip->pin();
    }
}

void DebuggerToolTipManagerPrivate::slotTooltipOverrideRequested
    (TextEditorWidget *editorWidget, const QPoint &point, int pos, bool *handled)
{
    QTC_ASSERT(handled, return);
    QTC_ASSERT(editorWidget, return);

    if (!settings().useToolTipsInMainEditor())
        return;

    const TextDocument *document = editorWidget->textDocument();
    if (!m_engine || !m_engine->canDisplayTooltip())
        return;

    DebuggerToolTipContext context;
    context.engineType = m_engine->objectName();
    context.fileName = document->filePath();
    context.position = pos;
    editorWidget->convertPosition(pos, &context.line, &context.column);
    ++context.column;
    QString raw = cppExpressionAt(editorWidget, context.position, &context.line, &context.column,
                                  &context.function, &context.scopeFromLine, &context.scopeToLine);
    context.expression = fixCppExpression(raw);
    context.isCppEditor = CppEditor::ProjectFile::classify(document->filePath())
                            != CppEditor::ProjectFile::Unsupported;

    if (context.expression.isEmpty()) {
        ToolTip::show(point, Tr::tr("No valid expression"), editorWidget);
        *handled = true;
        return;
    }

    purgeClosedToolTips();

    // Prefer a filter on an existing local variable if it can be found.
    const WatchItem *localVariable = m_engine->watchHandler()->findCppLocalVariable(context.expression);
    if (localVariable) {
        context.expression = localVariable->exp;
        if (context.expression.isEmpty())
            context.expression = localVariable->name;
        context.iname = localVariable->iname;

        DebuggerToolTip *tooltip = findToolTip(editorWidget, context);

        if (tooltip) {
            DEBUG("REUSING LOCALS TOOLTIP");
            tooltip->context.mousePosition = point;
            ToolTip::move(point);
        } else {
            DEBUG("CREATING LOCALS, WAITING...");
            tooltip = new DebuggerToolTip(m_engine, context, editorWidget);
            tooltip->setState(Acquired);
            m_tooltips.append(tooltip);
            ToolTip::show(point, tooltip, editorWidget);
        }
        DEBUG("SYNC IN STATE" << tooltip->state);
        tooltip->updateTooltip();

    } else {

        context.iname = "tooltip." + toHex(context.expression);

        DebuggerToolTip *tooltip = findToolTip(editorWidget, context);

        if (tooltip) {
            tooltip->context.mousePosition = point;
            ToolTip::move(point);
            DEBUG("UPDATING DELAYED.");
        } else {
            DEBUG("CREATING DELAYED.");
            tooltip = new DebuggerToolTip(m_engine, context, editorWidget);
            tooltip->context.mousePosition = point;
            m_tooltips.append(tooltip);
            tooltip->setState(PendingUnshown);
            if (m_engine->canHandleToolTip(context)) {
                m_engine->updateItem(context.iname);
            } else {
                ToolTip::show(point, Tr::tr("Expression too complex"), editorWidget);
                tooltip->close();
            }
        }
    }

    *handled = true;
}

void DebuggerToolTipManagerPrivate::slotEditorOpened(IEditor *e)
{
    // Move tooltip along when scrolled.
    if (auto textEditor = qobject_cast<BaseTextEditor *>(e)) {
        TextEditorWidget *widget = textEditor->editorWidget();
        QObject::connect(widget->verticalScrollBar(), &QScrollBar::valueChanged,
                         this, &DebuggerToolTipManagerPrivate::updateVisibleToolTips);
        QObject::connect(widget, &TextEditorWidget::tooltipOverrideRequested,
                         this, &DebuggerToolTipManagerPrivate::slotTooltipOverrideRequested);

        // Apparently the widget's window is still the original one once the
        // EditorManager::editorOpened() is fired.
        QTimer::singleShot(0, this, [this, widgetp = QPointer<QWidget>(widget)] {
            QTC_ASSERT(widgetp, return);
            QTC_ASSERT(widgetp->window(), return);
            widgetp->window()->installEventFilter(this);
        });
    }
}

DebuggerToolTipContexts DebuggerToolTipManager::pendingTooltips() const
{
    StackFrame frame = d->m_engine->stackHandler()->currentFrame();
    DebuggerToolTipContexts rc;
    d->purgeClosedToolTips();
    for (const auto &tooltip : std::as_const(d->m_tooltips)) {
        if (tooltip && tooltip->context.iname.startsWith("tooltip") && tooltip->context.matchesFrame(frame))
            rc.push_back(tooltip->context);
    }
    return rc;
}

bool DebuggerToolTipManagerPrivate::eventFilter(QObject *o, QEvent *e)
{
    if (m_tooltips.empty())
        return false;
    switch (e->type()) {
    case QEvent::Move: { // Move along with parent (toplevel)
        const auto me = static_cast<const QMoveEvent *>(e);
        const QPoint dist = me->pos() - me->oldPos();
        purgeClosedToolTips();
        QList<QPointer<DebuggerToolTip>> affectedTooltips;
        for (auto &tooltip : m_tooltips) {
            if (tooltip && tooltip->editorWidget && tooltip->editorWidget->window() == o)
                affectedTooltips.append(tooltip);
        }
        for (const QPointer<DebuggerToolTip> &tooltip : std::as_const(affectedTooltips)) {
            if (tooltip && tooltip->isVisible())
                tooltip->move(tooltip->pos() + dist);
        }
        break;
    }
    case QEvent::WindowStateChange: { // Hide/Show along with parent (toplevel)
        const auto se = static_cast<const QWindowStateChangeEvent *>(e);
        const bool wasMinimized = se->oldState() & Qt::WindowMinimized;
        const bool isMinimized  = static_cast<const QWidget *>(o)->windowState() & Qt::WindowMinimized;
        if (wasMinimized != isMinimized) {
            purgeClosedToolTips();
            QList<QPointer<DebuggerToolTip>> affectedTooltips;
            for (auto &tooltip : m_tooltips) {
                if (tooltip && tooltip->editorWidget && tooltip->editorWidget->window() == o)
                    affectedTooltips.append(tooltip);
            }
            for (DebuggerToolTip *tooltip : std::as_const(affectedTooltips))
                tooltip->setVisible(!isMinimized);
        }
        break;
    }
    default:
        break;
    }
    return false;
}

DebuggerToolTip *DebuggerToolTipManagerPrivate::findToolTip(TextEditorWidget *editorWidget,
                                                            const DebuggerToolTipContext &context)
{
    for (const auto &tooltip : std::as_const(m_tooltips)) {
        if (tooltip && tooltip->editorWidget == editorWidget && tooltip->context.isSame(context))
            return tooltip;
    }
    return nullptr;
}

void DebuggerToolTipManagerPrivate::purgeClosedToolTips()
{
    m_tooltips = Utils::filtered(m_tooltips, [](const QPointer<DebuggerToolTip> &tooltip) {
        return bool(tooltip);
    });
}

} // Debugger::Internal
