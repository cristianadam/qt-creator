// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "plaintextedit.h"

#include <QAccessible>
#include <QBuffer>
#include <QClipboard>
#include <QDesktopServices>
#include <QDrag>
#include <QGestureEvent>
#include <QGraphicsSceneEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMetaMethod>
#include <QMimeData>
#include <QPrinter>
#include <QScrollBar>
#include <QStyleHints>
#include <QStyleOption>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTextDocumentWriter>
#include <QTextList>
#include <QTextTableCell>
#include <QToolTip>

namespace TextEditor {

class UnicodeControlCharacterMenu : public QMenu
{
    Q_OBJECT
public:
    UnicodeControlCharacterMenu(QObject *editWidget, QWidget *parent);

private Q_SLOTS:
    void menuActionTriggered();

private:
    QObject *editWidget;
};

class TextEditMimeData : public QMimeData
{
public:
    inline TextEditMimeData(const QTextDocumentFragment &aFragment) : fragment(aFragment) {}

    virtual QStringList formats() const override;
    bool hasFormat(const QString &format) const override;

protected:
    virtual QVariant retrieveData(const QString &mimeType, QMetaType type) const override;
private:
    void setup() const;

    mutable QTextDocumentFragment fragment;
};

class InputControl : public QObject
{
    Q_OBJECT
public:
    enum Type {
        LineEdit,
        TextEdit
    };

    explicit InputControl(Type type, QObject *parent = nullptr);

    bool isAcceptableInput(const QKeyEvent *event) const;
    static bool isCommonTextEditShortcut(const QKeyEvent *ke);

private:
    const Type m_type;
};

InputControl::InputControl(Type type, QObject *parent)
    : QObject(parent)
    , m_type(type)
{
}

bool InputControl::isAcceptableInput(const QKeyEvent *event) const
{
    const QString text = event->text();
    if (text.isEmpty())
        return false;

    const QChar c = text.at(0);

    // Formatting characters such as ZWNJ, ZWJ, RLM, etc. This needs to go before the
    // next test, since CTRL+SHIFT is sometimes used to input it on Windows.
    if (c.category() == QChar::Other_Format)
        return true;

    // QTBUG-35734: ignore Ctrl/Ctrl+Shift; accept only AltGr (Alt+Ctrl) on German keyboards
    if (event->modifiers() == Qt::ControlModifier
        || event->modifiers() == (Qt::ShiftModifier | Qt::ControlModifier)) {
        return false;
    }

    if (c.isPrint())
        return true;

    if (c.category() == QChar::Other_PrivateUse)
        return true;

    if (c.isHighSurrogate() && text.size() > 1 && text.at(1).isLowSurrogate())
        return true;

    if (m_type == TextEdit && c == u'\t')
        return true;

    return false;
}

bool InputControl::isCommonTextEditShortcut(const QKeyEvent *ke)
{
    if (ke->modifiers() == Qt::NoModifier
        || ke->modifiers() == Qt::ShiftModifier
        || ke->modifiers() == Qt::KeypadModifier) {
        if (ke->key() < Qt::Key_Escape) {
            return true;
        } else {
            switch (ke->key()) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Delete:
            case Qt::Key_Home:
            case Qt::Key_End:
            case Qt::Key_Backspace:
            case Qt::Key_Left:
            case Qt::Key_Right:
            case Qt::Key_Up:
            case Qt::Key_Down:
            case Qt::Key_Tab:
                return true;
            default:
                break;
            }
        }
#if QT_CONFIG(shortcut)
    } else if (ke->matches(QKeySequence::Copy)
               || ke->matches(QKeySequence::Paste)
               || ke->matches(QKeySequence::Cut)
               || ke->matches(QKeySequence::Redo)
               || ke->matches(QKeySequence::Undo)
               || ke->matches(QKeySequence::MoveToNextWord)
               || ke->matches(QKeySequence::MoveToPreviousWord)
               || ke->matches(QKeySequence::MoveToStartOfDocument)
               || ke->matches(QKeySequence::MoveToEndOfDocument)
               || ke->matches(QKeySequence::SelectNextWord)
               || ke->matches(QKeySequence::SelectPreviousWord)
               || ke->matches(QKeySequence::SelectStartOfLine)
               || ke->matches(QKeySequence::SelectEndOfLine)
               || ke->matches(QKeySequence::SelectStartOfBlock)
               || ke->matches(QKeySequence::SelectEndOfBlock)
               || ke->matches(QKeySequence::SelectStartOfDocument)
               || ke->matches(QKeySequence::SelectEndOfDocument)
               || ke->matches(QKeySequence::SelectAll)
               ) {
        return true;
#endif
    }
    return false;
}

class WidgetTextControlPrivate;

class WidgetTextControl : public InputControl
{
    Q_OBJECT
#ifndef QT_NO_TEXTHTMLPARSER
    Q_PROPERTY(QString html READ toHtml WRITE setHtml NOTIFY textChanged USER true)
#endif
    Q_PROPERTY(bool overwriteMode READ overwriteMode WRITE setOverwriteMode)
    Q_PROPERTY(bool acceptRichText READ acceptRichText WRITE setAcceptRichText)
    Q_PROPERTY(int cursorWidth READ cursorWidth WRITE setCursorWidth)
    Q_PROPERTY(Qt::TextInteractionFlags textInteractionFlags READ textInteractionFlags
            WRITE setTextInteractionFlags)
    Q_PROPERTY(bool openExternalLinks READ openExternalLinks WRITE setOpenExternalLinks)
    Q_PROPERTY(bool ignoreUnusedNavigationEvents READ ignoreUnusedNavigationEvents
            WRITE setIgnoreUnusedNavigationEvents)
public:
    explicit WidgetTextControl(QObject *parent = nullptr);
    explicit WidgetTextControl(const QString &text, QObject *parent = nullptr);
    explicit WidgetTextControl(QTextDocument *doc, QObject *parent = nullptr);
    virtual ~WidgetTextControl();

    void setDocument(QTextDocument *document);
    QTextDocument *document() const;

    void setTextCursor(const QTextCursor &cursor, bool selectionClipboard = false);
    QTextCursor textCursor() const;

    void setTextInteractionFlags(Qt::TextInteractionFlags flags);
    Qt::TextInteractionFlags textInteractionFlags() const;

    void mergeCurrentCharFormat(const QTextCharFormat &modifier);

    void setCurrentCharFormat(const QTextCharFormat &format);
    QTextCharFormat currentCharFormat() const;

    bool find(const QString &exp, QTextDocument::FindFlags options = { });
#if QT_CONFIG(regularexpression)
    bool find(const QRegularExpression &exp, QTextDocument::FindFlags options = { });
#endif

    QString toPlainText() const;
#ifndef QT_NO_TEXTHTMLPARSER
    QString toHtml() const;
#endif
#if QT_CONFIG(textmarkdownwriter)
    QString toMarkdown(QTextDocument::MarkdownFeatures features = QTextDocument::MarkdownDialectGitHub) const;
#endif

    virtual void ensureCursorVisible();

    Q_INVOKABLE virtual QVariant loadResource(int type, const QUrl &name);
#ifndef QT_NO_CONTEXTMENU
    QMenu *createStandardContextMenu(const QPointF &pos, QWidget *parent);
#endif

    QTextCursor cursorForPosition(const QPointF &pos) const;
    QRectF cursorRect(const QTextCursor &cursor) const;
    QRectF cursorRect() const;
    QRectF selectionRect(const QTextCursor &cursor) const;
    QRectF selectionRect() const;

    virtual QString anchorAt(const QPointF &pos) const;
    QPointF anchorPosition(const QString &name) const;

    QString anchorAtCursor() const;

    QTextBlock blockWithMarkerAt(const QPointF &pos) const;

    bool overwriteMode() const;
    void setOverwriteMode(bool overwrite);

    int cursorWidth() const;
    void setCursorWidth(int width);

    bool acceptRichText() const;
    void setAcceptRichText(bool accept);

#if QT_CONFIG(textedit)
    void setExtraSelections(const QList<QTextEdit::ExtraSelection> &selections);
    QList<QTextEdit::ExtraSelection> extraSelections() const;
#endif

    void setTextWidth(qreal width);
    qreal textWidth() const;
    QSizeF size() const;

    void setOpenExternalLinks(bool open);
    bool openExternalLinks() const;

    void setIgnoreUnusedNavigationEvents(bool ignore);
    bool ignoreUnusedNavigationEvents() const;

    void moveCursor(QTextCursor::MoveOperation op, QTextCursor::MoveMode mode = QTextCursor::MoveAnchor);

    bool canPaste() const;

    void setCursorIsFocusIndicator(bool b);
    bool cursorIsFocusIndicator() const;

    void setDragEnabled(bool enabled);
    bool isDragEnabled() const;

    bool isWordSelectionEnabled() const;
    void setWordSelectionEnabled(bool enabled);

    bool isPreediting();

    void print(QPagedPaintDevice *printer) const;

    virtual int hitTest(const QPointF &point, Qt::HitTestAccuracy accuracy) const;
    virtual QRectF blockBoundingRect(const QTextBlock &block) const;
    QAbstractTextDocumentLayout::PaintContext getPaintContext(QWidget *widget) const;

public Q_SLOTS:
    void setPlainText(const QString &text);
#if QT_CONFIG(textmarkdownreader)
    void setMarkdown(const QString &text);
#endif
    void setHtml(const QString &text);

#ifndef QT_NO_CLIPBOARD
    void cut();
    void copy();
    void paste(QClipboard::Mode mode = QClipboard::Clipboard);
#endif

    void undo();
    void redo();

    void clear();
    void selectAll();

    void insertPlainText(const QString &text);
#ifndef QT_NO_TEXTHTMLPARSER
    void insertHtml(const QString &text);
#endif

    void append(const QString &text);
    void appendHtml(const QString &html);
    void appendPlainText(const QString &text);

    void adjustSize();

Q_SIGNALS:
    void textChanged();
    void undoAvailable(bool b);
    void redoAvailable(bool b);
    void currentCharFormatChanged(const QTextCharFormat &format);
    void copyAvailable(bool b);
    void selectionChanged();
    void cursorPositionChanged();

    // control signals
    void updateRequest(const QRectF &rect = QRectF());
    void documentSizeChanged(const QSizeF &);
    void blockCountChanged(int newBlockCount);
    void visibilityRequest(const QRectF &rect);
    void microFocusChanged();
    void linkActivated(const QString &link);
    void linkHovered(const QString &);
    void blockMarkerHovered(const QTextBlock &block);
    void modificationChanged(bool m);

public:
    // control properties
    QPalette palette() const;
    void setPalette(const QPalette &pal);

    virtual void processEvent(QEvent *e, const QTransform &transform, QWidget *contextWidget = nullptr);
    void processEvent(QEvent *e, const QPointF &coordinateOffset = QPointF(), QWidget *contextWidget = nullptr);

    // control methods
    void drawContents(QPainter *painter, const QRectF &rect = QRectF(), QWidget *widget = nullptr);

    void setFocus(bool focus, Qt::FocusReason = Qt::OtherFocusReason);

    virtual QVariant inputMethodQuery(Qt::InputMethodQuery property, QVariant argument) const;

    virtual QMimeData *createMimeDataFromSelection() const;
    virtual bool canInsertFromMimeData(const QMimeData *source) const;
    virtual void insertFromMimeData(const QMimeData *source);

    bool setFocusToAnchor(const QTextCursor &newCursor);
    bool setFocusToNextOrPreviousAnchor(bool next);
    bool findNextPrevAnchor(const QTextCursor& from, bool next, QTextCursor& newAnchor);

protected:
    virtual void timerEvent(QTimerEvent *e) override;

    virtual bool event(QEvent *e) override;

private:
    Q_DISABLE_COPY_MOVE(WidgetTextControl)

    std::unique_ptr<WidgetTextControlPrivate> d;
};

class WidgetTextControlPrivate : public QObject
{
public:
    WidgetTextControlPrivate(WidgetTextControl *q);

    bool cursorMoveKeyEvent(QKeyEvent *e);

    void updateCurrentCharFormat();

    void indent();
    void outdent();

    void gotoNextTableCell();
    void gotoPreviousTableCell();

    void createAutoBulletList();

    void init(Qt::TextFormat format = Qt::RichText, const QString &text = QString(),
              QTextDocument *document = nullptr);
    void setContent(Qt::TextFormat format = Qt::RichText, const QString &text = QString(),
                    QTextDocument *document = nullptr);
    void startDrag();

    void paste(const QMimeData *source);

    void setCursorPosition(const QPointF &pos);
    void setCursorPosition(int pos, QTextCursor::MoveMode mode = QTextCursor::MoveAnchor);

    void repaintCursor();
    inline void repaintSelection()
    { repaintOldAndNewSelection(QTextCursor()); }
    void repaintOldAndNewSelection(const QTextCursor &oldSelection);

    void selectionChanged(bool forceEmitSelectionChanged = false);

    void _q_updateCurrentCharFormatAndSelection();

#ifndef QT_NO_CLIPBOARD
    void setClipboardSelection();
#endif

    void _q_emitCursorPosChanged(const QTextCursor &someCursor);
    void _q_contentsChanged(int from, int charsRemoved, int charsAdded);

    void setCursorVisible(bool visible);
    void setBlinkingCursorEnabled(bool enable);
    void updateCursorBlinking();

    void extendWordwiseSelection(int suggestedNewPosition, qreal mouseXPosition);
    void extendBlockwiseSelection(int suggestedNewPosition);

    void _q_deleteSelected();

    void _q_setCursorAfterUndoRedo(int undoPosition, int charsAdded, int charsRemoved);

    QRectF cursorRectPlusUnicodeDirectionMarkers(const QTextCursor &cursor) const;
    QRectF rectForPosition(int position) const;
    QRectF selectionRect(const QTextCursor &cursor) const;
    inline QRectF selectionRect() const
    { return selectionRect(this->cursor); }

    QString anchorForCursor(const QTextCursor &anchor) const;

    void keyPressEvent(QKeyEvent *e);
    void mousePressEvent(QEvent *e, Qt::MouseButton button, const QPointF &pos,
                         Qt::KeyboardModifiers modifiers,
                         Qt::MouseButtons buttons,
                         const QPoint &globalPos);
    void mouseMoveEvent(QEvent *e, Qt::MouseButton button, const QPointF &pos,
                        Qt::KeyboardModifiers modifiers,
                        Qt::MouseButtons buttons,
                        const QPoint &globalPos);
    void mouseReleaseEvent(QEvent *e, Qt::MouseButton button, const QPointF &pos,
                           Qt::KeyboardModifiers modifiers,
                           Qt::MouseButtons buttons,
                           const QPoint &globalPos);
    void mouseDoubleClickEvent(QEvent *e, Qt::MouseButton button, const QPointF &pos,
                               Qt::KeyboardModifiers modifiers,
                               Qt::MouseButtons buttons,
                               const QPoint &globalPos);
    bool sendMouseEventToInputContext(QEvent *e,  QEvent::Type eventType, Qt::MouseButton button,
                                      const QPointF &pos,
                                      Qt::KeyboardModifiers modifiers,
                                      Qt::MouseButtons buttons,
                                      const QPoint &globalPos);
    void contextMenuEvent(const QPoint &screenPos, const QPointF &docPos, QWidget *contextWidget);
    void focusEvent(QFocusEvent *e);
#ifdef QT_KEYPAD_NAVIGATION
    void editFocusEvent(QEvent *e);
#endif
    bool dragEnterEvent(QEvent *e, const QMimeData *mimeData);
    void dragLeaveEvent();
    bool dragMoveEvent(QEvent *e, const QMimeData *mimeData, const QPointF &pos);
    bool dropEvent(const QMimeData *mimeData, const QPointF &pos, Qt::DropAction dropAction, QObject *source);

    void inputMethodEvent(QInputMethodEvent *);

    void activateLinkUnderCursor(QString href = QString());

#if QT_CONFIG(tooltip)
    void showToolTip(const QPoint &globalPos, const QPointF &pos, QWidget *contextWidget);
#endif

    bool isPreediting() const;
    void commitPreedit();

    void insertParagraphSeparator();
    void append(const QString &text, Qt::TextFormat format = Qt::AutoText);

    QTextDocument *doc;
    bool cursorOn;
    bool cursorVisible;
    QTextCursor cursor;
    bool cursorIsFocusIndicator;
    QTextCharFormat lastCharFormat;

    QTextCursor dndFeedbackCursor;

    Qt::TextInteractionFlags interactionFlags;

    QBasicTimer cursorBlinkTimer;
    QBasicTimer trippleClickTimer;
    QPointF trippleClickPoint;

    bool dragEnabled;

    bool mousePressed;

    bool mightStartDrag;
    QPoint mousePressPos;
    QPointer<QWidget> contextWidget;

    int lastSelectionPosition;
    int lastSelectionAnchor;

    bool ignoreAutomaticScrollbarAdjustement;

    QTextCursor selectedWordOnDoubleClick;
    QTextCursor selectedBlockOnTrippleClick;

    bool overwriteMode;
    bool acceptRichText;

    int preeditCursor;
    bool hideCursor; // used to hide the cursor in the preedit area

    QList<QAbstractTextDocumentLayout::Selection> extraSelections;

    QPalette palette;
    bool hasFocus;
#ifdef QT_KEYPAD_NAVIGATION
    bool hasEditFocus;
#endif
    bool isEnabled;

    QString highlightedAnchor; // Anchor below cursor
    QString anchorOnMousePress;
    QTextBlock blockWithMarkerUnderMouse;
    bool hadSelectionOnMousePress;

    bool ignoreUnusedNavigationEvents;
    bool openExternalLinks;

    bool wordSelectionEnabled;

    QString linkToCopy;
    void _q_copyLink();
    void _q_updateBlock(const QTextBlock &);
    void _q_documentLayoutChanged();

    WidgetTextControl *q = nullptr;
};

// could go into QTextCursor...
static QTextLine currentTextLine(const QTextCursor &cursor)
{
    const QTextBlock block = cursor.block();
    if (!block.isValid())
        return QTextLine();

    const QTextLayout *layout = block.layout();
    if (!layout)
        return QTextLine();

    const int relativePos = cursor.position() - block.position();
    return layout->lineForTextPosition(relativePos);
}

WidgetTextControlPrivate::WidgetTextControlPrivate(WidgetTextControl *q)
    : doc(nullptr), cursorOn(false), cursorVisible(false), cursorIsFocusIndicator(false),
#ifndef Q_OS_ANDROID
    interactionFlags(Qt::TextEditorInteraction),
#else
    interactionFlags(Qt::TextEditable | Qt::TextSelectableByKeyboard),
#endif
    dragEnabled(true),
#if QT_CONFIG(draganddrop)
    mousePressed(false), mightStartDrag(false),
#endif
    lastSelectionPosition(0), lastSelectionAnchor(0),
    ignoreAutomaticScrollbarAdjustement(false),
    overwriteMode(false),
    acceptRichText(true),
    preeditCursor(0), hideCursor(false),
    hasFocus(false),
#ifdef QT_KEYPAD_NAVIGATION
    hasEditFocus(false),
#endif
    isEnabled(true),
    hadSelectionOnMousePress(false),
    ignoreUnusedNavigationEvents(false),
    openExternalLinks(false),
    wordSelectionEnabled(false),
    q(q)
{}

bool WidgetTextControlPrivate::cursorMoveKeyEvent(QKeyEvent *e)
{
#ifdef QT_NO_SHORTCUT
    Q_UNUSED(e);
#endif

    if (cursor.isNull())
        return false;

    const QTextCursor oldSelection = cursor;
    const int oldCursorPos = cursor.position();

    QTextCursor::MoveMode mode = QTextCursor::MoveAnchor;
    QTextCursor::MoveOperation op = QTextCursor::NoMove;

    if (false) {
    }
#ifndef QT_NO_SHORTCUT
    if (e == QKeySequence::MoveToNextChar) {
        op = QTextCursor::Right;
    }
    else if (e == QKeySequence::MoveToPreviousChar) {
        op = QTextCursor::Left;
    }
    else if (e == QKeySequence::SelectNextChar) {
        op = QTextCursor::Right;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectPreviousChar) {
        op = QTextCursor::Left;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectNextWord) {
        op = QTextCursor::WordRight;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectPreviousWord) {
        op = QTextCursor::WordLeft;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectStartOfLine) {
        op = QTextCursor::StartOfLine;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectEndOfLine) {
        op = QTextCursor::EndOfLine;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectStartOfBlock) {
        op = QTextCursor::StartOfBlock;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectEndOfBlock) {
        op = QTextCursor::EndOfBlock;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectStartOfDocument) {
        op = QTextCursor::Start;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectEndOfDocument) {
        op = QTextCursor::End;
        mode = QTextCursor::KeepAnchor;
    }
    else if (e == QKeySequence::SelectPreviousLine) {
        op = QTextCursor::Up;
        mode = QTextCursor::KeepAnchor;
        {
            QTextBlock block = cursor.block();
            QTextLine line = currentTextLine(cursor);
            if (!block.previous().isValid()
                && line.isValid()
                && line.lineNumber() == 0)
                op = QTextCursor::Start;
        }
    }
    else if (e == QKeySequence::SelectNextLine) {
        op = QTextCursor::Down;
        mode = QTextCursor::KeepAnchor;
        {
            QTextBlock block = cursor.block();
            QTextLine line = currentTextLine(cursor);
            if (!block.next().isValid()
                && line.isValid()
                && line.lineNumber() == block.layout()->lineCount() - 1)
                op = QTextCursor::End;
        }
    }
    else if (e == QKeySequence::MoveToNextWord) {
        op = QTextCursor::WordRight;
    }
    else if (e == QKeySequence::MoveToPreviousWord) {
        op = QTextCursor::WordLeft;
    }
    else if (e == QKeySequence::MoveToEndOfBlock) {
        op = QTextCursor::EndOfBlock;
    }
    else if (e == QKeySequence::MoveToStartOfBlock) {
        op = QTextCursor::StartOfBlock;
    }
    else if (e == QKeySequence::MoveToNextLine) {
        op = QTextCursor::Down;
    }
    else if (e == QKeySequence::MoveToPreviousLine) {
        op = QTextCursor::Up;
    }
    else if (e == QKeySequence::MoveToStartOfLine) {
        op = QTextCursor::StartOfLine;
    }
    else if (e == QKeySequence::MoveToEndOfLine) {
        op = QTextCursor::EndOfLine;
    }
    else if (e == QKeySequence::MoveToStartOfDocument) {
        op = QTextCursor::Start;
    }
    else if (e == QKeySequence::MoveToEndOfDocument) {
        op = QTextCursor::End;
    }
#endif // QT_NO_SHORTCUT
    else {
        return false;
    }

    // Except for pageup and pagedown, OS X has very different behavior, we don't do it all, but
    // here's the breakdown:
    // Shift still works as an anchor, but only one of the other keys can be down Ctrl (Command),
    // Alt (Option), or Meta (Control).
    // Command/Control + Left/Right -- Move to left or right of the line
    //                 + Up/Down -- Move to top bottom of the file. (Control doesn't move the cursor)
    // Option + Left/Right -- Move one word Left/right.
    //        + Up/Down  -- Begin/End of Paragraph.
    // Home/End Top/Bottom of file. (usually don't move the cursor, but will select)

    bool visualNavigation = cursor.visualNavigation();
    cursor.setVisualNavigation(true);
    const bool moved = cursor.movePosition(op, mode);
    cursor.setVisualNavigation(visualNavigation);
    q->ensureCursorVisible();

    bool ignoreNavigationEvents = ignoreUnusedNavigationEvents;
    bool isNavigationEvent = e->key() == Qt::Key_Up || e->key() == Qt::Key_Down;

#ifdef QT_KEYPAD_NAVIGATION
    ignoreNavigationEvents = ignoreNavigationEvents || QApplicationPrivate::keypadNavigationEnabled();
    isNavigationEvent = isNavigationEvent ||
                        (QApplication::navigationMode() == Qt::NavigationModeKeypadDirectional
                         && (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right));
#else
    isNavigationEvent = isNavigationEvent || e->key() == Qt::Key_Left || e->key() == Qt::Key_Right;
#endif

    if (moved) {
        if (cursor.position() != oldCursorPos)
            emit q->cursorPositionChanged();
        emit q->microFocusChanged();
    } else if (ignoreNavigationEvents && isNavigationEvent && oldSelection.anchor() == cursor.anchor()) {
        return false;
    }

    selectionChanged(/*forceEmitSelectionChanged =*/(mode == QTextCursor::KeepAnchor));

    repaintOldAndNewSelection(oldSelection);

    return true;
}

void WidgetTextControlPrivate::updateCurrentCharFormat()
{

    QTextCharFormat fmt = cursor.charFormat();
    if (fmt == lastCharFormat)
        return;
    lastCharFormat = fmt;

    emit q->currentCharFormatChanged(fmt);
    emit q->microFocusChanged();
}

void WidgetTextControlPrivate::indent()
{
    QTextBlockFormat blockFmt = cursor.blockFormat();

    QTextList *list = cursor.currentList();
    if (!list) {
        QTextBlockFormat modifier;
        modifier.setIndent(blockFmt.indent() + 1);
        cursor.mergeBlockFormat(modifier);
    } else {
        QTextListFormat format = list->format();
        format.setIndent(format.indent() + 1);

        if (list->itemNumber(cursor.block()) == 1)
            list->setFormat(format);
        else
            cursor.createList(format);
    }
}

void WidgetTextControlPrivate::outdent()
{
    QTextBlockFormat blockFmt = cursor.blockFormat();

    QTextList *list = cursor.currentList();

    if (!list) {
        QTextBlockFormat modifier;
        modifier.setIndent(blockFmt.indent() - 1);
        cursor.mergeBlockFormat(modifier);
    } else {
        QTextListFormat listFmt = list->format();
        listFmt.setIndent(listFmt.indent() - 1);
        list->setFormat(listFmt);
    }
}

void WidgetTextControlPrivate::gotoNextTableCell()
{
    QTextTable *table = cursor.currentTable();
    QTextTableCell cell = table->cellAt(cursor);

    int newColumn = cell.column() + cell.columnSpan();
    int newRow = cell.row();

    if (newColumn >= table->columns()) {
        newColumn = 0;
        ++newRow;
        if (newRow >= table->rows())
            table->insertRows(table->rows(), 1);
    }

    cell = table->cellAt(newRow, newColumn);
    cursor = cell.firstCursorPosition();
}

void WidgetTextControlPrivate::gotoPreviousTableCell()
{
    QTextTable *table = cursor.currentTable();
    QTextTableCell cell = table->cellAt(cursor);

    int newColumn = cell.column() - 1;
    int newRow = cell.row();

    if (newColumn < 0) {
        newColumn = table->columns() - 1;
        --newRow;
        if (newRow < 0)
            return;
    }

    cell = table->cellAt(newRow, newColumn);
    cursor = cell.firstCursorPosition();
}

void WidgetTextControlPrivate::createAutoBulletList()
{
    cursor.beginEditBlock();

    QTextBlockFormat blockFmt = cursor.blockFormat();

    QTextListFormat listFmt;
    listFmt.setStyle(QTextListFormat::ListDisc);
    listFmt.setIndent(blockFmt.indent() + 1);

    blockFmt.setIndent(0);
    cursor.setBlockFormat(blockFmt);

    cursor.createList(listFmt);

    cursor.endEditBlock();
}

void WidgetTextControlPrivate::init(Qt::TextFormat format, const QString &text, QTextDocument *document)
{
    setContent(format, text, document);

    doc->setUndoRedoEnabled(interactionFlags & Qt::TextEditable);
    q->setCursorWidth(-1);
}

void WidgetTextControlPrivate::setContent(Qt::TextFormat format, const QString &text, QTextDocument *document)
{

    // for use when called from setPlainText. we may want to re-use the currently
    // set char format then.
    const QTextCharFormat charFormatForInsertion = cursor.charFormat();

    bool clearDocument = true;
    if (!doc) {
        if (document) {
            doc = document;
        } else {
            palette = QApplication::palette("WidgetTextControl");
            doc = new QTextDocument(q);
        }
        clearDocument = false;
        _q_documentLayoutChanged();
        cursor = QTextCursor(doc);

        // ####        doc->documentLayout()->setPaintDevice(viewport);

        QObject::connect(doc, &QTextDocument::contentsChanged, this,
                                &WidgetTextControlPrivate::_q_updateCurrentCharFormatAndSelection);
        QObject::connect(doc, &QTextDocument::cursorPositionChanged, this,
                                &WidgetTextControlPrivate::_q_emitCursorPosChanged);
        QObject::connect(doc, &QTextDocument::documentLayoutChanged, this,
                                &WidgetTextControlPrivate::_q_documentLayoutChanged);

        // convenience signal forwards
        QObject::connect(doc, &QTextDocument::undoAvailable, q, &WidgetTextControl::undoAvailable);
        QObject::connect(doc, &QTextDocument::redoAvailable, q, &WidgetTextControl::redoAvailable);
        QObject::connect(doc, &QTextDocument::modificationChanged, q,
                         &WidgetTextControl::modificationChanged);
        QObject::connect(doc, &QTextDocument::blockCountChanged, q,
                         &WidgetTextControl::blockCountChanged);
    }

    bool previousUndoRedoState = doc->isUndoRedoEnabled();
    if (!document)
        doc->setUndoRedoEnabled(false);

    //Saving the index save some time.
    static int contentsChangedIndex = QMetaMethod::fromSignal(&QTextDocument::contentsChanged).methodIndex();
    static int textChangedIndex = QMetaMethod::fromSignal(&WidgetTextControl::textChanged).methodIndex();
    // avoid multiple textChanged() signals being emitted
    QMetaObject::disconnect(doc, contentsChangedIndex, q, textChangedIndex);

    if (!text.isEmpty()) {
        // clear 'our' cursor for insertion to prevent
        // the emission of the cursorPositionChanged() signal.
        // instead we emit it only once at the end instead of
        // at the end of the document after loading and when
        // positioning the cursor again to the start of the
        // document.
        cursor = QTextCursor();
        if (format == Qt::PlainText) {
            QTextCursor formatCursor(doc);
            // put the setPlainText and the setCharFormat into one edit block,
            // so that the syntax highlight triggers only /once/ for the entire
            // document, not twice.
            formatCursor.beginEditBlock();
            doc->setPlainText(text);
            doc->setUndoRedoEnabled(false);
            formatCursor.select(QTextCursor::Document);
            formatCursor.setCharFormat(charFormatForInsertion);
            formatCursor.endEditBlock();
#if QT_CONFIG(textmarkdownreader)
        } else if (format == Qt::MarkdownText) {
            doc->setMarkdown(text);
            doc->setUndoRedoEnabled(false);
#endif
        } else {
#ifndef QT_NO_TEXTHTMLPARSER
            doc->setHtml(text);
#else
            doc->setPlainText(text);
#endif
            doc->setUndoRedoEnabled(false);
        }
        cursor = QTextCursor(doc);
    } else if (clearDocument) {
        doc->clear();
    }
    cursor.setCharFormat(charFormatForInsertion);

    QMetaObject::connect(doc, contentsChangedIndex, q, textChangedIndex);
    emit q->textChanged();
    if (!document)
        doc->setUndoRedoEnabled(previousUndoRedoState);
    _q_updateCurrentCharFormatAndSelection();
    if (!document)
        doc->setModified(false);

    q->ensureCursorVisible();
    emit q->cursorPositionChanged();

    QObject::connect(doc, &QTextDocument::contentsChange, this,
                            &WidgetTextControlPrivate::_q_contentsChanged, Qt::UniqueConnection);
}

void WidgetTextControlPrivate::startDrag()
{
#if QT_CONFIG(draganddrop)
    mousePressed = false;
    if (!contextWidget)
        return;
    QMimeData *data = q->createMimeDataFromSelection();

    QDrag *drag = new QDrag(contextWidget);
    drag->setMimeData(data);

    Qt::DropActions actions = Qt::CopyAction;
    Qt::DropAction action;
    if (interactionFlags & Qt::TextEditable) {
        actions |= Qt::MoveAction;
        action = drag->exec(actions, Qt::MoveAction);
    } else {
        action = drag->exec(actions, Qt::CopyAction);
    }

    if (action == Qt::MoveAction && drag->target() != contextWidget)
        cursor.removeSelectedText();
#endif
}

void WidgetTextControlPrivate::setCursorPosition(const QPointF &pos)
{
    const int cursorPos = q->hitTest(pos, Qt::FuzzyHit);
    if (cursorPos == -1)
        return;
    cursor.setPosition(cursorPos);
}

void WidgetTextControlPrivate::setCursorPosition(int pos, QTextCursor::MoveMode mode)
{
    cursor.setPosition(pos, mode);

    if (mode != QTextCursor::KeepAnchor) {
        selectedWordOnDoubleClick = QTextCursor();
        selectedBlockOnTrippleClick = QTextCursor();
    }
}

void WidgetTextControlPrivate::repaintCursor()
{
    emit q->updateRequest(cursorRectPlusUnicodeDirectionMarkers(cursor));
}

void WidgetTextControlPrivate::repaintOldAndNewSelection(const QTextCursor &oldSelection)
{
    if (cursor.hasSelection()
        && oldSelection.hasSelection()
        && cursor.currentFrame() == oldSelection.currentFrame()
        && !cursor.hasComplexSelection()
        && !oldSelection.hasComplexSelection()
        && cursor.anchor() == oldSelection.anchor()
        ) {
        QTextCursor differenceSelection(doc);
        differenceSelection.setPosition(oldSelection.position());
        differenceSelection.setPosition(cursor.position(), QTextCursor::KeepAnchor);
        emit q->updateRequest(q->selectionRect(differenceSelection));
    } else {
        if (!oldSelection.isNull())
            emit q->updateRequest(q->selectionRect(oldSelection) | cursorRectPlusUnicodeDirectionMarkers(oldSelection));
        emit q->updateRequest(q->selectionRect() | cursorRectPlusUnicodeDirectionMarkers(cursor));
    }
}

void WidgetTextControlPrivate::selectionChanged(bool forceEmitSelectionChanged /*=false*/)
{
    if (forceEmitSelectionChanged) {
        emit q->selectionChanged();
#if QT_CONFIG(accessibility)
        if (q->parent() && q->parent()->isWidgetType()) {
            QAccessibleTextSelectionEvent ev(q->parent(), cursor.anchor(), cursor.position());
            QAccessible::updateAccessibility(&ev);
        }
#endif
    }

    if (cursor.position() == lastSelectionPosition
        && cursor.anchor() == lastSelectionAnchor)
        return;

    bool selectionStateChange = (cursor.hasSelection()
                                 != (lastSelectionPosition != lastSelectionAnchor));
    if (selectionStateChange)
        emit q->copyAvailable(cursor.hasSelection());

    if (!forceEmitSelectionChanged
        && (selectionStateChange
            || (cursor.hasSelection()
                && (cursor.position() != lastSelectionPosition
                    || cursor.anchor() != lastSelectionAnchor)))) {
        emit q->selectionChanged();
#if QT_CONFIG(accessibility)
        if (q->parent() && q->parent()->isWidgetType()) {
            QAccessibleTextSelectionEvent ev(q->parent(), cursor.anchor(), cursor.position());
            QAccessible::updateAccessibility(&ev);
        }
#endif
    }
    emit q->microFocusChanged();
    lastSelectionPosition = cursor.position();
    lastSelectionAnchor = cursor.anchor();
}

void WidgetTextControlPrivate::_q_updateCurrentCharFormatAndSelection()
{
    updateCurrentCharFormat();
    selectionChanged();
}

#ifndef QT_NO_CLIPBOARD
void WidgetTextControlPrivate::setClipboardSelection()
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!cursor.hasSelection() || !clipboard->supportsSelection())
        return;
    QMimeData *data = q->createMimeDataFromSelection();
    clipboard->setMimeData(data, QClipboard::Selection);
}
#endif

void WidgetTextControlPrivate::_q_emitCursorPosChanged(const QTextCursor &someCursor)
{
    if (someCursor.isCopyOf(cursor)) {
        emit q->cursorPositionChanged();
        emit q->microFocusChanged();
    }
}

void WidgetTextControlPrivate::_q_contentsChanged(int from, int charsRemoved, int charsAdded)
{
#if QT_CONFIG(accessibility)

    if (QAccessible::isActive() && q->parent() && q->parent()->isWidgetType()) {
        QTextCursor tmp(doc);
        tmp.setPosition(from);
        // when setting a new text document the length is off
        // QTBUG-32583 - characterCount is off by 1 requires the -1
        tmp.setPosition(qMin(doc->characterCount() - 1, from + charsAdded), QTextCursor::KeepAnchor);
        QString newText = tmp.selectedText();

        // always report the right number of removed chars, but in lack of the real string use spaces
        QString oldText = QString(charsRemoved, u' ');

        QAccessibleEvent *ev = nullptr;
        if (charsRemoved == 0) {
            ev = new QAccessibleTextInsertEvent(q->parent(), from, newText);
        } else if (charsAdded == 0) {
            ev = new QAccessibleTextRemoveEvent(q->parent(), from, oldText);
        } else {
            ev = new QAccessibleTextUpdateEvent(q->parent(), from, oldText, newText);
        }
        QAccessible::updateAccessibility(ev);
        delete ev;
    }
#else
    Q_UNUSED(from);
    Q_UNUSED(charsRemoved);
    Q_UNUSED(charsAdded);
#endif
}

void WidgetTextControlPrivate::_q_documentLayoutChanged()
{
    QAbstractTextDocumentLayout *layout = doc->documentLayout();
    QObject::connect(layout, &QAbstractTextDocumentLayout::update, q,
                     &WidgetTextControl::updateRequest);
    QObject::connect(layout, &QAbstractTextDocumentLayout::updateBlock, this,
                            &WidgetTextControlPrivate::_q_updateBlock);
    QObject::connect(layout, &QAbstractTextDocumentLayout::documentSizeChanged, q,
                     &WidgetTextControl::documentSizeChanged);
}

void WidgetTextControlPrivate::setCursorVisible(bool visible)
{
    if (cursorVisible == visible)
        return;

    cursorVisible = visible;
    updateCursorBlinking();

    if (cursorVisible)
        connect(QGuiApplication::styleHints(), &QStyleHints::cursorFlashTimeChanged, this, &WidgetTextControlPrivate::updateCursorBlinking);
    else
        disconnect(QGuiApplication::styleHints(), &QStyleHints::cursorFlashTimeChanged, this, &WidgetTextControlPrivate::updateCursorBlinking);
}

void WidgetTextControlPrivate::updateCursorBlinking()
{
    cursorBlinkTimer.stop();
    if (cursorVisible) {
        int flashTime = QGuiApplication::styleHints()->cursorFlashTime();
        if (flashTime >= 2)
            cursorBlinkTimer.start(flashTime / 2, q);
    }

    cursorOn = cursorVisible;
    repaintCursor();
}

void WidgetTextControlPrivate::extendWordwiseSelection(int suggestedNewPosition, qreal mouseXPosition)
{

    // if inside the initial selected word keep that
    if (suggestedNewPosition >= selectedWordOnDoubleClick.selectionStart()
        && suggestedNewPosition <= selectedWordOnDoubleClick.selectionEnd()) {
        q->setTextCursor(selectedWordOnDoubleClick);
        return;
    }

    QTextCursor curs = selectedWordOnDoubleClick;
    curs.setPosition(suggestedNewPosition, QTextCursor::KeepAnchor);

    if (!curs.movePosition(QTextCursor::StartOfWord))
        return;
    const int wordStartPos = curs.position();

    const int blockPos = curs.block().position();
    const QPointF blockCoordinates = q->blockBoundingRect(curs.block()).topLeft();

    QTextLine line = currentTextLine(curs);
    if (!line.isValid())
        return;

    const qreal wordStartX = line.cursorToX(curs.position() - blockPos) + blockCoordinates.x();

    if (!curs.movePosition(QTextCursor::EndOfWord))
        return;
    const int wordEndPos = curs.position();

    const QTextLine otherLine = currentTextLine(curs);
    if (otherLine.textStart() != line.textStart()
        || wordEndPos == wordStartPos)
        return;

    const qreal wordEndX = line.cursorToX(curs.position() - blockPos) + blockCoordinates.x();

    if (!wordSelectionEnabled && (mouseXPosition < wordStartX || mouseXPosition > wordEndX))
        return;

    if (wordSelectionEnabled) {
        if (suggestedNewPosition < selectedWordOnDoubleClick.position()) {
            cursor.setPosition(selectedWordOnDoubleClick.selectionEnd());
            setCursorPosition(wordStartPos, QTextCursor::KeepAnchor);
        } else {
            cursor.setPosition(selectedWordOnDoubleClick.selectionStart());
            setCursorPosition(wordEndPos, QTextCursor::KeepAnchor);
        }
    } else {
        // keep the already selected word even when moving to the left
        // (#39164)
        if (suggestedNewPosition < selectedWordOnDoubleClick.position())
            cursor.setPosition(selectedWordOnDoubleClick.selectionEnd());
        else
            cursor.setPosition(selectedWordOnDoubleClick.selectionStart());

        const qreal differenceToStart = mouseXPosition - wordStartX;
        const qreal differenceToEnd = wordEndX - mouseXPosition;

        if (differenceToStart < differenceToEnd)
            setCursorPosition(wordStartPos, QTextCursor::KeepAnchor);
        else
            setCursorPosition(wordEndPos, QTextCursor::KeepAnchor);
    }

    if (interactionFlags & Qt::TextSelectableByMouse) {
#ifndef QT_NO_CLIPBOARD
        setClipboardSelection();
#endif
        selectionChanged(true);
    }
}

void WidgetTextControlPrivate::extendBlockwiseSelection(int suggestedNewPosition)
{

    // if inside the initial selected line keep that
    if (suggestedNewPosition >= selectedBlockOnTrippleClick.selectionStart()
        && suggestedNewPosition <= selectedBlockOnTrippleClick.selectionEnd()) {
        q->setTextCursor(selectedBlockOnTrippleClick);
        return;
    }

    if (suggestedNewPosition < selectedBlockOnTrippleClick.position()) {
        cursor.setPosition(selectedBlockOnTrippleClick.selectionEnd());
        cursor.setPosition(suggestedNewPosition, QTextCursor::KeepAnchor);
        cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    } else {
        cursor.setPosition(selectedBlockOnTrippleClick.selectionStart());
        cursor.setPosition(suggestedNewPosition, QTextCursor::KeepAnchor);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    }

    if (interactionFlags & Qt::TextSelectableByMouse) {
#ifndef QT_NO_CLIPBOARD
        setClipboardSelection();
#endif
        selectionChanged(true);
    }
}

void WidgetTextControlPrivate::_q_deleteSelected()
{
    if (!(interactionFlags & Qt::TextEditable) || !cursor.hasSelection())
        return;
    cursor.removeSelectedText();
}

void WidgetTextControl::undo()
{
    d->repaintSelection();
    const int oldCursorPos = d->cursor.position();
    d->doc->undo(&d->cursor);
    if (d->cursor.position() != oldCursorPos)
        emit cursorPositionChanged();
    emit microFocusChanged();
    ensureCursorVisible();
}

void WidgetTextControl::redo()
{
    d->repaintSelection();
    const int oldCursorPos = d->cursor.position();
    d->doc->redo(&d->cursor);
    if (d->cursor.position() != oldCursorPos)
        emit cursorPositionChanged();
    emit microFocusChanged();
    ensureCursorVisible();
}

WidgetTextControl::WidgetTextControl(QObject *parent)
    : InputControl(InputControl::TextEdit, parent)
    , d(new WidgetTextControlPrivate(this))
{
    d->init();
}

WidgetTextControl::WidgetTextControl(const QString &text, QObject *parent)
    : InputControl(InputControl::TextEdit, parent)
    , d(new WidgetTextControlPrivate(this))
{
    d->init(Qt::RichText, text);
}

WidgetTextControl::WidgetTextControl(QTextDocument *doc, QObject *parent)
    : InputControl(InputControl::TextEdit, parent)
    , d(new WidgetTextControlPrivate(this))
{
    d->init(Qt::RichText, QString(), doc);
}

WidgetTextControl::~WidgetTextControl()
{
}

void WidgetTextControl::setDocument(QTextDocument *document)
{
    if (d->doc == document)
        return;

    d->doc->disconnect(this);
    d->doc->documentLayout()->disconnect(this);
    d->doc->documentLayout()->setPaintDevice(nullptr);

    if (d->doc->parent() == this)
        delete d->doc;

    d->doc = nullptr;
    d->setContent(Qt::RichText, QString(), document);
}

QTextDocument *WidgetTextControl::document() const
{
    return d->doc;
}

void WidgetTextControl::setTextCursor(const QTextCursor &cursor, bool selectionClipboard)
{
    d->cursorIsFocusIndicator = false;
    const bool posChanged = cursor.position() != d->cursor.position();
    const QTextCursor oldSelection = d->cursor;
    d->cursor = cursor;
    d->cursorOn = d->hasFocus
                  && (d->interactionFlags & (Qt::TextSelectableByKeyboard | Qt::TextEditable));
    d->_q_updateCurrentCharFormatAndSelection();
    ensureCursorVisible();
    d->repaintOldAndNewSelection(oldSelection);
    if (posChanged)
        emit cursorPositionChanged();

#ifndef QT_NO_CLIPBOARD
    if (selectionClipboard)
        d->setClipboardSelection();
#else
    Q_UNUSED(selectionClipboard);
#endif
}

QTextCursor WidgetTextControl::textCursor() const
{
    return d->cursor;
}

#ifndef QT_NO_CLIPBOARD

void WidgetTextControl::cut()
{
    if (!(d->interactionFlags & Qt::TextEditable) || !d->cursor.hasSelection())
        return;
    copy();
    d->cursor.removeSelectedText();
}

void WidgetTextControl::copy()
{
    if (!d->cursor.hasSelection())
        return;
    QMimeData *data = createMimeDataFromSelection();
    QGuiApplication::clipboard()->setMimeData(data);
}

void WidgetTextControl::paste(QClipboard::Mode mode)
{
    const QMimeData *md = QGuiApplication::clipboard()->mimeData(mode);
    if (md)
        insertFromMimeData(md);
}
#endif

void WidgetTextControl::clear()
{
    // clears and sets empty content
    d->extraSelections.clear();
    d->setContent();
}


void WidgetTextControl::selectAll()
{
    const int selectionLength = qAbs(d->cursor.position() - d->cursor.anchor());
    const int oldCursorPos = d->cursor.position();
    d->cursor.select(QTextCursor::Document);
    d->selectionChanged(selectionLength != qAbs(d->cursor.position() - d->cursor.anchor()));
    d->cursorIsFocusIndicator = false;
    if (d->cursor.position() != oldCursorPos)
        emit cursorPositionChanged();
    emit updateRequest();
}

void WidgetTextControl::processEvent(QEvent *e, const QPointF &coordinateOffset, QWidget *contextWidget)
{
    QTransform t;
    t.translate(coordinateOffset.x(), coordinateOffset.y());
    processEvent(e, t, contextWidget);
}

void WidgetTextControl::processEvent(QEvent *e, const QTransform &transform, QWidget *contextWidget)
{
    if (d->interactionFlags == Qt::NoTextInteraction) {
        e->ignore();
        return;
    }

    d->contextWidget = contextWidget;

    if (!d->contextWidget) {
        switch (e->type()) {
#if QT_CONFIG(graphicsview)
        case QEvent::GraphicsSceneMouseMove:
        case QEvent::GraphicsSceneMousePress:
        case QEvent::GraphicsSceneMouseRelease:
        case QEvent::GraphicsSceneMouseDoubleClick:
        case QEvent::GraphicsSceneContextMenu:
        case QEvent::GraphicsSceneHoverEnter:
        case QEvent::GraphicsSceneHoverMove:
        case QEvent::GraphicsSceneHoverLeave:
        case QEvent::GraphicsSceneHelp:
        case QEvent::GraphicsSceneDragEnter:
        case QEvent::GraphicsSceneDragMove:
        case QEvent::GraphicsSceneDragLeave:
        case QEvent::GraphicsSceneDrop: {
            QGraphicsSceneEvent *ev = static_cast<QGraphicsSceneEvent *>(e);
            d->contextWidget = ev->widget();
            break;
        }
#endif // QT_CONFIG(graphicsview)
        default: break;
        };
    }

    switch (e->type()) {
    case QEvent::KeyPress:
        d->keyPressEvent(static_cast<QKeyEvent *>(e));
        break;
    case QEvent::MouseButtonPress: {
        QMouseEvent *ev = static_cast<QMouseEvent *>(e);
        d->mousePressEvent(ev, ev->button(), transform.map(ev->position().toPoint()), ev->modifiers(),
                           ev->buttons(), ev->globalPosition().toPoint());
        break; }
    case QEvent::MouseMove: {
        QMouseEvent *ev = static_cast<QMouseEvent *>(e);
        d->mouseMoveEvent(ev, ev->button(), transform.map(ev->position().toPoint()), ev->modifiers(),
                          ev->buttons(), ev->globalPosition().toPoint());
        break; }
    case QEvent::MouseButtonRelease: {
        QMouseEvent *ev = static_cast<QMouseEvent *>(e);
        d->mouseReleaseEvent(ev, ev->button(), transform.map(ev->position().toPoint()), ev->modifiers(),
                             ev->buttons(), ev->globalPosition().toPoint());
        break; }
    case QEvent::MouseButtonDblClick: {
        QMouseEvent *ev = static_cast<QMouseEvent *>(e);
        d->mouseDoubleClickEvent(ev, ev->button(), transform.map(ev->position().toPoint()), ev->modifiers(),
                                 ev->buttons(), ev->globalPosition().toPoint());
        break; }
    case QEvent::InputMethod:
        d->inputMethodEvent(static_cast<QInputMethodEvent *>(e));
        break;
#ifndef QT_NO_CONTEXTMENU
    case QEvent::ContextMenu: {
        QContextMenuEvent *ev = static_cast<QContextMenuEvent *>(e);
        d->contextMenuEvent(ev->globalPos(), transform.map(ev->pos()), contextWidget);
        break; }
#endif // QT_NO_CONTEXTMENU
    case QEvent::FocusIn:
    case QEvent::FocusOut:
        d->focusEvent(static_cast<QFocusEvent *>(e));
        break;

    case QEvent::EnabledChange:
        d->isEnabled = e->isAccepted();
        break;

#if QT_CONFIG(tooltip)
    case QEvent::ToolTip: {
        QHelpEvent *ev = static_cast<QHelpEvent *>(e);
        d->showToolTip(ev->globalPos(), transform.map(ev->pos()), contextWidget);
        break;
    }
#endif // QT_CONFIG(tooltip)

#if QT_CONFIG(draganddrop)
    case QEvent::DragEnter: {
        QDragEnterEvent *ev = static_cast<QDragEnterEvent *>(e);
        if (d->dragEnterEvent(e, ev->mimeData()))
            ev->acceptProposedAction();
        break;
    }
    case QEvent::DragLeave:
        d->dragLeaveEvent();
        break;
    case QEvent::DragMove: {
        QDragMoveEvent *ev = static_cast<QDragMoveEvent *>(e);
        if (d->dragMoveEvent(e, ev->mimeData(), transform.map(ev->position().toPoint())))
            ev->acceptProposedAction();
        break;
    }
    case QEvent::Drop: {
        QDropEvent *ev = static_cast<QDropEvent *>(e);
        if (d->dropEvent(ev->mimeData(), transform.map(ev->position().toPoint()), ev->dropAction(), ev->source()))
            ev->acceptProposedAction();
        break;
    }
#endif

#if QT_CONFIG(graphicsview)
    case QEvent::GraphicsSceneMousePress: {
        QGraphicsSceneMouseEvent *ev = static_cast<QGraphicsSceneMouseEvent *>(e);
        d->mousePressEvent(ev, ev->button(), transform.map(ev->pos()), ev->modifiers(), ev->buttons(),
                           ev->screenPos());
        break; }
    case QEvent::GraphicsSceneMouseMove: {
        QGraphicsSceneMouseEvent *ev = static_cast<QGraphicsSceneMouseEvent *>(e);
        d->mouseMoveEvent(ev, ev->button(), transform.map(ev->pos()), ev->modifiers(), ev->buttons(),
                          ev->screenPos());
        break; }
    case QEvent::GraphicsSceneMouseRelease: {
        QGraphicsSceneMouseEvent *ev = static_cast<QGraphicsSceneMouseEvent *>(e);
        d->mouseReleaseEvent(ev, ev->button(), transform.map(ev->pos()), ev->modifiers(), ev->buttons(),
                             ev->screenPos());
        break; }
    case QEvent::GraphicsSceneMouseDoubleClick: {
        QGraphicsSceneMouseEvent *ev = static_cast<QGraphicsSceneMouseEvent *>(e);
        d->mouseDoubleClickEvent(ev, ev->button(), transform.map(ev->pos()), ev->modifiers(), ev->buttons(),
                                 ev->screenPos());
        break; }
    case QEvent::GraphicsSceneContextMenu: {
        QGraphicsSceneContextMenuEvent *ev = static_cast<QGraphicsSceneContextMenuEvent *>(e);
        d->contextMenuEvent(ev->screenPos(), transform.map(ev->pos()), contextWidget);
        break; }

    case QEvent::GraphicsSceneHoverMove: {
        QGraphicsSceneHoverEvent *ev = static_cast<QGraphicsSceneHoverEvent *>(e);
        d->mouseMoveEvent(ev, Qt::NoButton, transform.map(ev->pos()), ev->modifiers(),Qt::NoButton,
                          ev->screenPos());
        break; }

    case QEvent::GraphicsSceneDragEnter: {
        QGraphicsSceneDragDropEvent *ev = static_cast<QGraphicsSceneDragDropEvent *>(e);
        if (d->dragEnterEvent(e, ev->mimeData()))
            ev->acceptProposedAction();
        break; }
    case QEvent::GraphicsSceneDragLeave:
        d->dragLeaveEvent();
        break;
    case QEvent::GraphicsSceneDragMove: {
        QGraphicsSceneDragDropEvent *ev = static_cast<QGraphicsSceneDragDropEvent *>(e);
        if (d->dragMoveEvent(e, ev->mimeData(), transform.map(ev->pos())))
            ev->acceptProposedAction();
        break; }
    case QEvent::GraphicsSceneDrop: {
        QGraphicsSceneDragDropEvent *ev = static_cast<QGraphicsSceneDragDropEvent *>(e);
        if (d->dropEvent(ev->mimeData(), transform.map(ev->pos()), ev->dropAction(), ev->source()))
            ev->accept();
        break; }
#endif // QT_CONFIG(graphicsview)
#ifdef QT_KEYPAD_NAVIGATION
    case QEvent::EnterEditFocus:
    case QEvent::LeaveEditFocus:
        if (QApplicationPrivate::keypadNavigationEnabled())
            d->editFocusEvent(e);
        break;
#endif
    case QEvent::ShortcutOverride:
        if (d->interactionFlags & Qt::TextEditable) {
            QKeyEvent* ke = static_cast<QKeyEvent *>(e);
            if (isCommonTextEditShortcut(ke))
                ke->accept();
        }
        break;
    default:
        break;
    }
}

bool WidgetTextControl::event(QEvent *e)
{
    return QObject::event(e);
}

void WidgetTextControl::timerEvent(QTimerEvent *e)
{
    if (e->timerId() == d->cursorBlinkTimer.timerId()) {
        d->cursorOn = !d->cursorOn;

        if (d->cursor.hasSelection())
            d->cursorOn &= (QApplication::style()->styleHint(QStyle::SH_BlinkCursorWhenTextSelected)
                            != 0);

        d->repaintCursor();
    } else if (e->timerId() == d->trippleClickTimer.timerId()) {
        d->trippleClickTimer.stop();
    }
}

void WidgetTextControl::setPlainText(const QString &text)
{
    d->setContent(Qt::PlainText, text);
}

#if QT_CONFIG(textmarkdownreader)
void WidgetTextControl::setMarkdown(const QString &text)
{
    d->setContent(Qt::MarkdownText, text);
}
#endif

void WidgetTextControl::setHtml(const QString &text)
{
    d->setContent(Qt::RichText, text);
}

void WidgetTextControlPrivate::keyPressEvent(QKeyEvent *e)
{
#ifndef QT_NO_SHORTCUT
    if (e == QKeySequence::SelectAll) {
        e->accept();
        q->selectAll();
#ifndef QT_NO_CLIPBOARD
        setClipboardSelection();
#endif
        return;
    }
#ifndef QT_NO_CLIPBOARD
    else if (e == QKeySequence::Copy) {
        e->accept();
        q->copy();
        return;
    }
#endif
#endif // QT_NO_SHORTCUT

    if (interactionFlags & Qt::TextSelectableByKeyboard
        && cursorMoveKeyEvent(e))
        goto accept;

    if (interactionFlags & Qt::LinksAccessibleByKeyboard) {
        if ((e->key() == Qt::Key_Return
             || e->key() == Qt::Key_Enter
#ifdef QT_KEYPAD_NAVIGATION
             || e->key() == Qt::Key_Select
#endif
             )
            && cursor.hasSelection()) {

            e->accept();
            activateLinkUnderCursor();
            return;
        }
    }

    if (!(interactionFlags & Qt::TextEditable)) {
        e->ignore();
        return;
    }

    if (e->key() == Qt::Key_Direction_L || e->key() == Qt::Key_Direction_R) {
        QTextBlockFormat fmt;
        fmt.setLayoutDirection((e->key() == Qt::Key_Direction_L) ? Qt::LeftToRight : Qt::RightToLeft);
        cursor.mergeBlockFormat(fmt);
        goto accept;
    }

    // schedule a repaint of the region of the cursor, as when we move it we
    // want to make sure the old cursor disappears (not noticeable when moving
    // only a few pixels but noticeable when jumping between cells in tables for
    // example)
    repaintSelection();

    if (e->key() == Qt::Key_Backspace && !(e->modifiers() & ~(Qt::ShiftModifier | Qt::GroupSwitchModifier))) {
        QTextBlockFormat blockFmt = cursor.blockFormat();
        QTextList *list = cursor.currentList();
        if (list && cursor.atBlockStart() && !cursor.hasSelection()) {
            list->remove(cursor.block());
        } else if (cursor.atBlockStart() && blockFmt.indent() > 0) {
            blockFmt.setIndent(blockFmt.indent() - 1);
            cursor.setBlockFormat(blockFmt);
        } else {
            cursor.deletePreviousChar();
            // QTextCursor localCursor = cursor;
            // localCursor.deletePreviousChar();
            // if (cursor.d)
            //     cursor.d->setX();
        }
        goto accept;
    }
#ifndef QT_NO_SHORTCUT
    else if (e == QKeySequence::InsertParagraphSeparator) {
        insertParagraphSeparator();
        e->accept();
        goto accept;
    } else if (e == QKeySequence::InsertLineSeparator) {
        cursor.insertText(QString(QChar::LineSeparator));
        e->accept();
        goto accept;
    }
#endif
    if (false) {
    }
#ifndef QT_NO_SHORTCUT
    else if (e == QKeySequence::Undo) {
        q->undo();
    }
    else if (e == QKeySequence::Redo) {
        q->redo();
    }
#ifndef QT_NO_CLIPBOARD
    else if (e == QKeySequence::Cut) {
        q->cut();
    }
    else if (e == QKeySequence::Paste) {
        QClipboard::Mode mode = QClipboard::Clipboard;
        if (QGuiApplication::clipboard()->supportsSelection()) {
            if (e->modifiers() == (Qt::CTRL | Qt::SHIFT) && e->key() == Qt::Key_Insert)
                mode = QClipboard::Selection;
        }
        q->paste(mode);
    }
#endif
    else if (e == QKeySequence::Delete) {
        cursor.deleteChar();
        // QTextCursor localCursor = cursor;
        // localCursor.deleteChar();
        // if (cursor.d)
        //     cursor.d->setX();
    } else if (e == QKeySequence::Backspace) {
        cursor.deletePreviousChar();
        // QTextCursor localCursor = cursor;
        // localCursor.deletePreviousChar();
        // if (cursor.d)
        //     cursor.d->setX();
    }else if (e == QKeySequence::DeleteEndOfWord) {
        if (!cursor.hasSelection())
            cursor.movePosition(QTextCursor::NextWord, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
    else if (e == QKeySequence::DeleteStartOfWord) {
        if (!cursor.hasSelection())
            cursor.movePosition(QTextCursor::PreviousWord, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
    else if (e == QKeySequence::DeleteEndOfLine) {
        QTextBlock block = cursor.block();
        if (cursor.position() == block.position() + block.length() - 2)
            cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
        else
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
#endif // QT_NO_SHORTCUT
    else {
        goto process;
    }
    goto accept;

process:
{
    if (q->isAcceptableInput(e)) {
        if (overwriteMode
            // no need to call deleteChar() if we have a selection, insertText
            // does it already
            && !cursor.hasSelection()
            && !cursor.atBlockEnd())
            cursor.deleteChar();

        cursor.insertText(e->text());
        selectionChanged();
    } else {
        e->ignore();
        return;
    }
}

accept:

#ifndef QT_NO_CLIPBOARD
    setClipboardSelection();
#endif

    e->accept();
    cursorOn = true;

    q->ensureCursorVisible();

    updateCurrentCharFormat();
}

QVariant WidgetTextControl::loadResource(int type, const QUrl &name)
{
    Q_UNUSED(type);
    Q_UNUSED(name);
    return QVariant();
}

void WidgetTextControlPrivate::_q_updateBlock(const QTextBlock &block)
{
    QRectF br = q->blockBoundingRect(block);
    br.setRight(qreal(INT_MAX)); // the block might have shrunk
    emit q->updateRequest(br);
}

QRectF WidgetTextControlPrivate::rectForPosition(int position) const
{
    const QTextBlock block = doc->findBlock(position);
    if (!block.isValid())
        return QRectF();
    const QAbstractTextDocumentLayout *docLayout = doc->documentLayout();
    const QTextLayout *layout = block.layout();
    const QPointF layoutPos = q->blockBoundingRect(block).topLeft();
    int relativePos = position - block.position();
    if (preeditCursor != 0) {
        int preeditPos = layout->preeditAreaPosition();
        if (relativePos == preeditPos)
            relativePos += preeditCursor;
        else if (relativePos > preeditPos)
            relativePos += layout->preeditAreaText().size();
    }
    QTextLine line = layout->lineForTextPosition(relativePos);

    int cursorWidth;
    {
        bool ok = false;
        cursorWidth = docLayout->property("cursorWidth").toInt(&ok);
        if (!ok)
            cursorWidth = 1;
    }

    QRectF r;

    if (line.isValid()) {
        qreal x = line.cursorToX(relativePos);
        qreal w = 0;
        if (overwriteMode) {
            if (relativePos < line.textLength() - line.textStart())
                w = line.cursorToX(relativePos + 1) - x;
            else
                w = QFontMetrics(block.layout()->font()).horizontalAdvance(u' '); // in sync with QTextLine::draw()
        }
        r = QRectF(layoutPos.x() + x, layoutPos.y() + line.y(),
                   cursorWidth + w, line.height());
    } else {
        r = QRectF(layoutPos.x(), layoutPos.y(), cursorWidth, 10); // #### correct height
    }

    return r;
}

namespace {
struct QTextFrameComparator {
    bool operator()(QTextFrame *frame, int position) { return frame->firstPosition() < position; }
    bool operator()(int position, QTextFrame *frame) { return position < frame->firstPosition(); }
};
}

static QRectF boundingRectOfFloatsInSelection(const QTextCursor &cursor)
{
    QRectF r;
    QTextFrame *frame = cursor.currentFrame();
    const QList<QTextFrame *> children = frame->childFrames();

    const QList<QTextFrame *>::ConstIterator firstFrame = std::lower_bound(children.constBegin(), children.constEnd(),
                                                                           cursor.selectionStart(), QTextFrameComparator());
    const QList<QTextFrame *>::ConstIterator lastFrame = std::upper_bound(children.constBegin(), children.constEnd(),
                                                                          cursor.selectionEnd(), QTextFrameComparator());
    for (QList<QTextFrame *>::ConstIterator it = firstFrame; it != lastFrame; ++it) {
        if ((*it)->frameFormat().position() != QTextFrameFormat::InFlow)
            r |= frame->document()->documentLayout()->frameBoundingRect(*it);
    }
    return r;
}

QRectF WidgetTextControl::selectionRect(const QTextCursor &cursor) const
{

    QRectF r = d->rectForPosition(cursor.selectionStart());

    if (cursor.hasComplexSelection() && cursor.currentTable()) {
        QTextTable *table = cursor.currentTable();

        r = d->doc->documentLayout()->frameBoundingRect(table);
        /*
        int firstRow, numRows, firstColumn, numColumns;
        cursor.selectedTableCells(&firstRow, &numRows, &firstColumn, &numColumns);

        const QTextTableCell firstCell = table->cellAt(firstRow, firstColumn);
        const QTextTableCell lastCell = table->cellAt(firstRow + numRows - 1, firstColumn + numColumns - 1);

        const QAbstractTextDocumentLayout * const layout = doc->documentLayout();

        QRectF tableSelRect = layout->blockBoundingRect(firstCell.firstCursorPosition().block());

        for (int col = firstColumn; col < firstColumn + numColumns; ++col) {
            const QTextTableCell cell = table->cellAt(firstRow, col);
            const qreal y = layout->blockBoundingRect(cell.firstCursorPosition().block()).top();

            tableSelRect.setTop(qMin(tableSelRect.top(), y));
        }

        for (int row = firstRow; row < firstRow + numRows; ++row) {
            const QTextTableCell cell = table->cellAt(row, firstColumn);
            const qreal x = layout->blockBoundingRect(cell.firstCursorPosition().block()).left();

            tableSelRect.setLeft(qMin(tableSelRect.left(), x));
        }

        for (int col = firstColumn; col < firstColumn + numColumns; ++col) {
            const QTextTableCell cell = table->cellAt(firstRow + numRows - 1, col);
            const qreal y = layout->blockBoundingRect(cell.lastCursorPosition().block()).bottom();

            tableSelRect.setBottom(qMax(tableSelRect.bottom(), y));
        }

        for (int row = firstRow; row < firstRow + numRows; ++row) {
            const QTextTableCell cell = table->cellAt(row, firstColumn + numColumns - 1);
            const qreal x = layout->blockBoundingRect(cell.lastCursorPosition().block()).right();

            tableSelRect.setRight(qMax(tableSelRect.right(), x));
        }

        r = tableSelRect.toRect();
        */
    } else if (cursor.hasSelection()) {
        const int position = cursor.selectionStart();
        const int anchor = cursor.selectionEnd();
        const QTextBlock posBlock = d->doc->findBlock(position);
        const QTextBlock anchorBlock = d->doc->findBlock(anchor);
        if (posBlock == anchorBlock && posBlock.isValid() && posBlock.layout()->lineCount()) {
            const QTextLine posLine = posBlock.layout()->lineForTextPosition(position - posBlock.position());
            const QTextLine anchorLine = anchorBlock.layout()->lineForTextPosition(anchor - anchorBlock.position());

            const int firstLine = qMin(posLine.lineNumber(), anchorLine.lineNumber());
            const int lastLine = qMax(posLine.lineNumber(), anchorLine.lineNumber());
            const QTextLayout *layout = posBlock.layout();
            r = QRectF();
            for (int i = firstLine; i <= lastLine; ++i) {
                r |= layout->lineAt(i).rect();
                r |= layout->lineAt(i).naturalTextRect(); // might be bigger in the case of wrap not enabled
            }
            r.translate(blockBoundingRect(posBlock).topLeft());
        } else {
            QRectF anchorRect = d->rectForPosition(cursor.selectionEnd());
            r |= anchorRect;
            r |= boundingRectOfFloatsInSelection(cursor);
            QRectF frameRect(d->doc->documentLayout()->frameBoundingRect(cursor.currentFrame()));
            r.setLeft(frameRect.left());
            r.setRight(frameRect.right());
        }
        if (r.isValid())
            r.adjust(-1, -1, 1, 1);
    }

    return r;
}

QRectF WidgetTextControl::selectionRect() const
{
    return selectionRect(d->cursor);
}

void WidgetTextControlPrivate::mousePressEvent(QEvent *e, Qt::MouseButton button, const QPointF &pos, Qt::KeyboardModifiers modifiers,
    Qt::MouseButtons buttons, const QPoint &globalPos)
{

    mousePressPos = pos.toPoint();

#if QT_CONFIG(draganddrop)
    mightStartDrag = false;
#endif

    if (sendMouseEventToInputContext(
            e, QEvent::MouseButtonPress, button, pos, modifiers, buttons, globalPos)) {
        return;
    }

    if (interactionFlags & Qt::LinksAccessibleByMouse) {
        anchorOnMousePress = q->anchorAt(pos);

        if (cursorIsFocusIndicator) {
            cursorIsFocusIndicator = false;
            repaintSelection();
            cursor.clearSelection();
        }
    }
    if (!(button & Qt::LeftButton) ||
        !((interactionFlags & Qt::TextSelectableByMouse) || (interactionFlags & Qt::TextEditable))) {
        e->ignore();
        return;
    }
    bool wasValid = blockWithMarkerUnderMouse.isValid();
    blockWithMarkerUnderMouse = q->blockWithMarkerAt(pos);
    if (wasValid != blockWithMarkerUnderMouse.isValid())
        emit q->blockMarkerHovered(blockWithMarkerUnderMouse);


    cursorIsFocusIndicator = false;
    const QTextCursor oldSelection = cursor;
    const int oldCursorPos = cursor.position();

    mousePressed = (interactionFlags & Qt::TextSelectableByMouse);

    commitPreedit();

    if (trippleClickTimer.isActive()
        && ((pos - trippleClickPoint).toPoint().manhattanLength() < QApplication::startDragDistance())) {

        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        selectedBlockOnTrippleClick = cursor;

        anchorOnMousePress = QString();
        blockWithMarkerUnderMouse = QTextBlock();
        emit q->blockMarkerHovered(blockWithMarkerUnderMouse);

        trippleClickTimer.stop();
    } else {
        int cursorPos = q->hitTest(pos, Qt::FuzzyHit);
        if (cursorPos == -1) {
            e->ignore();
            return;
        }

        if (modifiers == Qt::ShiftModifier && (interactionFlags & Qt::TextSelectableByMouse)) {
            if (wordSelectionEnabled && !selectedWordOnDoubleClick.hasSelection()) {
                selectedWordOnDoubleClick = cursor;
                selectedWordOnDoubleClick.select(QTextCursor::WordUnderCursor);
            }

            if (selectedBlockOnTrippleClick.hasSelection())
                extendBlockwiseSelection(cursorPos);
            else if (selectedWordOnDoubleClick.hasSelection())
                extendWordwiseSelection(cursorPos, pos.x());
            else if (!wordSelectionEnabled)
                setCursorPosition(cursorPos, QTextCursor::KeepAnchor);
        } else {

            if (dragEnabled
                && cursor.hasSelection()
                && !cursorIsFocusIndicator
                && cursorPos >= cursor.selectionStart()
                && cursorPos <= cursor.selectionEnd()
                && q->hitTest(pos, Qt::ExactHit) != -1) {
#if QT_CONFIG(draganddrop)
                mightStartDrag = true;
#endif
                return;
            }

            setCursorPosition(cursorPos);
        }
    }

    if (interactionFlags & Qt::TextEditable) {
        q->ensureCursorVisible();
        if (cursor.position() != oldCursorPos)
            emit q->cursorPositionChanged();
        _q_updateCurrentCharFormatAndSelection();
    } else {
        if (cursor.position() != oldCursorPos) {
            emit q->cursorPositionChanged();
            emit q->microFocusChanged();
        }
        selectionChanged();
    }
    repaintOldAndNewSelection(oldSelection);
    hadSelectionOnMousePress = cursor.hasSelection();
}

void WidgetTextControlPrivate::mouseMoveEvent(QEvent *e, Qt::MouseButton button, const QPointF &mousePos, Qt::KeyboardModifiers modifiers,
    Qt::MouseButtons buttons, const QPoint &globalPos)
{

    if (interactionFlags & Qt::LinksAccessibleByMouse) {
        QString anchor = q->anchorAt(mousePos);
        if (anchor != highlightedAnchor) {
            highlightedAnchor = anchor;
            emit q->linkHovered(anchor);
        }
    }

    if (buttons & Qt::LeftButton) {
        const bool editable = interactionFlags & Qt::TextEditable;

        if (!(mousePressed
              || editable
              || mightStartDrag
              || selectedWordOnDoubleClick.hasSelection()
              || selectedBlockOnTrippleClick.hasSelection()))
            return;

        const QTextCursor oldSelection = cursor;
        const int oldCursorPos = cursor.position();

        if (mightStartDrag) {
            if ((mousePos.toPoint() - mousePressPos).manhattanLength() > QApplication::startDragDistance())
                startDrag();
            return;
        }

        const qreal mouseX = qreal(mousePos.x());

        int newCursorPos = q->hitTest(mousePos, Qt::FuzzyHit);

        if (isPreediting()) {
            // note: oldCursorPos not including preedit
            int selectionStartPos = q->hitTest(mousePressPos, Qt::FuzzyHit);

            if (newCursorPos != selectionStartPos) {
                commitPreedit();
                // commit invalidates positions
                newCursorPos = q->hitTest(mousePos, Qt::FuzzyHit);
                selectionStartPos = q->hitTest(mousePressPos, Qt::FuzzyHit);
                setCursorPosition(selectionStartPos);
            }
        }

        if (newCursorPos == -1)
            return;

        if (mousePressed && wordSelectionEnabled && !selectedWordOnDoubleClick.hasSelection()) {
            selectedWordOnDoubleClick = cursor;
            selectedWordOnDoubleClick.select(QTextCursor::WordUnderCursor);
        }

        if (selectedBlockOnTrippleClick.hasSelection())
            extendBlockwiseSelection(newCursorPos);
        else if (selectedWordOnDoubleClick.hasSelection())
            extendWordwiseSelection(newCursorPos, mouseX);
        else if (mousePressed && !isPreediting())
            setCursorPosition(newCursorPos, QTextCursor::KeepAnchor);

        if (interactionFlags & Qt::TextEditable) {
            // don't call ensureVisible for the visible cursor to avoid jumping
            // scrollbars. the autoscrolling ensures smooth scrolling if necessary.
            //q->ensureCursorVisible();
            if (cursor.position() != oldCursorPos)
                emit q->cursorPositionChanged();
            _q_updateCurrentCharFormatAndSelection();
#ifndef QT_NO_IM
            if (contextWidget)
                QGuiApplication::inputMethod()->update(Qt::ImQueryInput);
#endif //QT_NO_IM
        } else {
            //emit q->visibilityRequest(QRectF(mousePos, QSizeF(1, 1)));
            if (cursor.position() != oldCursorPos) {
                emit q->cursorPositionChanged();
                emit q->microFocusChanged();
            }
        }
        selectionChanged(true);
        repaintOldAndNewSelection(oldSelection);
    } else {
        bool wasValid = blockWithMarkerUnderMouse.isValid();
        blockWithMarkerUnderMouse = q->blockWithMarkerAt(mousePos);
        if (wasValid != blockWithMarkerUnderMouse.isValid())
            emit q->blockMarkerHovered(blockWithMarkerUnderMouse);
    }

    sendMouseEventToInputContext(e, QEvent::MouseMove, button, mousePos, modifiers, buttons, globalPos);
}

void WidgetTextControlPrivate::mouseReleaseEvent(QEvent *e, Qt::MouseButton button, const QPointF &pos, Qt::KeyboardModifiers modifiers,
    Qt::MouseButtons buttons, const QPoint &globalPos)
{

    const QTextCursor oldSelection = cursor;
    if (sendMouseEventToInputContext(
            e, QEvent::MouseButtonRelease, button, pos, modifiers, buttons, globalPos)) {
        repaintOldAndNewSelection(oldSelection);
        return;
    }

    const int oldCursorPos = cursor.position();

#if QT_CONFIG(draganddrop)
    if (mightStartDrag && (button & Qt::LeftButton)) {
        mousePressed = false;
        setCursorPosition(pos);
        cursor.clearSelection();
        selectionChanged();
    }
#endif
    if (mousePressed) {
        mousePressed = false;
#ifndef QT_NO_CLIPBOARD
        setClipboardSelection();
        selectionChanged(true);
    } else if (button == Qt::MiddleButton
               && (interactionFlags & Qt::TextEditable)
               && QGuiApplication::clipboard()->supportsSelection()) {
        setCursorPosition(pos);
        const QMimeData *md = QGuiApplication::clipboard()->mimeData(QClipboard::Selection);
        if (md)
            q->insertFromMimeData(md);
#endif
    }

    repaintOldAndNewSelection(oldSelection);

    if (cursor.position() != oldCursorPos) {
        emit q->cursorPositionChanged();
        emit q->microFocusChanged();
    }

    // toggle any checkbox that the user clicks
    if ((interactionFlags & Qt::TextEditable) && (button & Qt::LeftButton) &&
        (blockWithMarkerUnderMouse.isValid()) && !cursor.hasSelection()) {
        QTextBlock markerBlock = q->blockWithMarkerAt(pos);
        if (markerBlock == blockWithMarkerUnderMouse) {
            auto fmt = blockWithMarkerUnderMouse.blockFormat();
            switch (fmt.marker()) {
            case QTextBlockFormat::MarkerType::Unchecked :
                fmt.setMarker(QTextBlockFormat::MarkerType::Checked);
                break;
            case QTextBlockFormat::MarkerType::Checked:
                fmt.setMarker(QTextBlockFormat::MarkerType::Unchecked);
                break;
            default:
                break;
            }
            cursor.setBlockFormat(fmt);
        }
    }

    if (interactionFlags & Qt::LinksAccessibleByMouse) {

        // Ignore event unless left button has been pressed
        if (!(button & Qt::LeftButton)) {
            e->ignore();
            return;
        }

        const QString anchor = q->anchorAt(pos);

        // Ignore event without selection anchor
        if (anchor.isEmpty()) {
            e->ignore();
            return;
        }

        if (!cursor.hasSelection()
            || (anchor == anchorOnMousePress && hadSelectionOnMousePress)) {

            const int anchorPos = q->hitTest(pos, Qt::ExactHit);

            // Ignore event without valid anchor position
            if (anchorPos < 0) {
                e->ignore();
                return;
            }

            cursor.setPosition(anchorPos);
            QString anchor = anchorOnMousePress;
            anchorOnMousePress = QString();
            activateLinkUnderCursor(anchor);
        }
    }
}

void WidgetTextControlPrivate::mouseDoubleClickEvent(QEvent *e, Qt::MouseButton button, const QPointF &pos,
    Qt::KeyboardModifiers modifiers, Qt::MouseButtons buttons,
    const QPoint &globalPos)
{

    if (button == Qt::LeftButton
        && (interactionFlags & Qt::TextSelectableByMouse)) {

#if QT_CONFIG(draganddrop)
        mightStartDrag = false;
#endif
        commitPreedit();

        const QTextCursor oldSelection = cursor;
        setCursorPosition(pos);
        QTextLine line = currentTextLine(cursor);
        bool doEmit = false;
        if (line.isValid() && line.textLength()) {
            cursor.select(QTextCursor::WordUnderCursor);
            doEmit = true;
        }
        repaintOldAndNewSelection(oldSelection);

        cursorIsFocusIndicator = false;
        selectedWordOnDoubleClick = cursor;

        trippleClickPoint = pos;
        trippleClickTimer.start(QApplication::doubleClickInterval(), q);
        if (doEmit) {
            selectionChanged();
#ifndef QT_NO_CLIPBOARD
            setClipboardSelection();
#endif
            emit q->cursorPositionChanged();
        }
    } else if (!sendMouseEventToInputContext(e, QEvent::MouseButtonDblClick, button, pos,
                                             modifiers, buttons, globalPos)) {
        e->ignore();
    }
}

bool WidgetTextControlPrivate::sendMouseEventToInputContext(
    QEvent *e, QEvent::Type eventType, Qt::MouseButton button, const QPointF &pos,
    Qt::KeyboardModifiers modifiers, Qt::MouseButtons buttons, const QPoint &globalPos)
{
    Q_UNUSED(eventType);
    Q_UNUSED(button);
    Q_UNUSED(pos);
    Q_UNUSED(modifiers);
    Q_UNUSED(buttons);
    Q_UNUSED(globalPos);
#if !defined(QT_NO_IM)

    if (isPreediting()) {
        QTextLayout *layout = cursor.block().layout();
        int cursorPos = q->hitTest(pos, Qt::FuzzyHit) - cursor.position();

        if (cursorPos < 0 || cursorPos > layout->preeditAreaText().size())
            cursorPos = -1;

        if (cursorPos >= 0) {
            if (eventType == QEvent::MouseButtonRelease)
                QGuiApplication::inputMethod()->invokeAction(QInputMethod::Click, cursorPos);

            e->setAccepted(true);
            return true;
        }
    }
#else
    Q_UNUSED(e);
#endif
    return false;
}

void WidgetTextControlPrivate::contextMenuEvent(const QPoint &screenPos, const QPointF &docPos, QWidget *contextWidget)
{
#ifdef QT_NO_CONTEXTMENU
    Q_UNUSED(screenPos);
    Q_UNUSED(docPos);
    Q_UNUSED(contextWidget);
#else
    QMenu *menu = q->createStandardContextMenu(docPos, contextWidget);
    if (!menu)
        return;
    menu->setAttribute(Qt::WA_DeleteOnClose);

    if (auto *widget = qobject_cast<QWidget *>(parent())) {
        if (auto *window = widget->window()->windowHandle())
            ;// QMenuPrivate::get(menu)->topData()->initialScreen = window->screen();
    }

    menu->popup(screenPos);
#endif
}

bool WidgetTextControlPrivate::dragEnterEvent(QEvent *e, const QMimeData *mimeData)
{
    if (!(interactionFlags & Qt::TextEditable) || !q->canInsertFromMimeData(mimeData)) {
        e->ignore();
        return false;
    }

    dndFeedbackCursor = QTextCursor();

    return true; // accept proposed action
}

void WidgetTextControlPrivate::dragLeaveEvent()
{

    const QRectF crect = q->cursorRect(dndFeedbackCursor);
    dndFeedbackCursor = QTextCursor();

    if (crect.isValid())
        emit q->updateRequest(crect);
}

bool WidgetTextControlPrivate::dragMoveEvent(QEvent *e, const QMimeData *mimeData, const QPointF &pos)
{
    if (!(interactionFlags & Qt::TextEditable) || !q->canInsertFromMimeData(mimeData)) {
        e->ignore();
        return false;
    }

    const int cursorPos = q->hitTest(pos, Qt::FuzzyHit);
    if (cursorPos != -1) {
        QRectF crect = q->cursorRect(dndFeedbackCursor);
        if (crect.isValid())
            emit q->updateRequest(crect);

        dndFeedbackCursor = cursor;
        dndFeedbackCursor.setPosition(cursorPos);

        crect = q->cursorRect(dndFeedbackCursor);
        emit q->updateRequest(crect);
    }

    return true; // accept proposed action
}

bool WidgetTextControlPrivate::dropEvent(const QMimeData *mimeData, const QPointF &pos, Qt::DropAction dropAction, QObject *source)
{
    dndFeedbackCursor = QTextCursor();

    if (!(interactionFlags & Qt::TextEditable) || !q->canInsertFromMimeData(mimeData))
        return false;

    repaintSelection();

    QTextCursor insertionCursor = q->cursorForPosition(pos);
    insertionCursor.beginEditBlock();

    if (dropAction == Qt::MoveAction && source == contextWidget)
        cursor.removeSelectedText();

    cursor = insertionCursor;
    q->insertFromMimeData(mimeData);
    insertionCursor.endEditBlock();
    q->ensureCursorVisible();
    return true; // accept proposed action
}

void WidgetTextControlPrivate::inputMethodEvent(QInputMethodEvent *e)
{
    if (!(interactionFlags & (Qt::TextEditable | Qt::TextSelectableByMouse)) || cursor.isNull()) {
        e->ignore();
        return;
    }
    bool isGettingInput = !e->commitString().isEmpty()
                          || e->preeditString() != cursor.block().layout()->preeditAreaText()
                          || e->replacementLength() > 0;

    if (!isGettingInput && e->attributes().isEmpty()) {
        e->ignore();
        return;
    }

    int oldCursorPos = cursor.position();

    cursor.beginEditBlock();
    if (isGettingInput) {
        cursor.removeSelectedText();
    }

    QTextBlock block;

    // insert commit string
    if (!e->commitString().isEmpty() || e->replacementLength()) {
        if (e->commitString().endsWith(QChar::LineFeed))
            block = cursor.block(); // Remember the block where the preedit text is
        QTextCursor c = cursor;
        c.setPosition(c.position() + e->replacementStart());
        c.setPosition(c.position() + e->replacementLength(), QTextCursor::KeepAnchor);
        c.insertText(e->commitString());
    }

    for (int i = 0; i < e->attributes().size(); ++i) {
        const QInputMethodEvent::Attribute &a = e->attributes().at(i);
        if (a.type == QInputMethodEvent::Selection) {
            QTextCursor oldCursor = cursor;
            int blockStart = a.start + cursor.block().position();
            cursor.setPosition(blockStart, QTextCursor::MoveAnchor);
            cursor.setPosition(blockStart + a.length, QTextCursor::KeepAnchor);
            q->ensureCursorVisible();
            repaintOldAndNewSelection(oldCursor);
        }
    }

    if (!block.isValid())
        block = cursor.block();
    QTextLayout *layout = block.layout();
    if (isGettingInput)
        layout->setPreeditArea(cursor.position() - block.position(), e->preeditString());
    QList<QTextLayout::FormatRange> overrides;
    overrides.reserve(e->attributes().size());
    const int oldPreeditCursor = preeditCursor;
    preeditCursor = e->preeditString().size();
    hideCursor = false;
    for (int i = 0; i < e->attributes().size(); ++i) {
        const QInputMethodEvent::Attribute &a = e->attributes().at(i);
        if (a.type == QInputMethodEvent::Cursor) {
            preeditCursor = a.start;
            hideCursor = !a.length;
        } else if (a.type == QInputMethodEvent::TextFormat) {
            QTextCharFormat f = cursor.charFormat();
            f.merge(qvariant_cast<QTextFormat>(a.value).toCharFormat());
            if (f.isValid()) {
                QTextLayout::FormatRange o;
                o.start = a.start + cursor.position() - block.position();
                o.length = a.length;
                o.format = f;

                // Make sure list is sorted by start index
                QList<QTextLayout::FormatRange>::iterator it = overrides.end();
                while (it != overrides.begin()) {
                    QList<QTextLayout::FormatRange>::iterator previous = it - 1;
                    if (o.start >= previous->start) {
                        overrides.insert(it, o);
                        break;
                    }
                    it = previous;
                }

                if (it == overrides.begin())
                    overrides.prepend(o);
            }
        }
    }

    if (cursor.charFormat().isValid()) {
        int start = cursor.position() - block.position();
        int end = start + e->preeditString().size();

        QList<QTextLayout::FormatRange>::iterator it = overrides.begin();
        while (it != overrides.end()) {
            QTextLayout::FormatRange range = *it;
            int rangeStart = range.start;
            if (rangeStart > start) {
                QTextLayout::FormatRange o;
                o.start = start;
                o.length = rangeStart - start;
                o.format = cursor.charFormat();
                it = overrides.insert(it, o) + 1;
            }

            ++it;
            start = range.start + range.length;
        }

        if (start < end) {
            QTextLayout::FormatRange o;
            o.start = start;
            o.length = end - start;
            o.format = cursor.charFormat();
            overrides.append(o);
        }
    }
    layout->setFormats(overrides);

    cursor.endEditBlock();

    // if (cursor.d)
    //     cursor.d->setX();
    if (oldCursorPos != cursor.position())
        emit q->cursorPositionChanged();
    if (oldPreeditCursor != preeditCursor)
        emit q->microFocusChanged();
}

QVariant WidgetTextControl::inputMethodQuery(Qt::InputMethodQuery property, QVariant argument) const
{
    QTextBlock block = d->cursor.block();
    switch(property) {
    case Qt::ImCursorRectangle:
        return cursorRect();
    case Qt::ImAnchorRectangle:
        return d->rectForPosition(d->cursor.anchor());
    case Qt::ImFont:
        return QVariant(d->cursor.charFormat().font());
    case Qt::ImCursorPosition: {
        const QPointF pt = argument.toPointF();
        if (!pt.isNull())
            return QVariant(cursorForPosition(pt).position() - block.position());
        return QVariant(d->cursor.position() - block.position()); }
    case Qt::ImSurroundingText:
        return QVariant(block.text());
    case Qt::ImCurrentSelection:
        return QVariant(d->cursor.selectedText());
    case Qt::ImMaximumTextLength:
        return QVariant(); // No limit.
    case Qt::ImAnchorPosition:
        return QVariant(d->cursor.anchor() - block.position());
    case Qt::ImAbsolutePosition: {
        const QPointF pt = argument.toPointF();
        if (!pt.isNull())
            return QVariant(cursorForPosition(pt).position());
        return QVariant(d->cursor.position()); }
    case Qt::ImTextAfterCursor:
    {
        int maxLength = argument.isValid() ? argument.toInt() : 1024;
        QTextCursor tmpCursor = d->cursor;
        int localPos = d->cursor.position() - block.position();
        QString result = block.text().mid(localPos);
        while (result.size() < maxLength) {
            int currentBlock = tmpCursor.blockNumber();
            tmpCursor.movePosition(QTextCursor::NextBlock);
            if (tmpCursor.blockNumber() == currentBlock)
                break;
            result += u'\n' + tmpCursor.block().text();
        }
        return QVariant(result);
    }
    case Qt::ImTextBeforeCursor:
    {
        int maxLength = argument.isValid() ? argument.toInt() : 1024;
        QTextCursor tmpCursor = d->cursor;
        int localPos = d->cursor.position() - block.position();
        int numBlocks = 0;
        int resultLen = localPos;
        while (resultLen < maxLength) {
            int currentBlock = tmpCursor.blockNumber();
            tmpCursor.movePosition(QTextCursor::PreviousBlock);
            if (tmpCursor.blockNumber() == currentBlock)
                break;
            numBlocks++;
            resultLen += tmpCursor.block().length();
        }
        QString result;
        while (numBlocks) {
            result += tmpCursor.block().text() + u'\n';
            tmpCursor.movePosition(QTextCursor::NextBlock);
            --numBlocks;
        }
        result += QStringView{block.text()}.mid(0, localPos);
        return QVariant(result);
    }
    default:
        return QVariant();
    }
}

void WidgetTextControl::setFocus(bool focus, Qt::FocusReason reason)
{
    QFocusEvent ev(focus ? QEvent::FocusIn : QEvent::FocusOut,
                   reason);
    processEvent(&ev);
}

void WidgetTextControlPrivate::focusEvent(QFocusEvent *e)
{
    emit q->updateRequest(q->selectionRect());
    if (e->gotFocus()) {
#ifdef QT_KEYPAD_NAVIGATION
        if (!QApplicationPrivate::keypadNavigationEnabled() || (hasEditFocus && (e->reason() == Qt::PopupFocusReason))) {
#endif
            cursorOn = (interactionFlags & (Qt::TextSelectableByKeyboard | Qt::TextEditable));
            if (interactionFlags & Qt::TextEditable) {
                setCursorVisible(true);
            }
#ifdef QT_KEYPAD_NAVIGATION
        }
#endif
    } else {
        setCursorVisible(false);
        cursorOn = false;

        if (cursorIsFocusIndicator
            && e->reason() != Qt::ActiveWindowFocusReason
            && e->reason() != Qt::PopupFocusReason
            && cursor.hasSelection()) {
            cursor.clearSelection();
        }
    }
    hasFocus = e->gotFocus();
}

QString WidgetTextControlPrivate::anchorForCursor(const QTextCursor &anchorCursor) const
{
    if (anchorCursor.hasSelection()) {
        QTextCursor cursor = anchorCursor;
        if (cursor.selectionStart() != cursor.position())
            cursor.setPosition(cursor.selectionStart());
        cursor.movePosition(QTextCursor::NextCharacter);
        QTextCharFormat fmt = cursor.charFormat();
        if (fmt.isAnchor() && fmt.hasProperty(QTextFormat::AnchorHref))
            return fmt.stringProperty(QTextFormat::AnchorHref);
    }
    return QString();
}

#ifdef QT_KEYPAD_NAVIGATION
void WidgetTextControlPrivate::editFocusEvent(QEvent *e)
{

    if (QApplicationPrivate::keypadNavigationEnabled()) {
        if (e->type() == QEvent::EnterEditFocus && interactionFlags & Qt::TextEditable) {
            const QTextCursor oldSelection = cursor;
            const int oldCursorPos = cursor.position();
            const bool moved = cursor.movePosition(QTextCursor::End, QTextCursor::MoveAnchor);
            q->ensureCursorVisible();
            if (moved) {
                if (cursor.position() != oldCursorPos)
                    emit q->cursorPositionChanged();
                emit q->microFocusChanged();
            }
            selectionChanged();
            repaintOldAndNewSelection(oldSelection);

            setBlinkingCursorEnabled(true);
        } else
            setBlinkingCursorEnabled(false);
    }

    hasEditFocus = e->type() == QEvent::EnterEditFocus;
}
#endif

#ifndef QT_NO_CONTEXTMENU
void setActionIcon(QAction *action, const QString &name)
{
    const QIcon icon = QIcon::fromTheme(name);
    if (!icon.isNull())
        action->setIcon(icon);
}

static QString ACCEL_KEY(QKeySequence::StandardKey k)
{
    auto result = QKeySequence(k).toString(QKeySequence::NativeText);
    if (!result.isEmpty())
        result.prepend('\t');
    return result;
}

QMenu *WidgetTextControl::createStandardContextMenu(const QPointF &pos, QWidget *parent)
{

    const bool showTextSelectionActions = d->interactionFlags & (Qt::TextEditable | Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);

    d->linkToCopy = QString();
    if (!pos.isNull())
        d->linkToCopy = anchorAt(pos);

    if (d->linkToCopy.isEmpty() && !showTextSelectionActions)
        return nullptr;

    QMenu *menu = new QMenu(parent);
    QAction *a;

    if (d->interactionFlags & Qt::TextEditable) {
        a = menu->addAction(tr("&Undo") + ACCEL_KEY(QKeySequence::Undo), this, SLOT(undo()));
        a->setEnabled(d->doc->isUndoAvailable());
        a->setObjectName(QStringLiteral("edit-undo"));
        setActionIcon(a, QStringLiteral("edit-undo"));
        a = menu->addAction(tr("&Redo") + ACCEL_KEY(QKeySequence::Redo), this, SLOT(redo()));
        a->setEnabled(d->doc->isRedoAvailable());
        a->setObjectName(QStringLiteral("edit-redo"));
        setActionIcon(a, QStringLiteral("edit-redo"));
        menu->addSeparator();

#ifndef QT_NO_CLIPBOARD
        a = menu->addAction(tr("Cu&t") + ACCEL_KEY(QKeySequence::Cut), this, SLOT(cut()));
        a->setEnabled(d->cursor.hasSelection());
        a->setObjectName(QStringLiteral("edit-cut"));
        setActionIcon(a, QStringLiteral("edit-cut"));
#endif
    }

#ifndef QT_NO_CLIPBOARD
    if (showTextSelectionActions) {
        a = menu->addAction(tr("&Copy") + ACCEL_KEY(QKeySequence::Copy), this, SLOT(copy()));
        a->setEnabled(d->cursor.hasSelection());
        a->setObjectName(QStringLiteral("edit-copy"));
        setActionIcon(a, QStringLiteral("edit-copy"));
    }

    if ((d->interactionFlags & Qt::LinksAccessibleByKeyboard)
        || (d->interactionFlags & Qt::LinksAccessibleByMouse)) {

        a = menu->addAction(tr("Copy &Link Location"), this, SLOT(_q_copyLink()));
        a->setEnabled(!d->linkToCopy.isEmpty());
        a->setObjectName(QStringLiteral("link-copy"));
    }
#endif // QT_NO_CLIPBOARD

    if (d->interactionFlags & Qt::TextEditable) {
#ifndef QT_NO_CLIPBOARD
        a = menu->addAction(tr("&Paste") + ACCEL_KEY(QKeySequence::Paste), this, SLOT(paste()));
        a->setEnabled(canPaste());
        a->setObjectName(QStringLiteral("edit-paste"));
        setActionIcon(a, QStringLiteral("edit-paste"));
#endif
        a = menu->addAction(tr("Delete"), this, SLOT(_q_deleteSelected()));
        a->setEnabled(d->cursor.hasSelection());
        a->setObjectName(QStringLiteral("edit-delete"));
        setActionIcon(a, QStringLiteral("edit-delete"));
    }


    if (showTextSelectionActions) {
        menu->addSeparator();
        a = menu->addAction(tr("Select All") + ACCEL_KEY(QKeySequence::SelectAll), this, SLOT(selectAll()));
        a->setEnabled(!d->doc->isEmpty());
        a->setObjectName(QStringLiteral("select-all"));
        setActionIcon(a, QStringLiteral("edit-select-all"));
    }

    if ((d->interactionFlags & Qt::TextEditable) && QGuiApplication::styleHints()->useRtlExtensions()) {
        menu->addSeparator();
        UnicodeControlCharacterMenu *ctrlCharacterMenu = new UnicodeControlCharacterMenu(this, menu);
        menu->addMenu(ctrlCharacterMenu);
    }

    return menu;
}
#endif // QT_NO_CONTEXTMENU

QTextCursor WidgetTextControl::cursorForPosition(const QPointF &pos) const
{
    int cursorPos = hitTest(pos, Qt::FuzzyHit);
    if (cursorPos == -1)
        cursorPos = 0;
    QTextCursor c(d->doc);
    c.setPosition(cursorPos);
    return c;
}

QRectF WidgetTextControl::cursorRect(const QTextCursor &cursor) const
{
    if (cursor.isNull())
        return QRectF();

    return d->rectForPosition(cursor.position());
}

QRectF WidgetTextControl::cursorRect() const
{
    return cursorRect(d->cursor);
}

QRectF WidgetTextControlPrivate::cursorRectPlusUnicodeDirectionMarkers(const QTextCursor &cursor) const
{
    if (cursor.isNull())
        return QRectF();

    return rectForPosition(cursor.position()).adjusted(-4, 0, 4, 0);
}

QString WidgetTextControl::anchorAt(const QPointF &pos) const
{
    return d->doc->documentLayout()->anchorAt(pos);
}

QString WidgetTextControl::anchorAtCursor() const
{

    return d->anchorForCursor(d->cursor);
}

QTextBlock WidgetTextControl::blockWithMarkerAt(const QPointF &pos) const
{
    return d->doc->documentLayout()->blockWithMarkerAt(pos);
}

bool WidgetTextControl::overwriteMode() const
{
    return d->overwriteMode;
}

void WidgetTextControl::setOverwriteMode(bool overwrite)
{
    d->overwriteMode = overwrite;
}

int WidgetTextControl::cursorWidth() const
{
    return d->doc->documentLayout()->property("cursorWidth").toInt();
}

void WidgetTextControl::setCursorWidth(int width)
{
    if (width == -1)
        width = QApplication::style()->pixelMetric(QStyle::PM_TextCursorWidth, nullptr, qobject_cast<QWidget *>(parent()));
    d->doc->documentLayout()->setProperty("cursorWidth", width);
    d->repaintCursor();
}

bool WidgetTextControl::acceptRichText() const
{
    return d->acceptRichText;
}

void WidgetTextControl::setAcceptRichText(bool accept)
{
    d->acceptRichText = accept;
}

#if QT_CONFIG(textedit)

void WidgetTextControl::setExtraSelections(const QList<QTextEdit::ExtraSelection> &selections)
{

    QMultiHash<int, int> hash;
    for (int i = 0; i < d->extraSelections.size(); ++i) {
        const QAbstractTextDocumentLayout::Selection &esel = d->extraSelections.at(i);
        hash.insert(esel.cursor.anchor(), i);
    }

    for (int i = 0; i < selections.size(); ++i) {
        const QTextEdit::ExtraSelection &sel = selections.at(i);
        const auto it = hash.constFind(sel.cursor.anchor());
        if (it != hash.cend()) {
            const QAbstractTextDocumentLayout::Selection &esel = d->extraSelections.at(it.value());
            if (esel.cursor.position() == sel.cursor.position()
                && esel.format == sel.format) {
                hash.erase(it);
                continue;
            }
        }
        QRectF r = selectionRect(sel.cursor);
        if (sel.format.boolProperty(QTextFormat::FullWidthSelection)) {
            r.setLeft(0);
            r.setWidth(qreal(INT_MAX));
        }
        emit updateRequest(r);
    }

    for (auto it = hash.cbegin(); it != hash.cend(); ++it) {
        const QAbstractTextDocumentLayout::Selection &esel = d->extraSelections.at(it.value());
        QRectF r = selectionRect(esel.cursor);
        if (esel.format.boolProperty(QTextFormat::FullWidthSelection)) {
            r.setLeft(0);
            r.setWidth(qreal(INT_MAX));
        }
        emit updateRequest(r);
    }

    d->extraSelections.resize(selections.size());
    for (int i = 0; i < selections.size(); ++i) {
        d->extraSelections[i].cursor = selections.at(i).cursor;
        d->extraSelections[i].format = selections.at(i).format;
    }
}

QList<QTextEdit::ExtraSelection> WidgetTextControl::extraSelections() const
{
    QList<QTextEdit::ExtraSelection> selections;
    const int numExtraSelections = d->extraSelections.size();
    selections.reserve(numExtraSelections);
    for (int i = 0; i < numExtraSelections; ++i) {
        QTextEdit::ExtraSelection sel;
        const QAbstractTextDocumentLayout::Selection &sel2 = d->extraSelections.at(i);
        sel.cursor = sel2.cursor;
        sel.format = sel2.format;
        selections.append(sel);
    }
    return selections;
}

#endif // QT_CONFIG(textedit)

void WidgetTextControl::setTextWidth(qreal width)
{
    d->doc->setTextWidth(width);
}

qreal WidgetTextControl::textWidth() const
{
    return d->doc->textWidth();
}

QSizeF WidgetTextControl::size() const
{
    return d->doc->size();
}

void WidgetTextControl::setOpenExternalLinks(bool open)
{
    d->openExternalLinks = open;
}

bool WidgetTextControl::openExternalLinks() const
{
    return d->openExternalLinks;
}

bool WidgetTextControl::ignoreUnusedNavigationEvents() const
{
    return d->ignoreUnusedNavigationEvents;
}

void WidgetTextControl::setIgnoreUnusedNavigationEvents(bool ignore)
{
    d->ignoreUnusedNavigationEvents = ignore;
}

void WidgetTextControl::moveCursor(QTextCursor::MoveOperation op, QTextCursor::MoveMode mode)
{
    const QTextCursor oldSelection = d->cursor;
    const bool moved = d->cursor.movePosition(op, mode);
    d->_q_updateCurrentCharFormatAndSelection();
    ensureCursorVisible();
    d->repaintOldAndNewSelection(oldSelection);
    if (moved)
        emit cursorPositionChanged();
}

bool WidgetTextControl::canPaste() const
{
#ifndef QT_NO_CLIPBOARD
    if (d->interactionFlags & Qt::TextEditable) {
        const QMimeData *md = QGuiApplication::clipboard()->mimeData();
        return md && canInsertFromMimeData(md);
    }
#endif
    return false;
}

void WidgetTextControl::setCursorIsFocusIndicator(bool b)
{
    d->cursorIsFocusIndicator = b;
    d->repaintCursor();
}

bool WidgetTextControl::cursorIsFocusIndicator() const
{
    return d->cursorIsFocusIndicator;
}


void WidgetTextControl::setDragEnabled(bool enabled)
{
    d->dragEnabled = enabled;
}

bool WidgetTextControl::isDragEnabled() const
{
    return d->dragEnabled;
}

void WidgetTextControl::setWordSelectionEnabled(bool enabled)
{
    d->wordSelectionEnabled = enabled;
}

bool WidgetTextControl::isWordSelectionEnabled() const
{
    return d->wordSelectionEnabled;
}

bool WidgetTextControl::isPreediting()
{
    return d->isPreediting();
}

#ifndef QT_NO_PRINTER
void WidgetTextControl::print(QPagedPaintDevice *printer) const
{
    if (!printer)
        return;
    QTextDocument *tempDoc = nullptr;
    const QTextDocument *doc = d->doc;
    if (auto qprinter = dynamic_cast<QPrinter *>(printer); qprinter && qprinter->printRange() == QPrinter::Selection) {
        if (!d->cursor.hasSelection())
            return;
        tempDoc = new QTextDocument(const_cast<QTextDocument *>(doc));
        tempDoc->setResourceProvider(doc->resourceProvider());
        tempDoc->setMetaInformation(QTextDocument::DocumentTitle, doc->metaInformation(QTextDocument::DocumentTitle));
        tempDoc->setPageSize(doc->pageSize());
        tempDoc->setDefaultFont(doc->defaultFont());
        tempDoc->setUseDesignMetrics(doc->useDesignMetrics());
        QTextCursor(tempDoc).insertFragment(d->cursor.selection());
        doc = tempDoc;

        // copy the custom object handlers
        // doc->documentLayout()->d_func()->handlers = d->doc->documentLayout()->d_func()->handlers;
    }
    doc->print(printer);
    delete tempDoc;
}
#endif

QMimeData *WidgetTextControl::createMimeDataFromSelection() const
{
    const QTextDocumentFragment fragment(d->cursor);
    return new TextEditMimeData(fragment);
}

bool WidgetTextControl::canInsertFromMimeData(const QMimeData *source) const
{
    if (d->acceptRichText)
        return (source->hasText() && !source->text().isEmpty())
               || source->hasHtml()
               || source->hasFormat("application/x-qrichtext")
               || source->hasFormat("application/x-qt-richtext");
    else
        return source->hasText() && !source->text().isEmpty();
}

void WidgetTextControl::insertFromMimeData(const QMimeData *source)
{
    if (!(d->interactionFlags & Qt::TextEditable) || !source)
        return;

    bool hasData = false;
    QTextDocumentFragment fragment;
#if QT_CONFIG(textmarkdownreader)
    const auto formats = source->formats();
    if (formats.size() && formats.first() == "text/markdown") {
        auto s = QString::fromUtf8(source->data("text/markdown"));
        fragment = QTextDocumentFragment::fromMarkdown(s);
        hasData = true;
    } else
#endif
#ifndef QT_NO_TEXTHTMLPARSER
        if (source->hasFormat("application/x-qrichtext") && d->acceptRichText) {
            // x-qrichtext is always UTF-8 (taken from Qt3 since we don't use it anymore).
            const QString richtext = "<meta name=\"qrichtext\" content=\"1\" />"
                                     + QString::fromUtf8(source->data("application/x-qrichtext"));
            fragment = QTextDocumentFragment::fromHtml(richtext, d->doc);
            hasData = true;
        } else if (source->hasHtml() && d->acceptRichText) {
            fragment = QTextDocumentFragment::fromHtml(source->html(), d->doc);
            hasData = true;
        }
#endif // QT_NO_TEXTHTMLPARSER
    if (!hasData) {
        const QString text = source->text();
        if (!text.isNull()) {
            fragment = QTextDocumentFragment::fromPlainText(text);
            hasData = true;
        }
    }

    if (hasData)
        d->cursor.insertFragment(fragment);
    ensureCursorVisible();
}

bool WidgetTextControl::findNextPrevAnchor(const QTextCursor &startCursor, bool next, QTextCursor &newAnchor)
{

    int anchorStart = -1;
    QString anchorHref;
    int anchorEnd = -1;

    if (next) {
        const int startPos = startCursor.selectionEnd();

        QTextBlock block = d->doc->findBlock(startPos);
        QTextBlock::Iterator it = block.begin();

        while (!it.atEnd() && it.fragment().position() < startPos)
            ++it;

        while (block.isValid()) {
            anchorStart = -1;

            // find next anchor
            for (; !it.atEnd(); ++it) {
                const QTextFragment fragment = it.fragment();
                const QTextCharFormat fmt = fragment.charFormat();

                if (fmt.isAnchor() && fmt.hasProperty(QTextFormat::AnchorHref)) {
                    anchorStart = fragment.position();
                    anchorHref = fmt.anchorHref();
                    break;
                }
            }

            if (anchorStart != -1) {
                anchorEnd = -1;

                // find next non-anchor fragment
                for (; !it.atEnd(); ++it) {
                    const QTextFragment fragment = it.fragment();
                    const QTextCharFormat fmt = fragment.charFormat();

                    if (!fmt.isAnchor() || fmt.anchorHref() != anchorHref) {
                        anchorEnd = fragment.position();
                        break;
                    }
                }

                if (anchorEnd == -1)
                    anchorEnd = block.position() + block.length() - 1;

                // make found selection
                break;
            }

            block = block.next();
            it = block.begin();
        }
    } else {
        int startPos = startCursor.selectionStart();
        if (startPos > 0)
            --startPos;

        QTextBlock block = d->doc->findBlock(startPos);
        QTextBlock::Iterator blockStart = block.begin();
        QTextBlock::Iterator it = block.end();

        if (startPos == block.position()) {
            it = block.begin();
        } else {
            do {
                if (it == blockStart) {
                    it = QTextBlock::Iterator();
                    block = QTextBlock();
                } else {
                    --it;
                }
            } while (!it.atEnd() && it.fragment().position() + it.fragment().length() - 1 > startPos);
        }

        while (block.isValid()) {
            anchorStart = -1;

            if (!it.atEnd()) {
                do {
                    const QTextFragment fragment = it.fragment();
                    const QTextCharFormat fmt = fragment.charFormat();

                    if (fmt.isAnchor() && fmt.hasProperty(QTextFormat::AnchorHref)) {
                        anchorStart = fragment.position() + fragment.length();
                        anchorHref = fmt.anchorHref();
                        break;
                    }

                    if (it == blockStart)
                        it = QTextBlock::Iterator();
                    else
                        --it;
                } while (!it.atEnd());
            }

            if (anchorStart != -1 && !it.atEnd()) {
                anchorEnd = -1;

                do {
                    const QTextFragment fragment = it.fragment();
                    const QTextCharFormat fmt = fragment.charFormat();

                    if (!fmt.isAnchor() || fmt.anchorHref() != anchorHref) {
                        anchorEnd = fragment.position() + fragment.length();
                        break;
                    }

                    if (it == blockStart)
                        it = QTextBlock::Iterator();
                    else
                        --it;
                } while (!it.atEnd());

                if (anchorEnd == -1)
                    anchorEnd = qMax(0, block.position());

                break;
            }

            block = block.previous();
            it = block.end();
            if (it != block.begin())
                --it;
            blockStart = block.begin();
        }

    }

    if (anchorStart != -1 && anchorEnd != -1) {
        newAnchor = d->cursor;
        newAnchor.setPosition(anchorStart);
        newAnchor.setPosition(anchorEnd, QTextCursor::KeepAnchor);
        return true;
    }

    return false;
}

void WidgetTextControlPrivate::activateLinkUnderCursor(QString href)
{
    QTextCursor oldCursor = cursor;

    if (href.isEmpty()) {
        QTextCursor tmp = cursor;
        if (tmp.selectionStart() != tmp.position())
            tmp.setPosition(tmp.selectionStart());
        tmp.movePosition(QTextCursor::NextCharacter);
        href = tmp.charFormat().anchorHref();
    }
    if (href.isEmpty())
        return;

    if (!cursor.hasSelection()) {
        QTextBlock block = cursor.block();
        const int cursorPos = cursor.position();

        QTextBlock::Iterator it = block.begin();
        QTextBlock::Iterator linkFragment;

        for (; !it.atEnd(); ++it) {
            QTextFragment fragment = it.fragment();
            const int fragmentPos = fragment.position();
            if (fragmentPos <= cursorPos &&
                fragmentPos + fragment.length() > cursorPos) {
                linkFragment = it;
                break;
            }
        }

        if (!linkFragment.atEnd()) {
            it = linkFragment;
            cursor.setPosition(it.fragment().position());
            if (it != block.begin()) {
                do {
                    --it;
                    QTextFragment fragment = it.fragment();
                    if (fragment.charFormat().anchorHref() != href)
                        break;
                    cursor.setPosition(fragment.position());
                } while (it != block.begin());
            }

            for (it = linkFragment; !it.atEnd(); ++it) {
                QTextFragment fragment = it.fragment();
                if (fragment.charFormat().anchorHref() != href)
                    break;
                cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
            }
        }
    }

    if (hasFocus) {
        cursorIsFocusIndicator = true;
    } else {
        cursorIsFocusIndicator = false;
        cursor.clearSelection();
    }
    repaintOldAndNewSelection(oldCursor);

#ifndef QT_NO_DESKTOPSERVICES
    if (openExternalLinks)
        QDesktopServices::openUrl(href);
    else
#endif
        emit q->linkActivated(href);
}

#if QT_CONFIG(tooltip)
void WidgetTextControlPrivate::showToolTip(const QPoint &globalPos, const QPointF &pos, QWidget *contextWidget)
{
    const QString toolTip = q->cursorForPosition(pos).charFormat().toolTip();
    if (toolTip.isEmpty())
        return;
    QToolTip::showText(globalPos, toolTip, contextWidget);
}
#endif // QT_CONFIG(tooltip)

bool WidgetTextControlPrivate::isPreediting() const
{
    QTextLayout *layout = cursor.block().layout();
    if (layout && !layout->preeditAreaText().isEmpty())
        return true;

    return false;
}

void WidgetTextControlPrivate::commitPreedit()
{
    if (!isPreediting())
        return;

    QGuiApplication::inputMethod()->commit();

    if (!isPreediting())
        return;

    cursor.beginEditBlock();
    preeditCursor = 0;
    QTextBlock block = cursor.block();
    QTextLayout *layout = block.layout();
    layout->setPreeditArea(-1, QString());
    layout->clearFormats();
    cursor.endEditBlock();
}

bool WidgetTextControl::setFocusToNextOrPreviousAnchor(bool next)
{

    if (!(d->interactionFlags & Qt::LinksAccessibleByKeyboard))
        return false;

    QRectF crect = selectionRect();
    emit updateRequest(crect);

    // If we don't have a current anchor, we start from the start/end
    if (!d->cursor.hasSelection()) {
        d->cursor = QTextCursor(d->doc);
        if (next)
            d->cursor.movePosition(QTextCursor::Start);
        else
            d->cursor.movePosition(QTextCursor::End);
    }

    QTextCursor newAnchor;
    if (findNextPrevAnchor(d->cursor, next, newAnchor)) {
        d->cursor = newAnchor;
        d->cursorIsFocusIndicator = true;
    } else {
        d->cursor.clearSelection();
    }

    if (d->cursor.hasSelection()) {
        crect = selectionRect();
        emit updateRequest(crect);
        emit visibilityRequest(crect);
        return true;
    } else {
        return false;
    }
}

bool WidgetTextControl::setFocusToAnchor(const QTextCursor &newCursor)
{

    if (!(d->interactionFlags & Qt::LinksAccessibleByKeyboard))
        return false;

    // Verify that this is an anchor.
    const QString anchorHref = d->anchorForCursor(newCursor);
    if (anchorHref.isEmpty())
        return false;

    // and process it
    QRectF crect = selectionRect();
    emit updateRequest(crect);

    d->cursor.setPosition(newCursor.selectionStart());
    d->cursor.setPosition(newCursor.selectionEnd(), QTextCursor::KeepAnchor);
    d->cursorIsFocusIndicator = true;

    crect = selectionRect();
    emit updateRequest(crect);
    emit visibilityRequest(crect);
    return true;
}

void WidgetTextControl::setTextInteractionFlags(Qt::TextInteractionFlags flags)
{
    if (flags == d->interactionFlags)
        return;
    d->interactionFlags = flags;

    if (d->hasFocus)
        d->setCursorVisible(flags & Qt::TextEditable);
}

Qt::TextInteractionFlags WidgetTextControl::textInteractionFlags() const
{
    return d->interactionFlags;
}

void WidgetTextControl::mergeCurrentCharFormat(const QTextCharFormat &modifier)
{
    d->cursor.mergeCharFormat(modifier);
    d->updateCurrentCharFormat();
}

void WidgetTextControl::setCurrentCharFormat(const QTextCharFormat &format)
{
    d->cursor.setCharFormat(format);
    d->updateCurrentCharFormat();
}

QTextCharFormat WidgetTextControl::currentCharFormat() const
{
    return d->cursor.charFormat();
}

void WidgetTextControl::insertPlainText(const QString &text)
{
    d->cursor.insertText(text);
}

#ifndef QT_NO_TEXTHTMLPARSER
void WidgetTextControl::insertHtml(const QString &text)
{
    d->cursor.insertHtml(text);
}
#endif // QT_NO_TEXTHTMLPARSER

QPointF WidgetTextControl::anchorPosition(const QString &name) const
{
    if (name.isEmpty())
        return QPointF();

    QRectF r;
    for (QTextBlock block = d->doc->begin(); block.isValid(); block = block.next()) {
        QTextCharFormat format = block.charFormat();
        if (format.isAnchor() && format.anchorNames().contains(name)) {
            r = d->rectForPosition(block.position());
            break;
        }

        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            QTextFragment fragment = it.fragment();
            format = fragment.charFormat();
            if (format.isAnchor() && format.anchorNames().contains(name)) {
                r = d->rectForPosition(fragment.position());
                block = QTextBlock();
                break;
            }
        }
    }
    if (!r.isValid())
        return QPointF();
    return QPointF(0, r.top());
}

void WidgetTextControl::adjustSize()
{
    d->doc->adjustSize();
}

bool WidgetTextControl::find(const QString &exp, QTextDocument::FindFlags options)
{
    QTextCursor search = d->doc->find(exp, d->cursor, options);
    if (search.isNull())
        return false;

    setTextCursor(search);
    return true;
}

#if QT_CONFIG(regularexpression)
bool WidgetTextControl::find(const QRegularExpression &exp, QTextDocument::FindFlags options)
{
    QTextCursor search = d->doc->find(exp, d->cursor, options);
    if (search.isNull())
        return false;

    setTextCursor(search);
    return true;
}
#endif

QString WidgetTextControl::toPlainText() const
{
    return document()->toPlainText();
}

#ifndef QT_NO_TEXTHTMLPARSER
QString WidgetTextControl::toHtml() const
{
    return document()->toHtml();
}
#endif

#if QT_CONFIG(textmarkdownwriter)
QString WidgetTextControl::toMarkdown(QTextDocument::MarkdownFeatures features) const
{
    return document()->toMarkdown(features);
}
#endif

void WidgetTextControlPrivate::insertParagraphSeparator()
{
    // clear blockFormat properties that the user is unlikely to want duplicated:
    // - don't insert <hr/> automatically
    // - the next paragraph after a heading should be a normal paragraph
    // - remove the bottom margin from the last list item before appending
    // - the next checklist item after a checked item should be unchecked
    auto blockFmt = cursor.blockFormat();
    auto charFmt = cursor.charFormat();
    blockFmt.clearProperty(QTextFormat::BlockTrailingHorizontalRulerWidth);
    if (blockFmt.hasProperty(QTextFormat::HeadingLevel)) {
        blockFmt.clearProperty(QTextFormat::HeadingLevel);
        charFmt = QTextCharFormat();
    }
    if (cursor.currentList()) {
        auto existingFmt = cursor.blockFormat();
        existingFmt.clearProperty(QTextBlockFormat::BlockBottomMargin);
        cursor.setBlockFormat(existingFmt);
        if (blockFmt.marker() == QTextBlockFormat::MarkerType::Checked)
            blockFmt.setMarker(QTextBlockFormat::MarkerType::Unchecked);
    }

    // After a blank line, reset block and char formats. I.e. you can end a list,
    // block quote, etc. by hitting enter twice, and get back to normal paragraph style.
    if (cursor.block().text().isEmpty() &&
        !cursor.blockFormat().hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth) &&
        !cursor.blockFormat().hasProperty(QTextFormat::BlockCodeLanguage)) {
        blockFmt = QTextBlockFormat();
        const bool blockFmtChanged = (cursor.blockFormat() != blockFmt);
        charFmt = QTextCharFormat();
        cursor.setBlockFormat(blockFmt);
        cursor.setCharFormat(charFmt);
        // If the user hit enter twice just to get back to default format,
        // don't actually insert a new block. But if the user then hits enter
        // yet again, the block format will not change, so we will insert a block.
        // This is what many word processors do.
        if (blockFmtChanged)
            return;
    }

    cursor.insertBlock(blockFmt, charFmt);
}

void WidgetTextControlPrivate::append(const QString &text, Qt::TextFormat format)
{
    QTextCursor tmp(doc);
    tmp.beginEditBlock();
    tmp.movePosition(QTextCursor::End);

    if (!doc->isEmpty())
        tmp.insertBlock(cursor.blockFormat(), cursor.charFormat());
    else
        tmp.setCharFormat(cursor.charFormat());

    // preserve the char format
    QTextCharFormat oldCharFormat = cursor.charFormat();

#ifndef QT_NO_TEXTHTMLPARSER
    if (format == Qt::RichText || (format == Qt::AutoText && Qt::mightBeRichText(text))) {
        tmp.insertHtml(text);
    } else {
        tmp.insertText(text);
    }
#else
    Q_UNUSED(format);
    tmp.insertText(text);
#endif // QT_NO_TEXTHTMLPARSER
    if (!cursor.hasSelection())
        cursor.setCharFormat(oldCharFormat);

    tmp.endEditBlock();
}

void WidgetTextControl::append(const QString &text)
{
    d->append(text, Qt::AutoText);
}

void WidgetTextControl::appendHtml(const QString &html)
{
    d->append(html, Qt::RichText);
}

void WidgetTextControl::appendPlainText(const QString &text)
{
    d->append(text, Qt::PlainText);
}


void WidgetTextControl::ensureCursorVisible()
{
    QRectF crect = d->rectForPosition(d->cursor.position()).adjusted(-5, 0, 5, 0);
    emit visibilityRequest(crect);
    emit microFocusChanged();
}

QPalette WidgetTextControl::palette() const
{
    return d->palette;
}

void WidgetTextControl::setPalette(const QPalette &pal)
{
    d->palette = pal;
}

QAbstractTextDocumentLayout::PaintContext WidgetTextControl::getPaintContext(QWidget *widget) const
{

    QAbstractTextDocumentLayout::PaintContext ctx;

    ctx.selections = d->extraSelections;
    ctx.palette = d->palette;
// #if QT_CONFIG(style_stylesheet)
//     if (widget) {
//         if (auto cssStyle = qt_styleSheet(widget->style())) {
//             QStyleOption option;
//             option.initFrom(widget);
//             cssStyle->styleSheetPalette(widget, &option, &ctx.palette);
//         }
//     }
// #endif // style_stylesheet
    if (d->cursorOn && d->isEnabled) {
        if (d->hideCursor)
            ctx.cursorPosition = -1;
        else if (d->preeditCursor != 0)
            ctx.cursorPosition = - (d->preeditCursor + 2);
        else
            ctx.cursorPosition = d->cursor.position();
    }

    if (!d->dndFeedbackCursor.isNull())
        ctx.cursorPosition = d->dndFeedbackCursor.position();
#ifdef QT_KEYPAD_NAVIGATION
    if (!QApplicationPrivate::keypadNavigationEnabled() || d->hasEditFocus)
#endif
        if (d->cursor.hasSelection()) {
            QAbstractTextDocumentLayout::Selection selection;
            selection.cursor = d->cursor;
            if (d->cursorIsFocusIndicator) {
                QStyleOption opt;
                opt.palette = ctx.palette;
                QStyleHintReturnVariant ret;
                QStyle *style = QApplication::style();
                if (widget)
                    style = widget->style();
                style->styleHint(QStyle::SH_TextControl_FocusIndicatorTextCharFormat, &opt, widget, &ret);
                selection.format = qvariant_cast<QTextFormat>(ret.variant).toCharFormat();
            } else {
                QPalette::ColorGroup cg = d->hasFocus ? QPalette::Active : QPalette::Inactive;
                selection.format.setBackground(ctx.palette.brush(cg, QPalette::Highlight));
                selection.format.setForeground(ctx.palette.brush(cg, QPalette::HighlightedText));
                QStyleOption opt;
                QStyle *style = QApplication::style();
                if (widget) {
                    opt.initFrom(widget);
                    style = widget->style();
                }
                if (style->styleHint(QStyle::SH_RichText_FullWidthSelection, &opt, widget))
                    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
            }
            ctx.selections.append(selection);
        }

    return ctx;
}

void WidgetTextControl::drawContents(QPainter *p, const QRectF &rect, QWidget *widget)
{
    p->save();
    QAbstractTextDocumentLayout::PaintContext ctx = getPaintContext(widget);
    if (rect.isValid())
        p->setClipRect(rect, Qt::IntersectClip);
    ctx.clip = rect;

    d->doc->documentLayout()->draw(p, ctx);
    p->restore();
}

void WidgetTextControlPrivate::_q_copyLink()
{
#ifndef QT_NO_CLIPBOARD
    QMimeData *md = new QMimeData;
    md->setText(linkToCopy);
    QGuiApplication::clipboard()->setMimeData(md);
#endif
}

int WidgetTextControl::hitTest(const QPointF &point, Qt::HitTestAccuracy accuracy) const
{
    return d->doc->documentLayout()->hitTest(point, accuracy);
}

QRectF WidgetTextControl::blockBoundingRect(const QTextBlock &block) const
{
    return d->doc->documentLayout()->blockBoundingRect(block);
}

#ifndef QT_NO_CONTEXTMENU
#define NUM_CONTROL_CHARACTERS 14
const struct QUnicodeControlCharacter {
    const char *text;
    ushort character;
} qt_controlCharacters[NUM_CONTROL_CHARACTERS] = {
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "LRM Left-to-right mark"), 0x200e },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "RLM Right-to-left mark"), 0x200f },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "ZWJ Zero width joiner"), 0x200d },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "ZWNJ Zero width non-joiner"), 0x200c },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "ZWSP Zero width space"), 0x200b },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "LRE Start of left-to-right embedding"), 0x202a },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "RLE Start of right-to-left embedding"), 0x202b },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "LRO Start of left-to-right override"), 0x202d },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "RLO Start of right-to-left override"), 0x202e },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "PDF Pop directional formatting"), 0x202c },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "LRI Left-to-right isolate"), 0x2066 },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "RLI Right-to-left isolate"), 0x2067 },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "FSI First strong isolate"), 0x2068 },
    { QT_TRANSLATE_NOOP("QUnicodeControlCharacterMenu", "PDI Pop directional isolate"), 0x2069 }
};

UnicodeControlCharacterMenu::UnicodeControlCharacterMenu(QObject *_editWidget, QWidget *parent)
    : QMenu(parent), editWidget(_editWidget)
{
    setTitle(tr("Insert Unicode control character"));
    for (int i = 0; i < NUM_CONTROL_CHARACTERS; ++i) {
        addAction(tr(qt_controlCharacters[i].text), this, SLOT(menuActionTriggered()));
    }
}

void UnicodeControlCharacterMenu::menuActionTriggered()
{
    QAction *a = qobject_cast<QAction *>(sender());
    int idx = actions().indexOf(a);
    if (idx < 0 || idx >= NUM_CONTROL_CHARACTERS)
        return;
    QChar c(qt_controlCharacters[idx].character);
    QString str(c);

#if QT_CONFIG(textedit)
    if (QTextEdit *edit = qobject_cast<QTextEdit *>(editWidget)) {
        edit->insertPlainText(str);
        return;
    }
#endif
    if (WidgetTextControl *control = qobject_cast<WidgetTextControl *>(editWidget)) {
        control->insertPlainText(str);
    }
#if QT_CONFIG(lineedit)
    if (QLineEdit *edit = qobject_cast<QLineEdit *>(editWidget)) {
        edit->insert(str);
        return;
    }
#endif
}
#endif // QT_NO_CONTEXTMENU

static QStringList supportedMimeTypes()
{
    static const QStringList mimeTypes{
        "text/plain",
        "text/html"
#if QT_CONFIG(textmarkdownwriter)
        , "text/markdown"
#endif
#if QT_CONFIG(textodfwriter)
        , "application/vnd.oasis.opendocument.text"
#endif
    };
    return mimeTypes;
}

/*! \internal
    \reimp
*/
QStringList TextEditMimeData::formats() const
{
    if (!fragment.isEmpty())
        return supportedMimeTypes();

    return QMimeData::formats();
}

/*! \internal
    \reimp
*/
bool TextEditMimeData::hasFormat(const QString &format) const
{
    if (!fragment.isEmpty())
        return supportedMimeTypes().contains(format);

    return QMimeData::hasFormat(format);
}

QVariant TextEditMimeData::retrieveData(const QString &mimeType, QMetaType type) const
{
    if (!fragment.isEmpty())
        setup();
    return QMimeData::retrieveData(mimeType, type);
}

void TextEditMimeData::setup() const
{
    TextEditMimeData *that = const_cast<TextEditMimeData *>(this);
#ifndef QT_NO_TEXTHTMLPARSER
    that->setData("text/html", fragment.toHtml().toUtf8());
#endif
#if QT_CONFIG(textmarkdownwriter)
    that->setData("text/markdown", fragment.toMarkdown().toUtf8());
#endif
#ifndef QT_NO_TEXTODFWRITER
    {
        QBuffer buffer;
        QTextDocumentWriter writer(&buffer, "ODF");
        writer.write(fragment);
        buffer.close();
        that->setData("application/vnd.oasis.opendocument.text", buffer.data());
    }
#endif
    that->setText(fragment.toPlainText());
    fragment = QTextDocumentFragment();
}

class PlainTextEditControl : public WidgetTextControl
{
    Q_OBJECT
public:
    PlainTextEditControl(PlainTextEdit *parent);

    QMimeData *createMimeDataFromSelection() const override;
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
    int hitTest(const QPointF &point, Qt::HitTestAccuracy = Qt::FuzzyHit) const override;
    QRectF blockBoundingRect(const QTextBlock &block) const override;
    inline QRectF cursorRect(const QTextCursor &cursor) const {
        QRectF r = WidgetTextControl::cursorRect(cursor);
        r.setLeft(qMax(r.left(), (qreal) 0.));
        return r;
    }
    inline QRectF cursorRect() { return cursorRect(textCursor()); }
    void ensureCursorVisible() override {
        textEdit->ensureCursorVisible();
        emit microFocusChanged();
    }

    PlainTextEdit *textEdit;
    int topBlock;
    QTextBlock firstVisibleBlock() const;

    QVariant loadResource(int type, const QUrl &name) override {
        return textEdit->loadResource(type, name);
    }
};


class PlainTextEditPrivate : public QObject
{
    Q_OBJECT
public:
    PlainTextEditPrivate(PlainTextEdit *qq);

    QWidget *viewport() const { return q->viewport(); }
    QScrollBar *hbar() { return q->horizontalScrollBar(); }
    const QScrollBar *hbar() const { return q->horizontalScrollBar(); }
    QScrollBar *vbar() { return q->verticalScrollBar(); }
    const QScrollBar *vbar() const { return q->verticalScrollBar(); }

    void init(const QString &txt = QString());
    void repaintContents(const QRectF &contentsRect);
    void updatePlaceholderVisibility();

    inline QPoint mapToContents(const QPoint &point) const
    { return QPoint(point.x() + horizontalOffset(), point.y() + verticalOffset()); }

    void adjustScrollbars();
    void verticalScrollbarActionTriggered(int action);
    void ensureViewportLayouted();
    void relayoutDocument();

    void pageUpDown(QTextCursor::MoveOperation op, QTextCursor::MoveMode moveMode, bool moveCursor = true);

    inline int horizontalOffset() const
    { return (q->isRightToLeft() ? (hbar()->maximum() - hbar()->value()) : hbar()->value()); }
    qreal verticalOffset(int topBlock, int topLine) const;
    qreal verticalOffset() const;

    inline void sendControlEvent(QEvent *e)
    { control->processEvent(e, QPointF(horizontalOffset(), verticalOffset()), viewport()); }

    void updateDefaultTextOption();

    PlainTextEdit *q = nullptr;
    QBasicTimer autoScrollTimer;
#ifdef QT_KEYPAD_NAVIGATION
    QBasicTimer deleteAllTimer;
#endif
    QPoint autoScrollDragPos;
    QString placeholderText;

    PlainTextEditControl *control = nullptr;
    qreal topLineFracture = 0; // for non-int sized fonts
    qreal pageUpDownLastCursorY = 0;
    PlainTextEdit::LineWrapMode lineWrap = PlainTextEdit::WidgetWidth;
    QTextOption::WrapMode wordWrap = QTextOption::WrapAtWordBoundaryOrAnywhere;
    int originalOffsetY = 0;
    int topLine = 0;

    uint tabChangesFocus : 1;
    uint showCursorOnInitialShow : 1;
    uint backgroundVisible : 1;
    uint centerOnScroll : 1;
    uint inDrag : 1;
    uint clickCausedFocus : 1;
    uint pageUpDownLastCursorYIsValid : 1;
    uint placeholderTextShown : 1;

    void setTopLine(int visualTopLine, int dx = 0);
    void setTopBlock(int newTopBlock, int newTopLine, int dx = 0);

    void ensureVisible(int position, bool center, bool forceCenter = false);
    void ensureCursorVisible(bool center = false);
    void updateViewport();

    QPointer<PlainTextDocumentLayout> documentLayoutPtr;

    void append(const QString &text, Qt::TextFormat format = Qt::AutoText);

    void cursorPositionChanged();
    void modificationChanged(bool);
    inline bool placeHolderTextToBeShown() const
    {
        return q->document()->isEmpty() && !q->placeholderText().isEmpty();
    }
    inline void handleSoftwareInputPanel(Qt::MouseButton button, bool clickCausedFocus)
    {
        if (button == Qt::LeftButton)
            handleSoftwareInputPanel(clickCausedFocus);
    }

    inline void handleSoftwareInputPanel(bool clickCausedFocus = false)
    {
        if (qApp->autoSipEnabled()) {
            QStyle::RequestSoftwareInputPanel behavior = QStyle::RequestSoftwareInputPanel(
                q->style()->styleHint(QStyle::SH_RequestSoftwareInputPanel));
            if (!clickCausedFocus || behavior == QStyle::RSIP_OnMouseClick) {
                QGuiApplication::inputMethod()->show();
            }
        }
    }

};

static inline bool shouldEnableInputMethod(PlainTextEdit *control)
{
#if defined(Q_OS_ANDROID)
    Q_UNUSED(control);
    return !control->isReadOnly() || (control->textInteractionFlags() & Qt::TextSelectableByMouse);
#else
    return !control->isReadOnly();
#endif
}

class PlainTextDocumentLayoutPrivate
{
public:
    PlainTextDocumentLayoutPrivate(PlainTextDocumentLayout *qq) {
        q = qq;
        mainViewPrivate = nullptr;
        width = 0;
        maximumWidth = 0;
        maximumWidthBlockNumber = 0;
        blockCount = 1;
        blockUpdate = blockDocumentSizeChanged = false;
        cursorWidth = 1;
        textLayoutFlags = 0;
    }

    PlainTextDocumentLayout *q;

    qreal width;
    qreal maximumWidth;
    int maximumWidthBlockNumber;
    int blockCount;
    PlainTextEditPrivate *mainViewPrivate;
    bool blockUpdate;
    bool blockDocumentSizeChanged;
    int cursorWidth;
    int textLayoutFlags;

    void layoutBlock(const QTextBlock &block);
    qreal blockWidth(const QTextBlock &block);

    void relayout();
};



/*! \class PlainTextDocumentLayout
    \since 4.4
    \brief The PlainTextDocumentLayout class implements a plain text layout for QTextDocument.

    \ingroup richtext-processing
    \inmodule QtWidgets

   A PlainTextDocumentLayout is required for text documents that can
   be display or edited in a PlainTextEdit. See
   QTextDocument::setDocumentLayout().

   PlainTextDocumentLayout uses the QAbstractTextDocumentLayout API
   that QTextDocument requires, but redefines it partially in order to
   support plain text better. For instances, it does not operate on
   vertical pixels, but on paragraphs (called blocks) instead. The
   height of a document is identical to the number of paragraphs it
   contains. The layout also doesn't support tables or nested frames,
   or any sort of advanced text layout that goes beyond a list of
   paragraphs with syntax highlighting.

*/



/*!
  Constructs a plain text document layout for the text \a document.
 */
PlainTextDocumentLayout::PlainTextDocumentLayout(QTextDocument *document)
    : QAbstractTextDocumentLayout(document)
    , d(std::make_unique<PlainTextDocumentLayoutPrivate>(this))
{
}
/*!
  Destructs a plain text document layout.
 */
PlainTextDocumentLayout::~PlainTextDocumentLayout() {}


/*!
  \reimp
 */
void PlainTextDocumentLayout::draw(QPainter *, const PaintContext &)
{
}

/*!
  \reimp
 */
int PlainTextDocumentLayout::hitTest(const QPointF &, Qt::HitTestAccuracy ) const
{
    //     this function is used from
    //     QAbstractTextDocumentLayout::anchorAt(), but is not
    //     implementable in a plain text document layout, because the
    //     layout depends on the top block and top line which depends on
    //     the view
    return -1;
}

/*!
  \reimp
 */
int PlainTextDocumentLayout::pageCount() const
{ return 1; }

/*!
  \reimp
 */
QSizeF PlainTextDocumentLayout::documentSize() const
{
    return QSizeF(d->maximumWidth, document()->lineCount());
}

/*!
  \reimp
 */
QRectF PlainTextDocumentLayout::frameBoundingRect(QTextFrame *) const
{
    return QRectF(0, 0, qMax(d->width, d->maximumWidth), qreal(INT_MAX));
}

/*!
  \reimp
 */
QRectF PlainTextDocumentLayout::blockBoundingRect(const QTextBlock &block) const
{
    if (!block.isValid()) { return QRectF(); }
    QTextLayout *tl = block.layout();
    if (!tl->lineCount())
        const_cast<PlainTextDocumentLayout*>(this)->layoutBlock(block);
    QRectF br;
    if (block.isVisible()) {
        br = QRectF(QPointF(0, 0), tl->boundingRect().bottomRight());
        if (tl->lineCount() == 1)
            br.setWidth(qMax(br.width(), tl->lineAt(0).naturalTextWidth()));
        qreal margin = document()->documentMargin();
        br.adjust(0, 0, margin, 0);
        if (!block.next().isValid())
            br.adjust(0, 0, 0, margin);
    }
    return br;

}

/*!
  Ensures that \a block has a valid layout
 */
void PlainTextDocumentLayout::ensureBlockLayout(const QTextBlock &block) const
{
    if (!block.isValid())
        return;
    QTextLayout *tl = block.layout();
    if (!tl->lineCount())
        const_cast<PlainTextDocumentLayout*>(this)->layoutBlock(block);
}


/*! \property PlainTextDocumentLayout::cursorWidth

    This property specifies the width of the cursor in pixels. The default value is 1.
*/
void PlainTextDocumentLayout::setCursorWidth(int width)
{
    d->cursorWidth = width;
}

int PlainTextDocumentLayout::cursorWidth() const
{
    return d->cursorWidth;
}

/*!

   Requests a complete update on all views.
 */
void PlainTextDocumentLayout::requestUpdate()
{
    emit update(QRectF(0., -document()->documentMargin(), 1000000000., 1000000000.));
}


void PlainTextDocumentLayout::setTextWidth(qreal newWidth)
{
    d->width = d->maximumWidth = newWidth;
    d->relayout();
}

qreal PlainTextDocumentLayout::textWidth() const
{
    return d->width;
}

void PlainTextDocumentLayoutPrivate::relayout()
{
    QTextBlock block = q->document()->firstBlock();
    while (block.isValid()) {
        block.layout()->clearLayout();
        block.setLineCount(block.isVisible() ? 1 : 0);
        block = block.next();
    }
    emit q->update();
}


/*! \reimp
 */
void PlainTextDocumentLayout::documentChanged(int from, int charsRemoved, int charsAdded)
{
    QTextDocument *doc = document();
    int newBlockCount = doc->blockCount();
    int charsChanged = charsRemoved + charsAdded;

    QTextBlock changeStartBlock = doc->findBlock(from);
    QTextBlock changeEndBlock = doc->findBlock(qMax(0, from + charsChanged - 1));
    bool blockVisibilityChanged = false;

    if (changeStartBlock == changeEndBlock && newBlockCount == d->blockCount) {
        QTextBlock block = changeStartBlock;
        if (block.isValid() && block.length()) {
            QRectF oldBr = blockBoundingRect(block);
            layoutBlock(block);
            QRectF newBr = blockBoundingRect(block);
            if (newBr.height() == oldBr.height()) {
                if (!d->blockUpdate)
                    emit updateBlock(block);
                return;
            }
        }
    } else {
        QTextBlock block = changeStartBlock;
        do {
            block.clearLayout();
            if (block.isVisible()
                    ? (block.lineCount() == 0)
                    : (block.lineCount() > 0)) {
                blockVisibilityChanged = true;
                block.setLineCount(block.isVisible() ? 1 : 0);
            }
            if (block == changeEndBlock)
                break;
            block = block.next();
        } while(block.isValid());
    }

    if (newBlockCount != d->blockCount || blockVisibilityChanged) {
        int changeEnd = changeEndBlock.blockNumber();
        int blockDiff = newBlockCount - d->blockCount;
        int oldChangeEnd = changeEnd - blockDiff;

        if (d->maximumWidthBlockNumber > oldChangeEnd)
            d->maximumWidthBlockNumber += blockDiff;

        d->blockCount = newBlockCount;
        if (d->blockCount == 1)
            d->maximumWidth = blockWidth(doc->firstBlock());

        if (!d->blockDocumentSizeChanged)
            emit documentSizeChanged(documentSize());

        if (blockDiff == 1 && changeEnd == newBlockCount -1 ) {
            if (!d->blockUpdate) {
                QTextBlock b = changeStartBlock;
                for(;;) {
                    emit updateBlock(b);
                    if (b == changeEndBlock)
                        break;
                    b = b.next();
                }
            }
            return;
        }
    }

    if (!d->blockUpdate)
        emit update(QRectF(0., -doc->documentMargin(), 1000000000., 1000000000.)); // optimization potential
}


void PlainTextDocumentLayout::layoutBlock(const QTextBlock &block)
{
    QTextDocument *doc = document();
    qreal margin = doc->documentMargin();
    qreal blockMaximumWidth = 0;

    qreal height = 0;
    QTextLayout *tl = block.layout();
    QTextOption option = doc->defaultTextOption();
    tl->setTextOption(option);

    int extraMargin = 0;
    if (option.flags() & QTextOption::AddSpaceForLineAndParagraphSeparators) {
        QFontMetrics fm(block.charFormat().font());
        extraMargin += fm.horizontalAdvance(QChar(0x21B5));
    }
    tl->beginLayout();
    qreal availableWidth = d->width;
    if (availableWidth <= 0) {
        availableWidth = qreal(INT_MAX); // similar to text edit with pageSize.width == 0
    }
    availableWidth -= 2*margin + extraMargin;
    while (1) {
        QTextLine line = tl->createLine();
        if (!line.isValid())
            break;
        line.setLeadingIncluded(true);
        line.setLineWidth(availableWidth);
        line.setPosition(QPointF(margin, height));
        height += line.height();
        if (line.leading() < 0)
            height += qCeil(line.leading());
        blockMaximumWidth = qMax(blockMaximumWidth, line.naturalTextWidth() + 2*margin);
    }
    tl->endLayout();

    int previousLineCount = doc->lineCount();
    const_cast<QTextBlock&>(block).setLineCount(block.isVisible() ? tl->lineCount() : 0);
    int lineCount = doc->lineCount();

    bool emitDocumentSizeChanged = previousLineCount != lineCount;
    if (blockMaximumWidth > d->maximumWidth) {
        // new longest line
        d->maximumWidth = blockMaximumWidth;
        d->maximumWidthBlockNumber = block.blockNumber();
        emitDocumentSizeChanged = true;
    } else if (block.blockNumber() == d->maximumWidthBlockNumber && blockMaximumWidth < d->maximumWidth) {
        // longest line shrinking
        QTextBlock b = doc->firstBlock();
        d->maximumWidth = 0;
        QTextBlock maximumBlock;
        while (b.isValid()) {
            qreal blockMaximumWidth = blockWidth(b);
            if (blockMaximumWidth > d->maximumWidth) {
                d->maximumWidth = blockMaximumWidth;
                maximumBlock = b;
            }
            b = b.next();
        }
        if (maximumBlock.isValid()) {
            d->maximumWidthBlockNumber = maximumBlock.blockNumber();
            emitDocumentSizeChanged = true;
        }
    }
    if (emitDocumentSizeChanged && !d->blockDocumentSizeChanged)
        emit documentSizeChanged(documentSize());
}

qreal PlainTextDocumentLayout::blockWidth(const QTextBlock &block)
{
    QTextLayout *layout = block.layout();
    if (!layout->lineCount())
        return 0; // only for layouted blocks
    qreal blockWidth = 0;
    for (int i = 0; i < layout->lineCount(); ++i) {
        QTextLine line = layout->lineAt(i);
        blockWidth = qMax(line.naturalTextWidth() + 8, blockWidth);
    }
    return blockWidth;
}


PlainTextEditControl::PlainTextEditControl(PlainTextEdit *parent)
    : WidgetTextControl(parent), textEdit(parent),
    topBlock(0)
{
    setAcceptRichText(false);
}

void PlainTextEditPrivate::cursorPositionChanged()
{
    pageUpDownLastCursorYIsValid = false;
#if QT_CONFIG(accessibility)
    QAccessibleTextCursorEvent ev(q, q->textCursor().position());
    QAccessible::updateAccessibility(&ev);
#endif
    emit q->cursorPositionChanged();
}

void PlainTextEditPrivate::verticalScrollbarActionTriggered(int action) {

    const auto a = static_cast<QAbstractSlider::SliderAction>(action);
    switch (a) {
    case QAbstractSlider::SliderPageStepAdd:
        pageUpDown(QTextCursor::Down, QTextCursor::MoveAnchor, false);
        break;
    case QAbstractSlider::SliderPageStepSub:
        pageUpDown(QTextCursor::Up, QTextCursor::MoveAnchor, false);
        break;
    default:
        break;
    }
}

QMimeData *PlainTextEditControl::createMimeDataFromSelection() const {
    PlainTextEdit *ed = qobject_cast<PlainTextEdit *>(parent());
    if (!ed)
        return WidgetTextControl::createMimeDataFromSelection();
    return ed->createMimeDataFromSelection();
}
bool PlainTextEditControl::canInsertFromMimeData(const QMimeData *source) const {
    PlainTextEdit *ed = qobject_cast<PlainTextEdit *>(parent());
    if (!ed)
        return WidgetTextControl::canInsertFromMimeData(source);
    return ed->canInsertFromMimeData(source);
}
void PlainTextEditControl::insertFromMimeData(const QMimeData *source) {
    PlainTextEdit *ed = qobject_cast<PlainTextEdit *>(parent());
    if (!ed)
        WidgetTextControl::insertFromMimeData(source);
    else
        ed->insertFromMimeData(source);
}

qreal PlainTextEditPrivate::verticalOffset(int topBlock, int topLine) const
{
    qreal offset = 0;
    QTextDocument *doc = control->document();

    if (topLine) {
        QTextBlock currentBlock = doc->findBlockByNumber(topBlock);
        PlainTextDocumentLayout *documentLayout = qobject_cast<PlainTextDocumentLayout*>(doc->documentLayout());
        Q_ASSERT(documentLayout);
        QRectF r = documentLayout->blockBoundingRect(currentBlock);
        Q_UNUSED(r);
        QTextLayout *layout = currentBlock.layout();
        if (layout && topLine <= layout->lineCount()) {
            QTextLine line = layout->lineAt(topLine - 1);
            const QRectF lr = line.naturalTextRect();
            offset = lr.bottom();
        }
    }
    if (topBlock == 0 && topLine == 0)
        offset -= doc->documentMargin(); // top margin
    return offset;
}


qreal PlainTextEditPrivate::verticalOffset() const {
    return verticalOffset(control->topBlock, topLine) + topLineFracture;
}


QTextBlock PlainTextEditControl::firstVisibleBlock() const
{
    return document()->findBlockByNumber(topBlock);
}



int PlainTextEditControl::hitTest(const QPointF &point, Qt::HitTestAccuracy ) const {
    int currentBlockNumber = topBlock;
    QTextBlock currentBlock = document()->findBlockByNumber(currentBlockNumber);
    if (!currentBlock.isValid())
        return -1;

    PlainTextDocumentLayout *documentLayout = qobject_cast<PlainTextDocumentLayout*>(document()->documentLayout());
    Q_ASSERT(documentLayout);

    QPointF offset;
    QRectF r = documentLayout->blockBoundingRect(currentBlock);
    while (currentBlock.next().isValid() && r.bottom() + offset.y() <= point.y()) {
        offset.ry() += r.height();
        currentBlock = currentBlock.next();
        ++currentBlockNumber;
        r = documentLayout->blockBoundingRect(currentBlock);
    }
    while (currentBlock.previous().isValid() && r.top() + offset.y() > point.y()) {
        offset.ry() -= r.height();
        currentBlock = currentBlock.previous();
        --currentBlockNumber;
        r = documentLayout->blockBoundingRect(currentBlock);
    }


    if (!currentBlock.isValid())
        return -1;
    QTextLayout *layout = currentBlock.layout();
    int off = 0;
    QPointF pos = point - offset;
    for (int i = 0; i < layout->lineCount(); ++i) {
        QTextLine line = layout->lineAt(i);
        const QRectF lr = line.naturalTextRect();
        if (lr.top() > pos.y()) {
            off = qMin(off, line.textStart());
        } else if (lr.bottom() <= pos.y()) {
            off = qMax(off, line.textStart() + line.textLength());
        } else {
            off = line.xToCursor(pos.x(), overwriteMode() ?
                                              QTextLine::CursorOnCharacter : QTextLine::CursorBetweenCharacters);
            break;
        }
    }

    return currentBlock.position() + off;
}

QRectF PlainTextEditControl::blockBoundingRect(const QTextBlock &block) const {
    int currentBlockNumber = topBlock;
    int blockNumber = block.blockNumber();
    QTextBlock currentBlock = document()->findBlockByNumber(currentBlockNumber);
    if (!currentBlock.isValid())
        return QRectF();
    Q_ASSERT(currentBlock.blockNumber() == currentBlockNumber);
    QTextDocument *doc = document();
    PlainTextDocumentLayout *documentLayout = qobject_cast<PlainTextDocumentLayout*>(doc->documentLayout());
    Q_ASSERT(documentLayout);

    QPointF offset;
    if (!block.isValid())
        return QRectF();
    QRectF r = documentLayout->blockBoundingRect(currentBlock);
    int maxVerticalOffset = r.height();
    while (currentBlockNumber < blockNumber && offset.y() - maxVerticalOffset <= 2* textEdit->viewport()->height()) {
        offset.ry() += r.height();
        currentBlock = currentBlock.next();
        ++currentBlockNumber;
        if (!currentBlock.isVisible()) {
            currentBlock = doc->findBlockByLineNumber(currentBlock.firstLineNumber());
            currentBlockNumber = currentBlock.blockNumber();
        }
        r = documentLayout->blockBoundingRect(currentBlock);
    }
    while (currentBlockNumber > blockNumber && offset.y() + maxVerticalOffset >= -textEdit->viewport()->height()) {
        currentBlock = currentBlock.previous();
        --currentBlockNumber;
        while (!currentBlock.isVisible()) {
            currentBlock = currentBlock.previous();
            --currentBlockNumber;
        }
        if (!currentBlock.isValid())
            break;

        r = documentLayout->blockBoundingRect(currentBlock);
        offset.ry() -= r.height();
    }

    if (currentBlockNumber != blockNumber) {
        // fallback for blocks out of reach. Give it some geometry at
        // least, and ensure the layout is up to date.
        r = documentLayout->blockBoundingRect(block);
        if (currentBlockNumber > blockNumber)
            offset.ry() -= r.height();
    }
    r.translate(offset);
    return r;
}

void PlainTextEditPrivate::setTopLine(int visualTopLine, int dx)
{
    QTextDocument *doc = control->document();
    QTextBlock block = doc->findBlockByLineNumber(visualTopLine);
    int blockNumber = block.blockNumber();
    int lineNumber = visualTopLine - block.firstLineNumber();
    setTopBlock(blockNumber, lineNumber, dx);
}

void PlainTextEditPrivate::setTopBlock(int blockNumber, int lineNumber, int dx)
{
    blockNumber = qMax(0, blockNumber);
    lineNumber = qMax(0, lineNumber);
    QTextDocument *doc = control->document();
    QTextBlock block = doc->findBlockByNumber(blockNumber);

    int newTopLine = block.firstLineNumber() + lineNumber;
    int maxTopLine = vbar()->maximum();

    if (newTopLine > maxTopLine) {
        block = doc->findBlockByLineNumber(maxTopLine);
        blockNumber = block.blockNumber();
        lineNumber = maxTopLine - block.firstLineNumber();
    }

    vbar()->setValue(newTopLine);

    if (!dx && blockNumber == control->topBlock && lineNumber == topLine)
        return;

    if (viewport()->updatesEnabled() && viewport()->isVisible()) {
        int dy = 0;
        if (doc->findBlockByNumber(control->topBlock).isValid()) {
            qreal realdy = -q->blockBoundingGeometry(block).y()
            + verticalOffset() - verticalOffset(blockNumber, lineNumber);
            dy = (int)realdy;
            topLineFracture = realdy - dy;
        }
        control->topBlock = blockNumber;
        topLine = lineNumber;

        vbar()->setValue(block.firstLineNumber() + lineNumber);

        if (dx || dy) {
            viewport()->scroll(q->isRightToLeft() ? -dx : dx, dy);
            QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle | Qt::ImAnchorRectangle);
        } else {
            viewport()->update();
            topLineFracture = 0;
        }
        emit q->updateRequest(viewport()->rect(), dy);
    } else {
        control->topBlock = blockNumber;
        topLine = lineNumber;
        topLineFracture = 0;
    }

}



void PlainTextEditPrivate::ensureVisible(int position, bool center, bool forceCenter) {
    QRectF visible = QRectF(viewport()->rect()).translated(-q->contentOffset());
    QTextBlock block = control->document()->findBlock(position);
    if (!block.isValid())
        return;
    QRectF br = control->blockBoundingRect(block);
    if (!br.isValid())
        return;
    QTextLine line = block.layout()->lineForTextPosition(position - block.position());
    Q_ASSERT(line.isValid());
    QRectF lr = line.naturalTextRect().translated(br.topLeft());

    if (lr.bottom() >= visible.bottom() || (center && lr.top() < visible.top()) || forceCenter){

        qreal height = visible.height();
        if (center)
            height /= 2;

        qreal h = center ? line.naturalTextRect().center().y() : line.naturalTextRect().bottom();

        QTextBlock previousVisibleBlock = block;
        while (h < height && block.previous().isValid()) {
            previousVisibleBlock = block;
            do {
                block = block.previous();
            } while (!block.isVisible() && block.previous().isValid());
            h += q->blockBoundingRect(block).height();
        }

        int l = 0;
        int lineCount = block.layout()->lineCount();
        qreal voffset = verticalOffset(block.blockNumber(), 0);
        while (l < lineCount) {
            QRectF lineRect = block.layout()->lineAt(l).naturalTextRect();
            if (h - voffset - lineRect.top() <= height)
                break;
            ++l;
        }

        if (l >= lineCount) {
            block = previousVisibleBlock;
            l = 0;
        }
        setTopBlock(block.blockNumber(), l);
    } else if (lr.top() < visible.top()) {
        setTopBlock(block.blockNumber(), line.lineNumber());
    }

}


void PlainTextEditPrivate::updateViewport()
{
    viewport()->update();
    emit q->updateRequest(viewport()->rect(), 0);
}

PlainTextEditPrivate::PlainTextEditPrivate(PlainTextEdit *qq)
    : q(qq)
    , tabChangesFocus(false)
    , showCursorOnInitialShow(false)
    , backgroundVisible(false)
    , centerOnScroll(false)
    , inDrag(false)
    , clickCausedFocus(false)
    , pageUpDownLastCursorYIsValid(false)
    , placeholderTextShown(false)
{
}

void PlainTextEditPrivate::init(const QString &txt)
{
    control = new PlainTextEditControl(q);

    QTextDocument *doc = new QTextDocument(control);
    QAbstractTextDocumentLayout *layout = new PlainTextDocumentLayout(doc);
    doc->setDocumentLayout(layout);
    control->setDocument(doc);

    control->setPalette(q->palette());

    QObject::connect(vbar(), &QAbstractSlider::actionTriggered,
                     this, &PlainTextEditPrivate::verticalScrollbarActionTriggered);
    QObject::connect(control, &WidgetTextControl::microFocusChanged, q,
                     [this](){q->updateMicroFocus(); });
    QObject::connect(control, &WidgetTextControl::documentSizeChanged,
                     this, &PlainTextEditPrivate::adjustScrollbars);
    QObject::connect(control, &WidgetTextControl::blockCountChanged,
                     q, &PlainTextEdit::blockCountChanged);
    QObject::connect(control, &WidgetTextControl::updateRequest,
                     this, &PlainTextEditPrivate::repaintContents);
    QObject::connect(control, &WidgetTextControl::modificationChanged,
                     q, &PlainTextEdit::modificationChanged);
    QObject::connect(control, &WidgetTextControl::textChanged, q, &PlainTextEdit::textChanged);
    QObject::connect(control, &WidgetTextControl::undoAvailable, q, &PlainTextEdit::undoAvailable);
    QObject::connect(control, &WidgetTextControl::redoAvailable, q, &PlainTextEdit::redoAvailable);
    QObject::connect(control, &WidgetTextControl::copyAvailable, q, &PlainTextEdit::copyAvailable);
    QObject::connect(control, &WidgetTextControl::selectionChanged, q, &PlainTextEdit::selectionChanged);
    QObject::connect(control, &WidgetTextControl::cursorPositionChanged,
                     this, &PlainTextEditPrivate::cursorPositionChanged);
    QObject::connect(control, &WidgetTextControl::textChanged,
                     this, &PlainTextEditPrivate::updatePlaceholderVisibility);
    QObject::connect(control, &WidgetTextControl::textChanged, q, [this](){q->updateMicroFocus(); });

    // set a null page size initially to avoid any relayouting until the textedit
    // is shown. relayoutDocument() will take care of setting the page size to the
    // viewport dimensions later.
    doc->setTextWidth(-1);
    doc->documentLayout()->setPaintDevice(viewport());
    doc->setDefaultFont(q->font());


    if (!txt.isEmpty())
        control->setPlainText(txt);

    hbar()->setSingleStep(20);
    vbar()->setSingleStep(1);

    viewport()->setBackgroundRole(QPalette::Base);
    q->setAcceptDrops(true);
    q->setFocusPolicy(Qt::StrongFocus);
    q->setAttribute(Qt::WA_KeyCompression);
    q->setAttribute(Qt::WA_InputMethodEnabled);
    q->setInputMethodHints(Qt::ImhMultiLine);

#ifndef QT_NO_CURSOR
    viewport()->setCursor(Qt::IBeamCursor);
#endif
}

void PlainTextEditPrivate::updatePlaceholderVisibility()
{
    // We normally only repaint the part of view that contains text in the
    // document that has changed (in repaintContents). But the placeholder
    // text is not a part of the document, but is drawn on separately. So whenever
    // we either show or hide the placeholder text, we issue a full update.
    if (placeholderTextShown != placeHolderTextToBeShown()) {
        viewport()->update();
        placeholderTextShown = placeHolderTextToBeShown();
    }
}

void PlainTextEditPrivate::repaintContents(const QRectF &contentsRect)
{
    if (!contentsRect.isValid()) {
        updateViewport();
        return;
    }
    const int xOffset = horizontalOffset();
    const int yOffset = (int)verticalOffset();
    const QRect visibleRect(xOffset, yOffset, viewport()->width(), viewport()->height());

    QRect r = contentsRect.adjusted(-1, -1, 1, 1).intersected(visibleRect).toAlignedRect();
    if (r.isEmpty())
        return;

    r.translate(-xOffset, -yOffset);
    viewport()->update(r);
    emit q->updateRequest(r, 0);
}

void PlainTextEditPrivate::pageUpDown(QTextCursor::MoveOperation op, QTextCursor::MoveMode moveMode, bool moveCursor)
{


    QTextCursor cursor = control->textCursor();
    if (moveCursor) {
        ensureCursorVisible();
        if (!pageUpDownLastCursorYIsValid)
            pageUpDownLastCursorY = control->cursorRect(cursor).top() - verticalOffset();
    }

    qreal lastY = pageUpDownLastCursorY;


    if (op == QTextCursor::Down) {
        QRectF visible = QRectF(viewport()->rect()).translated(-q->contentOffset());
        QTextBlock firstVisibleBlock = q->firstVisibleBlock();
        QTextBlock block = firstVisibleBlock;
        QRectF br = q->blockBoundingRect(block);
        qreal h = 0;
        int atEnd = false;
        while (h + br.height() <= visible.bottom()) {
            if (!block.next().isValid()) {
                atEnd = true;
                lastY = visible.bottom(); // set cursor to last line
                break;
            }
            h += br.height();
            block = block.next();
            br = q->blockBoundingRect(block);
        }

        if (!atEnd) {
            int line = 0;
            qreal diff = visible.bottom() - h;
            int lineCount = block.layout()->lineCount();
            while (line < lineCount - 1) {
                if (block.layout()->lineAt(line).naturalTextRect().bottom() > diff) {
                    // the first line that did not completely fit the screen
                    break;
                }
                ++line;
            }
            setTopBlock(block.blockNumber(), line);
        }

        if (moveCursor) {
            // move using movePosition to keep the cursor's x
            lastY += verticalOffset();
            bool moved = false;
            do {
                moved = cursor.movePosition(op, moveMode);
            } while (moved && control->cursorRect(cursor).top() < lastY);
        }

    } else if (op == QTextCursor::Up) {

        QRectF visible = QRectF(viewport()->rect()).translated(-q->contentOffset());
        visible.translate(0, -visible.height()); // previous page
        QTextBlock block = q->firstVisibleBlock();
        qreal h = 0;
        while (h >= visible.top()) {
            if (!block.previous().isValid()) {
                if (control->topBlock == 0 && topLine == 0) {
                    lastY = 0; // set cursor to first line
                }
                break;
            }
            block = block.previous();
            QRectF br = q->blockBoundingRect(block);
            h -= br.height();
        }

        int line = 0;
        if (block.isValid()) {
            qreal diff = visible.top() - h;
            int lineCount = block.layout()->lineCount();
            while (line < lineCount) {
                if (block.layout()->lineAt(line).naturalTextRect().top() >= diff)
                    break;
                ++line;
            }
            if (line == lineCount) {
                if (block.next().isValid() && block.next() != q->firstVisibleBlock()) {
                    block = block.next();
                    line = 0;
                } else {
                    --line;
                }
            }
        }
        setTopBlock(block.blockNumber(), line);

        if (moveCursor) {
            cursor.setVisualNavigation(true);
            // move using movePosition to keep the cursor's x
            lastY += verticalOffset();
            bool moved = false;
            do {
                moved = cursor.movePosition(op, moveMode);
            } while (moved && control->cursorRect(cursor).top() > lastY);
        }
    }

    if (moveCursor) {
        control->setTextCursor(cursor, moveMode == QTextCursor::KeepAnchor);
        pageUpDownLastCursorYIsValid = true;
    }
}

#if QT_CONFIG(scrollbar)

void PlainTextEditPrivate::adjustScrollbars()
{
    QTextDocument *doc = control->document();
    PlainTextDocumentLayout *documentLayout = qobject_cast<PlainTextDocumentLayout*>(doc->documentLayout());
    Q_ASSERT(documentLayout);
    bool documentSizeChangedBlocked = documentLayout->d->blockDocumentSizeChanged;
    documentLayout->d->blockDocumentSizeChanged = true;
    qreal margin = doc->documentMargin();

    int vmax = 0;

    int vSliderLength = 0;
    if (!centerOnScroll && q->isVisible()) {
        QTextBlock block = doc->lastBlock();
        const qreal visible = viewport()->rect().height() - margin - 1;
        qreal y = 0;
        int visibleFromBottom = 0;

        while (block.isValid()) {
            if (!block.isVisible()) {
                block = block.previous();
                continue;
            }
            y += documentLayout->blockBoundingRect(block).height();

            QTextLayout *layout = block.layout();
            int layoutLineCount = layout->lineCount();
            if (y > visible) {
                int lineNumber = 0;
                while (lineNumber < layoutLineCount) {
                    QTextLine line = layout->lineAt(lineNumber);
                    const QRectF lr = line.naturalTextRect();
                    if (lr.top() >= y - visible)
                        break;
                    ++lineNumber;
                }
                if (lineNumber < layoutLineCount)
                    visibleFromBottom += (layoutLineCount - lineNumber);
                break;

            }
            visibleFromBottom += layoutLineCount;
            block = block.previous();
        }
        vmax = qMax(0, doc->lineCount() - visibleFromBottom);
        vSliderLength = visibleFromBottom;

    } else {
        vmax = qMax(0, doc->lineCount() - 1);
        int lineSpacing = q->fontMetrics().lineSpacing();
        vSliderLength = lineSpacing != 0 ? viewport()->height() / lineSpacing : 0;
    }

    QSizeF documentSize = documentLayout->documentSize();
    vbar()->setRange(0, qMax(0, vmax));
    vbar()->setPageStep(vSliderLength);
    int visualTopLine = vmax;
    QTextBlock firstVisibleBlock = q->firstVisibleBlock();
    if (firstVisibleBlock.isValid())
        visualTopLine = firstVisibleBlock.firstLineNumber() + topLine;

    vbar()->setValue(visualTopLine);

    hbar()->setRange(0, (int)documentSize.width() - viewport()->width());
    hbar()->setPageStep(viewport()->width());
    documentLayout->d->blockDocumentSizeChanged = documentSizeChangedBlocked;
    setTopLine(vbar()->value());
}

#endif


void PlainTextEditPrivate::ensureViewportLayouted()
{
}

/*!
    \class PlainTextEdit
    \since 4.4
    \brief The PlainTextEdit class provides a widget that is used to edit and display
    plain text.

    \ingroup richtext-processing
    \inmodule QtWidgets

    \section1 Introduction and Concepts

    PlainTextEdit is an advanced viewer/editor supporting plain
    text. It is optimized to handle large documents and to respond
    quickly to user input.

    QPlainText uses very much the same technology and concepts as
    QTextEdit, but is optimized for plain text handling.

    PlainTextEdit works on paragraphs and characters. A paragraph is
    a formatted string which is word-wrapped to fit into the width of
    the widget. By default when reading plain text, one newline
    signifies a paragraph. A document consists of zero or more
    paragraphs. Paragraphs are separated by hard line breaks. Each
    character within a paragraph has its own attributes, for example,
    font and color.

    The shape of the mouse cursor on a PlainTextEdit is
    Qt::IBeamCursor by default.  It can be changed through the
    viewport()'s cursor property.

    \section1 Using PlainTextEdit as a Display Widget

    The text is set or replaced using setPlainText() which deletes the
    existing text and replaces it with the text passed to setPlainText().

    Text can be inserted using the QTextCursor class or using the
    convenience functions insertPlainText(), appendPlainText() or
    paste().

    By default, the text edit wraps words at whitespace to fit within
    the text edit widget. The setLineWrapMode() function is used to
    specify the kind of line wrap you want, \l WidgetWidth or \l
    NoWrap if you don't want any wrapping.  If you use word wrap to
    the widget's width \l WidgetWidth, you can specify whether to
    break on whitespace or anywhere with setWordWrapMode().

    The find() function can be used to find and select a given string
    within the text.

    If you want to limit the total number of paragraphs in a
    PlainTextEdit, as it is for example useful in a log viewer, then
    you can use the maximumBlockCount property. The combination of
    setMaximumBlockCount() and appendPlainText() turns PlainTextEdit
    into an efficient viewer for log text. The scrolling can be
    reduced with the centerOnScroll() property, making the log viewer
    even faster. Text can be formatted in a limited way, either using
    a syntax highlighter (see below), or by appending html-formatted
    text with appendHtml(). While PlainTextEdit does not support
    complex rich text rendering with tables and floats, it does
    support limited paragraph-based formatting that you may need in a
    log viewer.

    \section2 Read-only Key Bindings

    When PlainTextEdit is used read-only the key bindings are limited to
    navigation, and text may only be selected with the mouse:
    \table
    \header \li Keypresses \li Action
    \row \li Qt::UpArrow        \li Moves one line up.
    \row \li Qt::DownArrow        \li Moves one line down.
    \row \li Qt::LeftArrow        \li Moves one character to the left.
    \row \li Qt::RightArrow        \li Moves one character to the right.
    \row \li PageUp        \li Moves one (viewport) page up.
    \row \li PageDown        \li Moves one (viewport) page down.
    \row \li Home        \li Moves to the beginning of the text.
    \row \li End                \li Moves to the end of the text.
    \row \li Alt+Wheel
         \li Scrolls the page horizontally (the Wheel is the mouse wheel).
    \row \li Ctrl+Wheel        \li Zooms the text.
    \row \li Ctrl+A            \li Selects all text.
    \endtable


    \section1 Using PlainTextEdit as an Editor

    All the information about using PlainTextEdit as a display widget also
    applies here.

    Selection of text is handled by the QTextCursor class, which provides
    functionality for creating selections, retrieving the text contents or
    deleting selections. You can retrieve the object that corresponds with
    the user-visible cursor using the textCursor() method. If you want to set
    a selection in PlainTextEdit just create one on a QTextCursor object and
    then make that cursor the visible cursor using setCursor(). The selection
    can be copied to the clipboard with copy(), or cut to the clipboard with
    cut(). The entire text can be selected using selectAll().

    PlainTextEdit holds a QTextDocument object which can be retrieved using the
    document() method. You can also set your own document object using setDocument().
    QTextDocument emits a textChanged() signal if the text changes and it also
    provides a isModified() function which will return true if the text has been
    modified since it was either loaded or since the last call to setModified
    with false as argument. In addition it provides methods for undo and redo.

    \section2 Syntax Highlighting

    Just like QTextEdit, PlainTextEdit works together with
    QSyntaxHighlighter.

    \section2 Editing Key Bindings

    The list of key bindings which are implemented for editing:
    \table
    \header \li Keypresses \li Action
    \row \li Backspace \li Deletes the character to the left of the cursor.
    \row \li Delete \li Deletes the character to the right of the cursor.
    \row \li Ctrl+C \li Copy the selected text to the clipboard.
    \row \li Ctrl+Insert \li Copy the selected text to the clipboard.
    \row \li Ctrl+K \li Deletes to the end of the line.
    \row \li Ctrl+V \li Pastes the clipboard text into text edit.
    \row \li Shift+Insert \li Pastes the clipboard text into text edit.
    \row \li Ctrl+X \li Deletes the selected text and copies it to the clipboard.
    \row \li Shift+Delete \li Deletes the selected text and copies it to the clipboard.
    \row \li Ctrl+Z \li Undoes the last operation.
    \row \li Ctrl+Y \li Redoes the last operation.
    \row \li LeftArrow \li Moves the cursor one character to the left.
    \row \li Ctrl+LeftArrow \li Moves the cursor one word to the left.
    \row \li RightArrow \li Moves the cursor one character to the right.
    \row \li Ctrl+RightArrow \li Moves the cursor one word to the right.
    \row \li UpArrow \li Moves the cursor one line up.
    \row \li Ctrl+UpArrow \li Moves the cursor one word up.
    \row \li DownArrow \li Moves the cursor one line down.
    \row \li Ctrl+Down Arrow \li Moves the cursor one word down.
    \row \li PageUp \li Moves the cursor one page up.
    \row \li PageDown \li Moves the cursor one page down.
    \row \li Home \li Moves the cursor to the beginning of the line.
    \row \li Ctrl+Home \li Moves the cursor to the beginning of the text.
    \row \li End \li Moves the cursor to the end of the line.
    \row \li Ctrl+End \li Moves the cursor to the end of the text.
    \row \li Alt+Wheel \li Scrolls the page horizontally (the Wheel is the mouse wheel).
    \row \li Ctrl+Wheel \li Zooms the text.
    \endtable

    To select (mark) text hold down the Shift key whilst pressing one
    of the movement keystrokes, for example, \e{Shift+Right Arrow}
    will select the character to the right, and \e{Shift+Ctrl+Right
    Arrow} will select the word to the right, etc.

   \section1 Differences to QTextEdit

   PlainTextEdit is a thin class, implemented by using most of the
   technology that is behind QTextEdit and QTextDocument. Its
   performance benefits over QTextEdit stem mostly from using a
   different and simplified text layout called
   PlainTextDocumentLayout on the text document (see
   QTextDocument::setDocumentLayout()). The plain text document layout
   does not support tables nor embedded frames, and \e{replaces a
   pixel-exact height calculation with a line-by-line respectively
   paragraph-by-paragraph scrolling approach}. This makes it possible
   to handle significantly larger documents, and still resize the
   editor with line wrap enabled in real time. It also makes for a
   fast log viewer (see setMaximumBlockCount()).

    \sa QTextDocument, QTextCursor
        {Syntax Highlighter Example}, {Rich Text Processing}

*/

/*!
    \property PlainTextEdit::plainText

    This property gets and sets the plain text editor's contents. The previous
    contents are removed and undo/redo history is reset when this property is set.
    currentCharFormat() is also reset, unless textCursor() is already at the
    beginning of the document.

    By default, for an editor with no contents, this property contains an empty string.
*/

/*!
    \property PlainTextEdit::undoRedoEnabled
    \brief whether undo and redo are enabled

    Users are only able to undo or redo actions if this property is
    true, and if there is an action that can be undone (or redone).

    By default, this property is \c true.
*/

/*!
    \enum PlainTextEdit::LineWrapMode

    \value NoWrap
    \value WidgetWidth
*/


/*!
    Constructs an empty PlainTextEdit with parent \a
    parent.
*/
PlainTextEdit::PlainTextEdit(QWidget *parent)
    : QAbstractScrollArea(parent)
    , d(std::make_unique<PlainTextEditPrivate>(this))
{
    d->init();
}

/*!
    \internal
*/
PlainTextEdit::PlainTextEdit(PlainTextEditPrivate &dd, QWidget *parent)
    : QAbstractScrollArea(parent)
    , d(std::make_unique<PlainTextEditPrivate>(this))
{
    d->init();
}

/*!
    Constructs a PlainTextEdit with parent \a parent. The text edit will display
    the plain text \a text.
*/
PlainTextEdit::PlainTextEdit(const QString &text, QWidget *parent)
    : QAbstractScrollArea(parent)
    , d(std::make_unique<PlainTextEditPrivate>(this))
{
    d->init(text);
}


/*!
    Destructor.
*/
PlainTextEdit::~PlainTextEdit()
{
    if (d->documentLayoutPtr) {
        if (d->documentLayoutPtr->d->mainViewPrivate == d.get())
            d->documentLayoutPtr->d->mainViewPrivate = nullptr;
    }
}

/*!
    Makes \a document the new document of the text editor.

    The parent QObject of the provided document remains the owner
    of the object. If the current document is a child of the text
    editor, then it is deleted.

    The document must have a document layout that inherits
    PlainTextDocumentLayout (see QTextDocument::setDocumentLayout()).

    \sa document()
*/
void PlainTextEdit::setDocument(QTextDocument *document)
{
    PlainTextDocumentLayout *documentLayout = nullptr;

    if (!document) {
        document = new QTextDocument(d->control);
        documentLayout = new PlainTextDocumentLayout(document);
        document->setDocumentLayout(documentLayout);
    } else {
        documentLayout = qobject_cast<PlainTextDocumentLayout*>(document->documentLayout());
        if (Q_UNLIKELY(!documentLayout)) {
            qWarning("PlainTextEdit::setDocument: Document set does not support PlainTextDocumentLayout");
            return;
        }
    }
    d->control->setDocument(document);
    if (!documentLayout->d->mainViewPrivate)
        documentLayout->d->mainViewPrivate = d.get();
    d->documentLayoutPtr = documentLayout;
    d->updateDefaultTextOption();
    d->relayoutDocument();
    d->adjustScrollbars();
}

/*!
    Returns a pointer to the underlying document.

    \sa setDocument()
*/
QTextDocument *PlainTextEdit::document() const
{
    return d->control->document();
}

/*!
    \since 5.3

    \property PlainTextEdit::placeholderText
    \brief the editor placeholder text

    Setting this property makes the editor display a grayed-out
    placeholder text as long as the document() is empty.

    By default, this property contains an empty string.

    \sa document()
*/
void PlainTextEdit::setPlaceholderText(const QString &placeholderText)
{
    if (d->placeholderText != placeholderText) {
        d->placeholderText = placeholderText;
        d->updatePlaceholderVisibility();
    }
}

QString PlainTextEdit::placeholderText() const
{
    return d->placeholderText;
}

/*!
    Sets the visible \a cursor.
*/
void PlainTextEdit::setTextCursor(const QTextCursor &cursor)
{
    doSetTextCursor(cursor);
}

/*!
    \internal

     This provides a hook for subclasses to intercept cursor changes.
*/

void PlainTextEdit::doSetTextCursor(const QTextCursor &cursor)
{
    d->control->setTextCursor(cursor);
}

/*!
    Returns a copy of the QTextCursor that represents the currently visible cursor.
    Note that changes on the returned cursor do not affect PlainTextEdit's cursor; use
    setTextCursor() to update the visible cursor.
 */
QTextCursor PlainTextEdit::textCursor() const
{
    return d->control->textCursor();
}

/*!
    Returns the reference of the anchor at position \a pos, or an
    empty string if no anchor exists at that point.

    \since 4.7
 */
QString PlainTextEdit::anchorAt(const QPoint &pos) const
{
    return QString();
}

/*!
    Undoes the last operation.

    If there is no operation to undo, i.e. there is no undo step in
    the undo/redo history, nothing happens.

    \sa redo()
*/
void PlainTextEdit::undo()
{
    d->control->undo();
}

void PlainTextEdit::redo()
{
    d->control->redo();
}

/*!
    \fn void PlainTextEdit::redo()

    Redoes the last operation.

    If there is no operation to redo, i.e. there is no redo step in
    the undo/redo history, nothing happens.

    \sa undo()
*/

#ifndef QT_NO_CLIPBOARD
/*!
    Copies the selected text to the clipboard and deletes it from
    the text edit.

    If there is no selected text nothing happens.

    \sa copy(), paste()
*/

void PlainTextEdit::cut()
{
    d->control->cut();
}

/*!
    Copies any selected text to the clipboard.

    \sa copyAvailable()
*/

void PlainTextEdit::copy()
{
    d->control->copy();
}

/*!
    Pastes the text from the clipboard into the text edit at the
    current cursor position.

    If there is no text in the clipboard nothing happens.

    To change the behavior of this function, i.e. to modify what
    PlainTextEdit can paste and how it is being pasted, reimplement the
    virtual canInsertFromMimeData() and insertFromMimeData()
    functions.

    \sa cut(), copy()
*/

void PlainTextEdit::paste()
{
    d->control->paste();
}
#endif

/*!
    Deletes all the text in the text edit.

    Notes:
    \list
    \li The undo/redo history is also cleared.
    \li currentCharFormat() is reset, unless textCursor()
    is already at the beginning of the document.
    \endlist

    \sa cut(), setPlainText()
*/
void PlainTextEdit::clear()
{
    // clears and sets empty content
    d->control->topBlock = d->topLine = d->topLineFracture = 0;
    d->control->clear();
}


/*!
    Selects all text.

    \sa copy(), cut(), textCursor()
 */
void PlainTextEdit::selectAll()
{
    d->control->selectAll();
}

/*! \internal
*/
bool PlainTextEdit::event(QEvent *e)
{

    switch (e->type()) {
#ifndef QT_NO_CONTEXTMENU
    case QEvent::ContextMenu:
        if (static_cast<QContextMenuEvent *>(e)->reason() == QContextMenuEvent::Keyboard) {
            ensureCursorVisible();
            const QPoint cursorPos = cursorRect().center();
            QContextMenuEvent ce(QContextMenuEvent::Keyboard, cursorPos, viewport()->mapToGlobal(cursorPos));
            ce.setAccepted(e->isAccepted());
            const bool result = QAbstractScrollArea::event(&ce);
            e->setAccepted(ce.isAccepted());
            return result;
        }
        break;
#endif // QT_NO_CONTEXTMENU
    case QEvent::ShortcutOverride:
    case QEvent::ToolTip:
        d->sendControlEvent(e);
        break;
#ifdef QT_KEYPAD_NAVIGATION
    case QEvent::EnterEditFocus:
    case QEvent::LeaveEditFocus:
        if (QApplicationPrivate::keypadNavigationEnabled())
            d->sendControlEvent(e);
        break;
#endif
#ifndef QT_NO_GESTURES
    case QEvent::Gesture:
        if (auto *g = static_cast<QGestureEvent *>(e)->gesture(Qt::PanGesture)) {
            QPanGesture *panGesture = static_cast<QPanGesture *>(g);
            QScrollBar *hBar = horizontalScrollBar();
            QScrollBar *vBar = verticalScrollBar();
            if (panGesture->state() == Qt::GestureStarted)
                d->originalOffsetY = vBar->value();
            QPointF offset = panGesture->offset();
            if (!offset.isNull()) {
                if (QGuiApplication::isRightToLeft())
                    offset.rx() *= -1;
                // PlainTextEdit scrolls by lines only in vertical direction
                QFontMetrics fm(document()->defaultFont());
                int lineHeight = fm.height();
                int newX = hBar->value() - panGesture->delta().x();
                int newY = d->originalOffsetY - offset.y()/lineHeight;
                hBar->setValue(newX);
                vBar->setValue(newY);
            }
        }
        return true;
#endif // QT_NO_GESTURES
    case QEvent::WindowActivate:
    case QEvent::WindowDeactivate:
        d->control->setPalette(palette());
        break;
    default:
        break;
    }
    return QAbstractScrollArea::event(e);
}

/*! \internal
*/

void PlainTextEdit::timerEvent(QTimerEvent *e)
{
    if (e->timerId() == d->autoScrollTimer.timerId()) {
        QRect visible = viewport()->rect();
        QPoint pos;
        if (d->inDrag) {
            pos = d->autoScrollDragPos;
            visible.adjust(qMin(visible.width()/3,20), qMin(visible.height()/3,20),
                           -qMin(visible.width()/3,20), -qMin(visible.height()/3,20));
        } else {
            const QPoint globalPos = QCursor::pos();
            pos = viewport()->mapFromGlobal(globalPos);
            QMouseEvent ev(QEvent::MouseMove, pos, viewport()->mapTo(viewport()->topLevelWidget(), pos), globalPos,
                           Qt::LeftButton, Qt::LeftButton, QGuiApplication::keyboardModifiers());
            mouseMoveEvent(&ev);
        }
        int deltaY = qMax(pos.y() - visible.top(), visible.bottom() - pos.y()) - visible.height();
        int deltaX = qMax(pos.x() - visible.left(), visible.right() - pos.x()) - visible.width();
        int delta = qMax(deltaX, deltaY);
        if (delta >= 0) {
            if (delta < 7)
                delta = 7;
            int timeout = 4900 / (delta * delta);
            d->autoScrollTimer.start(timeout, this);

            if (deltaY > 0)
                d->vbar()->triggerAction(pos.y() < visible.center().y() ?
                                           QAbstractSlider::SliderSingleStepSub
                                                                      : QAbstractSlider::SliderSingleStepAdd);
            if (deltaX > 0)
                d->hbar()->triggerAction(pos.x() < visible.center().x() ?
                                           QAbstractSlider::SliderSingleStepSub
                                                                      : QAbstractSlider::SliderSingleStepAdd);
        }
    }
#ifdef QT_KEYPAD_NAVIGATION
    else if (e->timerId() == d->deleteAllTimer.timerId()) {
        d->deleteAllTimer.stop();
        clear();
    }
#endif
}

/*!
    Changes the text of the text edit to the string \a text.
    Any previous text is removed.

    \a text is interpreted as plain text.

    Notes:
    \list
    \li The undo/redo history is also cleared.
    \li currentCharFormat() is reset, unless textCursor()
    is already at the beginning of the document.
    \endlist

    \sa toPlainText()
*/

void PlainTextEdit::setPlainText(const QString &text)
{
    d->control->setPlainText(text);
}

/*!
    \fn QString PlainTextEdit::toPlainText() const

    Returns the text of the text edit as plain text.

    \sa PlainTextEdit::setPlainText()
 */

/*! \reimp
*/
void PlainTextEdit::keyPressEvent(QKeyEvent *e)
{

#ifdef QT_KEYPAD_NAVIGATION
    switch (e->key()) {
    case Qt::Key_Select:
        if (QApplicationPrivate::keypadNavigationEnabled()) {
            if (!(d->control->textInteractionFlags() & Qt::LinksAccessibleByKeyboard))
                setEditFocus(!hasEditFocus());
            else {
                if (!hasEditFocus())
                    setEditFocus(true);
                else {
                    QTextCursor cursor = d->control->textCursor();
                    QTextCharFormat charFmt = cursor.charFormat();
                    if (!cursor.hasSelection() || charFmt.anchorHref().isEmpty()) {
                        setEditFocus(false);
                    }
                }
            }
        }
        break;
    case Qt::Key_Back:
    case Qt::Key_No:
        if (!QApplicationPrivate::keypadNavigationEnabled() || !hasEditFocus()) {
            e->ignore();
            return;
        }
        break;
    default:
        if (QApplicationPrivate::keypadNavigationEnabled()) {
            if (!hasEditFocus() && !(e->modifiers() & Qt::ControlModifier)) {
                if (e->text()[0].isPrint()) {
                    setEditFocus(true);
                    clear();
                } else {
                    e->ignore();
                    return;
                }
            }
        }
        break;
    }
#endif

#ifndef QT_NO_SHORTCUT

    Qt::TextInteractionFlags tif = d->control->textInteractionFlags();

    if (tif & Qt::TextSelectableByKeyboard){
        if (e == QKeySequence::SelectPreviousPage) {
            e->accept();
            d->pageUpDown(QTextCursor::Up, QTextCursor::KeepAnchor);
            return;
        } else if (e ==QKeySequence::SelectNextPage) {
            e->accept();
            d->pageUpDown(QTextCursor::Down, QTextCursor::KeepAnchor);
            return;
        }
    }
    if (tif & (Qt::TextSelectableByKeyboard | Qt::TextEditable)) {
        if (e == QKeySequence::MoveToPreviousPage) {
            e->accept();
            d->pageUpDown(QTextCursor::Up, QTextCursor::MoveAnchor);
            return;
        } else if (e == QKeySequence::MoveToNextPage) {
            e->accept();
            d->pageUpDown(QTextCursor::Down, QTextCursor::MoveAnchor);
            return;
        }
    }

    if (!(tif & Qt::TextEditable)) {
        switch (e->key()) {
        case Qt::Key_Space:
            e->accept();
            if (e->modifiers() & Qt::ShiftModifier)
                d->vbar()->triggerAction(QAbstractSlider::SliderPageStepSub);
            else
                d->vbar()->triggerAction(QAbstractSlider::SliderPageStepAdd);
            break;
        default:
            d->sendControlEvent(e);
            if (!e->isAccepted() && e->modifiers() == Qt::NoModifier) {
                if (e->key() == Qt::Key_Home) {
                    d->vbar()->triggerAction(QAbstractSlider::SliderToMinimum);
                    e->accept();
                } else if (e->key() == Qt::Key_End) {
                    d->vbar()->triggerAction(QAbstractSlider::SliderToMaximum);
                    e->accept();
                }
            }
            if (!e->isAccepted()) {
                QAbstractScrollArea::keyPressEvent(e);
            }
        }
        return;
    }
#endif // QT_NO_SHORTCUT

    d->sendControlEvent(e);
#ifdef QT_KEYPAD_NAVIGATION
    if (!e->isAccepted()) {
        switch (e->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
            if (QApplicationPrivate::keypadNavigationEnabled()) {
                // Cursor position didn't change, so we want to leave
                // these keys to change focus.
                e->ignore();
                return;
            }
            break;
        case Qt::Key_Left:
        case Qt::Key_Right:
            if (QApplicationPrivate::keypadNavigationEnabled()
                && QApplication::navigationMode() == Qt::NavigationModeKeypadDirectional) {
                // Same as for Key_Up and Key_Down.
                e->ignore();
                return;
            }
            break;
        case Qt::Key_Back:
            if (!e->isAutoRepeat()) {
                if (QApplicationPrivate::keypadNavigationEnabled()) {
                    if (document()->isEmpty()) {
                        setEditFocus(false);
                        e->accept();
                    } else if (!d->deleteAllTimer.isActive()) {
                        e->accept();
                        d->deleteAllTimer.start(750, this);
                    }
                } else {
                    e->ignore();
                    return;
                }
            }
            break;
        default: break;
        }
    }
#endif
}

/*! \reimp
*/
void PlainTextEdit::keyReleaseEvent(QKeyEvent *e)
{
    if (!isReadOnly())
        d->handleSoftwareInputPanel();

#ifdef QT_KEYPAD_NAVIGATION
    if (QApplicationPrivate::keypadNavigationEnabled()) {
        if (!e->isAutoRepeat() && e->key() == Qt::Key_Back
            && d->deleteAllTimer.isActive()) {
            d->deleteAllTimer.stop();
            QTextCursor cursor = d->control->textCursor();
            QTextBlockFormat blockFmt = cursor.blockFormat();

            QTextList *list = cursor.currentList();
            if (list && cursor.atBlockStart()) {
                list->remove(cursor.block());
            } else if (cursor.atBlockStart() && blockFmt.indent() > 0) {
                blockFmt.setIndent(blockFmt.indent() - 1);
                cursor.setBlockFormat(blockFmt);
            } else {
                cursor.deletePreviousChar();
            }
            setTextCursor(cursor);
        }
    }
#else
    QWidget::keyReleaseEvent(e);
#endif
}

/*!
    Loads the resource specified by the given \a type and \a name.

    This function is an extension of QTextDocument::loadResource().

    \sa QTextDocument::loadResource()
*/
QVariant PlainTextEdit::loadResource(int type, const QUrl &name)
{
    Q_UNUSED(type);
    Q_UNUSED(name);
    return QVariant();
}

/*! \reimp
*/
void PlainTextEdit::resizeEvent(QResizeEvent *e)
{
    if (e->oldSize().width() != e->size().width())
        d->relayoutDocument();
    d->adjustScrollbars();
}

void PlainTextEditPrivate::relayoutDocument()
{
    QTextDocument *doc = control->document();
    PlainTextDocumentLayout *documentLayout = qobject_cast<PlainTextDocumentLayout*>(doc->documentLayout());
    Q_ASSERT(documentLayout);
    documentLayoutPtr = documentLayout;

    int width = viewport()->width();

    if (documentLayout->d->mainViewPrivate == nullptr
        || documentLayout->d->mainViewPrivate == this
        || width > documentLayout->textWidth()) {
        documentLayout->d->mainViewPrivate = this;
        documentLayout->setTextWidth(width);
    }
}

static void fillBackground(QPainter *p, const QRectF &rect, QBrush brush, const QRectF &gradientRect = QRectF())
{
    p->save();
    if (brush.style() >= Qt::LinearGradientPattern && brush.style() <= Qt::ConicalGradientPattern) {
        if (!gradientRect.isNull()) {
            QTransform m = QTransform::fromTranslate(gradientRect.left(), gradientRect.top());
            m.scale(gradientRect.width(), gradientRect.height());
            brush.setTransform(m);
            const_cast<QGradient *>(brush.gradient())->setCoordinateMode(QGradient::LogicalMode);
        }
    } else {
        p->setBrushOrigin(rect.topLeft());
    }
    p->fillRect(rect, brush);
    p->restore();
}



/*! \reimp
*/
void PlainTextEdit::paintEvent(QPaintEvent *e)
{
    QPainter painter(viewport());
    Q_ASSERT(qobject_cast<PlainTextDocumentLayout*>(document()->documentLayout()));

    QPointF offset(contentOffset());

    QRect er = e->rect();
    QRect viewportRect = viewport()->rect();

    bool editable = !isReadOnly();

    QTextBlock block = firstVisibleBlock();
    qreal maximumWidth = document()->documentLayout()->documentSize().width();

    // Set a brush origin so that the WaveUnderline knows where the wave started
    painter.setBrushOrigin(offset);

    // keep right margin clean from full-width selection
    int maxX = offset.x() + qMax((qreal)viewportRect.width(), maximumWidth)
               - document()->documentMargin() + cursorWidth();
    er.setRight(qMin(er.right(), maxX));
    painter.setClipRect(er);

    if (d->placeHolderTextToBeShown()) {
        const QColor col = d->control->palette().placeholderText().color();
        painter.setPen(col);
        painter.setClipRect(e->rect());
        const int margin = int(document()->documentMargin());
        QRectF textRect = viewportRect.adjusted(margin, margin, 0, 0);
        painter.drawText(textRect, Qt::AlignTop | Qt::TextWordWrap, placeholderText());
    }

    QAbstractTextDocumentLayout::PaintContext context = getPaintContext();
    painter.setPen(context.palette.text().color());

    while (block.isValid()) {

        QRectF r = blockBoundingRect(block).translated(offset);
        QTextLayout *layout = block.layout();

        if (!block.isVisible()) {
            offset.ry() += r.height();
            block = block.next();
            continue;
        }

        if (r.bottom() >= er.top() && r.top() <= er.bottom()) {

            QTextBlockFormat blockFormat = block.blockFormat();

            QBrush bg = blockFormat.background();
            if (bg != Qt::NoBrush) {
                QRectF contentsRect = r;
                contentsRect.setWidth(qMax(r.width(), maximumWidth));
                fillBackground(&painter, contentsRect, bg);
            }

            QList<QTextLayout::FormatRange> selections;
            int blpos = block.position();
            int bllen = block.length();
            for (int i = 0; i < context.selections.size(); ++i) {
                const QAbstractTextDocumentLayout::Selection &range = context.selections.at(i);
                const int selStart = range.cursor.selectionStart() - blpos;
                const int selEnd = range.cursor.selectionEnd() - blpos;
                if (selStart < bllen && selEnd > 0
                    && selEnd > selStart) {
                    QTextLayout::FormatRange o;
                    o.start = selStart;
                    o.length = selEnd - selStart;
                    o.format = range.format;
                    selections.append(o);
                } else if (!range.cursor.hasSelection() && range.format.hasProperty(QTextFormat::FullWidthSelection)
                           && block.contains(range.cursor.position())) {
                    // for full width selections we don't require an actual selection, just
                    // a position to specify the line. that's more convenience in usage.
                    QTextLayout::FormatRange o;
                    QTextLine l = layout->lineForTextPosition(range.cursor.position() - blpos);
                    o.start = l.textStart();
                    o.length = l.textLength();
                    if (o.start + o.length == bllen - 1)
                        ++o.length; // include newline
                    o.format = range.format;
                    selections.append(o);
                }
            }

            bool drawCursor = ((editable || (textInteractionFlags() & Qt::TextSelectableByKeyboard))
                               && context.cursorPosition >= blpos
                               && context.cursorPosition < blpos + bllen);

            bool drawCursorAsBlock = drawCursor && overwriteMode() ;

            if (drawCursorAsBlock) {
                if (context.cursorPosition == blpos + bllen - 1) {
                    drawCursorAsBlock = false;
                } else {
                    QTextLayout::FormatRange o;
                    o.start = context.cursorPosition - blpos;
                    o.length = 1;
                    o.format.setForeground(palette().base());
                    o.format.setBackground(palette().text());
                    selections.append(o);
                }
            }

            layout->draw(&painter, offset, selections, er);

            if ((drawCursor && !drawCursorAsBlock)
                || (editable && context.cursorPosition < -1
                    && !layout->preeditAreaText().isEmpty())) {
                int cpos = context.cursorPosition;
                if (cpos < -1)
                    cpos = layout->preeditAreaPosition() - (cpos + 2);
                else
                    cpos -= blpos;
                layout->drawCursor(&painter, offset, cpos, cursorWidth());
            }
        }

        offset.ry() += r.height();
        if (offset.y() > viewportRect.height())
            break;
        block = block.next();
    }

    if (backgroundVisible() && !block.isValid() && offset.y() <= er.bottom()
        && (centerOnScroll() || verticalScrollBar()->maximum() == verticalScrollBar()->minimum())) {
        painter.fillRect(QRect(QPoint((int)er.left(), (int)offset.y()), er.bottomRight()), palette().window());
    }
}


void PlainTextEditPrivate::updateDefaultTextOption()
{
    QTextDocument *doc = control->document();

    QTextOption opt = doc->defaultTextOption();
    QTextOption::WrapMode oldWrapMode = opt.wrapMode();

    if (lineWrap == PlainTextEdit::NoWrap)
        opt.setWrapMode(QTextOption::NoWrap);
    else
        opt.setWrapMode(wordWrap);

    if (opt.wrapMode() != oldWrapMode)
        doc->setDefaultTextOption(opt);
}


/*! \reimp
*/
void PlainTextEdit::mousePressEvent(QMouseEvent *e)
{
#ifdef QT_KEYPAD_NAVIGATION
    if (QApplicationPrivate::keypadNavigationEnabled() && !hasEditFocus())
        setEditFocus(true);
#endif
    d->sendControlEvent(e);
}

/*! \reimp
*/
void PlainTextEdit::mouseMoveEvent(QMouseEvent *e)
{
    d->inDrag = false; // paranoia
    const QPoint pos = e->position().toPoint();
    d->sendControlEvent(e);
    if (!(e->buttons() & Qt::LeftButton))
        return;
    if (e->source() == Qt::MouseEventNotSynthesized) {
        const QRect visible = viewport()->rect();
        if (visible.contains(pos))
            d->autoScrollTimer.stop();
        else if (!d->autoScrollTimer.isActive())
            d->autoScrollTimer.start(100, this);
    }
}

/*! \reimp
*/
void PlainTextEdit::mouseReleaseEvent(QMouseEvent *e)
{
    d->sendControlEvent(e);
    if (e->source() == Qt::MouseEventNotSynthesized && d->autoScrollTimer.isActive()) {
        d->autoScrollTimer.stop();
        d->ensureCursorVisible();
    }

    if (!isReadOnly() && rect().contains(e->position().toPoint()))
        d->handleSoftwareInputPanel(e->button(), d->clickCausedFocus);
    d->clickCausedFocus = 0;
}

/*! \reimp
*/
void PlainTextEdit::mouseDoubleClickEvent(QMouseEvent *e)
{
    d->sendControlEvent(e);
}

/*! \reimp
*/
bool PlainTextEdit::focusNextPrevChild(bool next)
{
    if (!d->tabChangesFocus && d->control->textInteractionFlags() & Qt::TextEditable)
        return false;
    return QAbstractScrollArea::focusNextPrevChild(next);
}

#ifndef QT_NO_CONTEXTMENU
/*!
  \fn void PlainTextEdit::contextMenuEvent(QContextMenuEvent *event)

  Shows the standard context menu created with createStandardContextMenu().

  If you do not want the text edit to have a context menu, you can set
  its \l contextMenuPolicy to Qt::NoContextMenu. If you want to
  customize the context menu, reimplement this function. If you want
  to extend the standard context menu, reimplement this function, call
  createStandardContextMenu() and extend the menu returned.

  Information about the event is passed in the \a event object.

  \snippet code/src_gui_widgets_plaintextedit.cpp 0
*/
void PlainTextEdit::contextMenuEvent(QContextMenuEvent *e)
{
    d->sendControlEvent(e);
}
#endif // QT_NO_CONTEXTMENU

#if QT_CONFIG(draganddrop)
/*! \reimp
*/
void PlainTextEdit::dragEnterEvent(QDragEnterEvent *e)
{
    d->inDrag = true;
    d->sendControlEvent(e);
}

/*! \reimp
*/
void PlainTextEdit::dragLeaveEvent(QDragLeaveEvent *e)
{
    d->inDrag = false;
    d->autoScrollTimer.stop();
    d->sendControlEvent(e);
}

/*! \reimp
*/
void PlainTextEdit::dragMoveEvent(QDragMoveEvent *e)
{
    d->autoScrollDragPos = e->position().toPoint();
    if (!d->autoScrollTimer.isActive())
        d->autoScrollTimer.start(100, this);
    d->sendControlEvent(e);
}

/*! \reimp
*/
void PlainTextEdit::dropEvent(QDropEvent *e)
{
    d->inDrag = false;
    d->autoScrollTimer.stop();
    d->sendControlEvent(e);
}

#endif // QT_CONFIG(draganddrop)

/*! \reimp
 */
void PlainTextEdit::inputMethodEvent(QInputMethodEvent *e)
{
#ifdef QT_KEYPAD_NAVIGATION
    if (d->control->textInteractionFlags() & Qt::TextEditable
        && QApplicationPrivate::keypadNavigationEnabled()
        && !hasEditFocus()) {
        setEditFocus(true);
        selectAll();    // so text is replaced rather than appended to
    }
#endif
    d->sendControlEvent(e);
    const bool emptyEvent = e->preeditString().isEmpty() && e->commitString().isEmpty()
                            && e->attributes().isEmpty();
    if (emptyEvent)
        return;
    ensureCursorVisible();
}

/*!\reimp
*/
void PlainTextEdit::scrollContentsBy(int dx, int /*dy*/)
{
    d->setTopLine(d->vbar()->value(), dx);
}

/*!\reimp
*/
QVariant PlainTextEdit::inputMethodQuery(Qt::InputMethodQuery property) const
{
    return inputMethodQuery(property, QVariant());
}

/*!\internal
 */
QVariant PlainTextEdit::inputMethodQuery(Qt::InputMethodQuery query, QVariant argument) const
{
    switch (query) {
    case Qt::ImEnabled:
        return isEnabled() && !isReadOnly();
    case Qt::ImHints:
    case Qt::ImInputItemClipRectangle:
        return QWidget::inputMethodQuery(query);
    case Qt::ImReadOnly:
        return isReadOnly();
    default:
        break;
    }

    const QPointF offset = contentOffset();
    switch (argument.userType()) {
    case QMetaType::QRectF:
        argument = argument.toRectF().translated(-offset);
        break;
    case QMetaType::QPointF:
        argument = argument.toPointF() - offset;
        break;
    case QMetaType::QRect:
        argument = argument.toRect().translated(-offset.toPoint());
        break;
    case QMetaType::QPoint:
        argument = argument.toPoint() - offset;
        break;
    default:
        break;
    }

    const QVariant v = d->control->inputMethodQuery(query, argument);
    switch (v.userType()) {
    case QMetaType::QRectF:
        return v.toRectF().translated(offset);
    case QMetaType::QPointF:
        return v.toPointF() + offset;
    case QMetaType::QRect:
        return v.toRect().translated(offset.toPoint());
    case QMetaType::QPoint:
        return v.toPoint() + offset.toPoint();
    default:
        break;
    }
    return v;
}

/*! \reimp
*/
void PlainTextEdit::focusInEvent(QFocusEvent *e)
{
    if (e->reason() == Qt::MouseFocusReason) {
        d->clickCausedFocus = 1;
    }
    QAbstractScrollArea::focusInEvent(e);
    d->sendControlEvent(e);
}

/*! \reimp
*/
void PlainTextEdit::focusOutEvent(QFocusEvent *e)
{
    QAbstractScrollArea::focusOutEvent(e);
    d->sendControlEvent(e);
}

/*! \reimp
*/
void PlainTextEdit::showEvent(QShowEvent *)
{
    if (d->showCursorOnInitialShow) {
        d->showCursorOnInitialShow = false;
        ensureCursorVisible();
    }
    d->adjustScrollbars();
}

/*! \reimp
*/
void PlainTextEdit::changeEvent(QEvent *e)
{
    QAbstractScrollArea::changeEvent(e);

    switch (e->type()) {
    case QEvent::ApplicationFontChange:
    case QEvent::FontChange:
        d->control->document()->setDefaultFont(font());
        break;
    case QEvent::ActivationChange:
        if (!isActiveWindow())
            d->autoScrollTimer.stop();
        break;
    case QEvent::EnabledChange:
        e->setAccepted(isEnabled());
        d->control->setPalette(palette());
        d->sendControlEvent(e);
        break;
    case QEvent::PaletteChange:
        d->control->setPalette(palette());
        break;
    case QEvent::LayoutDirectionChange:
        d->sendControlEvent(e);
        break;
    default:
        break;
    }
}

/*! \reimp
*/
#if QT_CONFIG(wheelevent)
void PlainTextEdit::wheelEvent(QWheelEvent *e)
{
    if (!(d->control->textInteractionFlags() & Qt::TextEditable)) {
        if (e->modifiers() & Qt::ControlModifier) {
            float delta = e->angleDelta().y() / 120.f;
            zoomInF(delta);
            return;
        }
    }
    QAbstractScrollArea::wheelEvent(e);
    updateMicroFocus();
}
#endif

/*!
    Zooms in on the text by making the base font size \a range
    points larger and recalculating all font sizes to be the new size.
    This does not change the size of any images.

    \sa zoomOut()
*/
void PlainTextEdit::zoomIn(int range)
{
    zoomInF(range);
}

/*!
    Zooms out on the text by making the base font size \a range points
    smaller and recalculating all font sizes to be the new size. This
    does not change the size of any images.

    \sa zoomIn()
*/
void PlainTextEdit::zoomOut(int range)
{
    zoomInF(-range);
}

/*!
    \internal
*/
void PlainTextEdit::zoomInF(float range)
{
    if (range == 0.f)
        return;
    QFont f = font();
    const float newSize = f.pointSizeF() + range;
    if (newSize <= 0)
        return;
    f.setPointSizeF(newSize);
    setFont(f);
}

#ifndef QT_NO_CONTEXTMENU
/*!  This function creates the standard context menu which is shown
  when the user clicks on the text edit with the right mouse
  button. It is called from the default contextMenuEvent() handler.
  The popup menu's ownership is transferred to the caller.

  We recommend that you use the createStandardContextMenu(QPoint) version instead
  which will enable the actions that are sensitive to where the user clicked.
*/

QMenu *PlainTextEdit::createStandardContextMenu()
{
    return d->control->createStandardContextMenu(QPointF(), this);
}

/*!
  \since 5.5
  This function creates the standard context menu which is shown
  when the user clicks on the text edit with the right mouse
  button. It is called from the default contextMenuEvent() handler
  and it takes the \a position in document coordinates where the mouse click was.
  This can enable actions that are sensitive to the position where the user clicked.
  The popup menu's ownership is transferred to the caller.
*/

QMenu *PlainTextEdit::createStandardContextMenu(const QPoint &position)
{
    return d->control->createStandardContextMenu(position, this);
}
#endif // QT_NO_CONTEXTMENU

/*!
  returns a QTextCursor at position \a pos (in viewport coordinates).
*/
QTextCursor PlainTextEdit::cursorForPosition(const QPoint &pos) const
{
    return d->control->cursorForPosition(d->mapToContents(pos));
}

/*!
  returns a rectangle (in viewport coordinates) that includes the
  \a cursor.
 */
QRect PlainTextEdit::cursorRect(const QTextCursor &cursor) const
{
    if (cursor.isNull())
        return QRect();

    QRect r = d->control->cursorRect(cursor).toRect();
    r.translate(-d->horizontalOffset(),-(int)d->verticalOffset());
    return r;
}

/*!
  returns a rectangle (in viewport coordinates) that includes the
  cursor of the text edit.
 */
QRect PlainTextEdit::cursorRect() const
{
    QRect r = d->control->cursorRect().toRect();
    r.translate(-d->horizontalOffset(),-(int)d->verticalOffset());
    return r;
}


/*!
   \property PlainTextEdit::overwriteMode
   \brief whether text entered by the user will overwrite existing text

   As with many text editors, the plain text editor widget can be configured
   to insert or overwrite existing text with new text entered by the user.

   If this property is \c true, existing text is overwritten, character-for-character
   by new text; otherwise, text is inserted at the cursor position, displacing
   existing text.

   By default, this property is \c false (new text does not overwrite existing text).
*/

bool PlainTextEdit::overwriteMode() const
{
    return d->control->overwriteMode();
}

void PlainTextEdit::setOverwriteMode(bool overwrite)
{
    d->control->setOverwriteMode(overwrite);
}

/*!
    \property PlainTextEdit::tabStopDistance
    \brief the tab stop distance in pixels
    \since 5.10

    By default, this property contains a value of 80 pixels.

    Do not set a value less than the \l {QFontMetrics::}{horizontalAdvance()}
    of the QChar::VisualTabCharacter character, otherwise the tab-character
    will be drawn incompletely.

    \sa QTextOption::ShowTabsAndSpaces, QTextDocument::defaultTextOption
*/

qreal PlainTextEdit::tabStopDistance() const
{
    return d->control->document()->defaultTextOption().tabStopDistance();
}

void PlainTextEdit::setTabStopDistance(qreal distance)
{
    QTextOption opt = d->control->document()->defaultTextOption();
    if (opt.tabStopDistance() == distance || distance < 0)
        return;
    opt.setTabStopDistance(distance);
    d->control->document()->setDefaultTextOption(opt);
}


/*!
    \property PlainTextEdit::cursorWidth

    This property specifies the width of the cursor in pixels. The default value is 1.
*/
int PlainTextEdit::cursorWidth() const
{
    return d->control->cursorWidth();
}

void PlainTextEdit::setCursorWidth(int width)
{
    d->control->setCursorWidth(width);
}



/*!
    This function allows temporarily marking certain regions in the document
    with a given color, specified as \a selections. This can be useful for
    example in a programming editor to mark a whole line of text with a given
    background color to indicate the existence of a breakpoint.

    \sa QTextEdit::ExtraSelection, extraSelections()
*/
void PlainTextEdit::setExtraSelections(const QList<QTextEdit::ExtraSelection> &selections)
{
    d->control->setExtraSelections(selections);
}

/*!
    Returns previously set extra selections.

    \sa setExtraSelections()
*/
QList<QTextEdit::ExtraSelection> PlainTextEdit::extraSelections() const
{
    return d->control->extraSelections();
}

/*!
    This function returns a new MIME data object to represent the contents
    of the text edit's current selection. It is called when the selection needs
    to be encapsulated into a new QMimeData object; for example, when a drag
    and drop operation is started, or when data is copied to the clipboard.

    If you reimplement this function, note that the ownership of the returned
    QMimeData object is passed to the caller. The selection can be retrieved
    by using the textCursor() function.
*/
QMimeData *PlainTextEdit::createMimeDataFromSelection() const
{
    return d->control->WidgetTextControl::createMimeDataFromSelection();
}

/*!
    This function returns \c true if the contents of the MIME data object, specified
    by \a source, can be decoded and inserted into the document. It is called
    for example when during a drag operation the mouse enters this widget and it
    is necessary to determine whether it is possible to accept the drag.
 */
bool PlainTextEdit::canInsertFromMimeData(const QMimeData *source) const
{
    return d->control->WidgetTextControl::canInsertFromMimeData(source);
}

/*!
    This function inserts the contents of the MIME data object, specified
    by \a source, into the text edit at the current cursor position. It is
    called whenever text is inserted as the result of a clipboard paste
    operation, or when the text edit accepts data from a drag and drop
    operation.
*/
void PlainTextEdit::insertFromMimeData(const QMimeData *source)
{
    d->control->WidgetTextControl::insertFromMimeData(source);
}

/*!
    \property PlainTextEdit::readOnly
    \brief whether the text edit is read-only

    In a read-only text edit the user can only navigate through the
    text and select text; modifying the text is not possible.

    This property's default is false.
*/

bool PlainTextEdit::isReadOnly() const
{
    return !d->control || !(d->control->textInteractionFlags() & Qt::TextEditable);
}

void PlainTextEdit::setReadOnly(bool ro)
{
    Qt::TextInteractionFlags flags = Qt::NoTextInteraction;
    if (ro) {
        flags = Qt::TextSelectableByMouse;
    } else {
        flags = Qt::TextEditorInteraction;
    }
    d->control->setTextInteractionFlags(flags);
    setAttribute(Qt::WA_InputMethodEnabled, shouldEnableInputMethod(this));
    QEvent event(QEvent::ReadOnlyChange);
    QCoreApplication::sendEvent(this, &event);
}

/*!
    \property PlainTextEdit::textInteractionFlags

    Specifies how the label should interact with user input if it displays text.

    If the flags contain either Qt::LinksAccessibleByKeyboard or Qt::TextSelectableByKeyboard
    then the focus policy is also automatically set to Qt::ClickFocus.

    The default value depends on whether the PlainTextEdit is read-only
    or editable.
*/

void PlainTextEdit::setTextInteractionFlags(Qt::TextInteractionFlags flags)
{
    d->control->setTextInteractionFlags(flags);
}

Qt::TextInteractionFlags PlainTextEdit::textInteractionFlags() const
{
    return d->control->textInteractionFlags();
}

/*!
    Merges the properties specified in \a modifier into the current character
    format by calling QTextCursor::mergeCharFormat on the editor's cursor.
    If the editor has a selection then the properties of \a modifier are
    directly applied to the selection.

    \sa QTextCursor::mergeCharFormat()
 */
void PlainTextEdit::mergeCurrentCharFormat(const QTextCharFormat &modifier)
{
    d->control->mergeCurrentCharFormat(modifier);
}

/*!
    Sets the char format that is be used when inserting new text to \a
    format by calling QTextCursor::setCharFormat() on the editor's
    cursor.  If the editor has a selection then the char format is
    directly applied to the selection.
 */
void PlainTextEdit::setCurrentCharFormat(const QTextCharFormat &format)
{
    d->control->setCurrentCharFormat(format);
}

/*!
    Returns the char format that is used when inserting new text.
 */
QTextCharFormat PlainTextEdit::currentCharFormat() const
{
    return d->control->currentCharFormat();
}



/*!
    Convenience slot that inserts \a text at the current
    cursor position.

    It is equivalent to

    \snippet code/src_gui_widgets_plaintextedit.cpp 1
 */
void PlainTextEdit::insertPlainText(const QString &text)
{
    d->control->insertPlainText(text);
}


/*!
    Moves the cursor by performing the given \a operation.

    If \a mode is QTextCursor::KeepAnchor, the cursor selects the text it moves over.
    This is the same effect that the user achieves when they hold down the Shift key
    and move the cursor with the cursor keys.

    \sa QTextCursor::movePosition()
*/
void PlainTextEdit::moveCursor(QTextCursor::MoveOperation operation, QTextCursor::MoveMode mode)
{
    d->control->moveCursor(operation, mode);
}

/*!
    Returns whether text can be pasted from the clipboard into the textedit.
*/
bool PlainTextEdit::canPaste() const
{
    return d->control->canPaste();
}

/*!
    Convenience function to print the text edit's document to the given \a printer. This
    is equivalent to calling the print method on the document directly except that this
    function also supports QPrinter::Selection as print range.

    \sa QTextDocument::print()
*/
#ifndef QT_NO_PRINTER
void PlainTextEdit::print(QPagedPaintDevice *printer) const
{
    d->control->print(printer);
}
#endif

/*! \property PlainTextEdit::tabChangesFocus
  \brief whether \uicontrol Tab changes focus or is accepted as input

  In some occasions text edits should not allow the user to input
  tabulators or change indentation using the \uicontrol Tab key, as this breaks
  the focus chain. The default is false.

*/

bool PlainTextEdit::tabChangesFocus() const
{
    return d->tabChangesFocus;
}

void PlainTextEdit::setTabChangesFocus(bool b)
{
    d->tabChangesFocus = b;
}

/*!
    \property PlainTextEdit::documentTitle
    \brief the title of the document parsed from the text.

    By default, this property contains an empty string.
*/

/*!
    \property PlainTextEdit::lineWrapMode
    \brief the line wrap mode

    The default mode is WidgetWidth which causes words to be
    wrapped at the right edge of the text edit. Wrapping occurs at
    whitespace, keeping whole words intact. If you want wrapping to
    occur within words use setWordWrapMode().
*/

PlainTextEdit::LineWrapMode PlainTextEdit::lineWrapMode() const
{
    return d->lineWrap;
}

void PlainTextEdit::setLineWrapMode(LineWrapMode wrap)
{
    if (d->lineWrap == wrap)
        return;
    d->lineWrap = wrap;
    d->updateDefaultTextOption();
    d->relayoutDocument();
    d->adjustScrollbars();
    ensureCursorVisible();
}

/*!
    \property PlainTextEdit::wordWrapMode
    \brief the mode PlainTextEdit will use when wrapping text by words

    By default, this property is set to QTextOption::WrapAtWordBoundaryOrAnywhere.

    \sa QTextOption::WrapMode
*/

QTextOption::WrapMode PlainTextEdit::wordWrapMode() const
{
    return d->wordWrap;
}

void PlainTextEdit::setWordWrapMode(QTextOption::WrapMode mode)
{
    if (mode == d->wordWrap)
        return;
    d->wordWrap = mode;
    d->updateDefaultTextOption();
}

/*!
    \property PlainTextEdit::backgroundVisible
    \brief whether the palette background is visible outside the document area

    If set to true, the plain text edit paints the palette background
    on the viewport area not covered by the text document. Otherwise,
    if set to false, it won't. The feature makes it possible for
    the user to visually distinguish between the area of the document,
    painted with the base color of the palette, and the empty
    area not covered by any document.

    The default is false.
*/

bool PlainTextEdit::backgroundVisible() const
{
    return d->backgroundVisible;
}

void PlainTextEdit::setBackgroundVisible(bool visible)
{
    if (visible == d->backgroundVisible)
        return;
    d->backgroundVisible = visible;
    d->updateViewport();
}

/*!
    \property PlainTextEdit::centerOnScroll
    \brief whether the cursor should be centered on screen

    If set to true, the plain text edit scrolls the document
    vertically to make the cursor visible at the center of the
    viewport. This also allows the text edit to scroll below the end
    of the document. Otherwise, if set to false, the plain text edit
    scrolls the smallest amount possible to ensure the cursor is
    visible.  The same algorithm is applied to any new line appended
    through appendPlainText().

    The default is false.

    \sa centerCursor(), ensureCursorVisible()
*/

bool PlainTextEdit::centerOnScroll() const
{
    return d->centerOnScroll;
}

void PlainTextEdit::setCenterOnScroll(bool enabled)
{
    if (enabled == d->centerOnScroll)
        return;
    d->centerOnScroll = enabled;
    d->adjustScrollbars();
}



/*!
    Finds the next occurrence of the string, \a exp, using the given
    \a options. Returns \c true if \a exp was found and changes the
    cursor to select the match; otherwise returns \c false.
*/
bool PlainTextEdit::find(const QString &exp, QTextDocument::FindFlags options)
{
    return d->control->find(exp, options);
}

/*!
    \fn bool PlainTextEdit::find(const QRegularExpression &exp, QTextDocument::FindFlags options)

    \since 5.13
    \overload

    Finds the next occurrence, matching the regular expression, \a exp, using the given
    \a options.

    Returns \c true if a match was found and changes the cursor to select the match;
    otherwise returns \c false.

    \warning For historical reasons, the case sensitivity option set on
    \a exp is ignored. Instead, the \a options are used to determine
    if the search is case sensitive or not.
*/
#if QT_CONFIG(regularexpression)
bool PlainTextEdit::find(const QRegularExpression &exp, QTextDocument::FindFlags options)
{
    return d->control->find(exp, options);
}
#endif

/*!
    \fn void PlainTextEdit::copyAvailable(bool yes)

    This signal is emitted when text is selected or de-selected in the
    text edit.

    When text is selected this signal will be emitted with \a yes set
    to true. If no text has been selected or if the selected text is
    de-selected this signal is emitted with \a yes set to false.

    If \a yes is true then copy() can be used to copy the selection to
    the clipboard. If \a yes is false then copy() does nothing.

    \sa selectionChanged()
*/


/*!
    \fn void PlainTextEdit::selectionChanged()

    This signal is emitted whenever the selection changes.

    \sa copyAvailable()
*/

/*!
    \fn void PlainTextEdit::cursorPositionChanged()

    This signal is emitted whenever the position of the
    cursor changed.
*/



/*!
    \fn void PlainTextEdit::updateRequest(const QRect &rect, int dy)

    This signal is emitted when the text document needs an update of
    the specified \a rect. If the text is scrolled, \a rect will cover
    the entire viewport area. If the text is scrolled vertically, \a
    dy carries the amount of pixels the viewport was scrolled.

    The purpose of the signal is to support extra widgets in plain
    text edit subclasses that e.g. show line numbers, breakpoints, or
    other extra information.
*/

/*!  \fn void PlainTextEdit::blockCountChanged(int newBlockCount);

    This signal is emitted whenever the block count changes. The new
    block count is passed in \a newBlockCount.
*/

/*!  \fn void PlainTextEdit::modificationChanged(bool changed);

    This signal is emitted whenever the content of the document
    changes in a way that affects the modification state. If \a
    changed is true, the document has been modified; otherwise it is
    false.

    For example, calling setModified(false) on a document and then
    inserting text causes the signal to get emitted. If you undo that
    operation, causing the document to return to its original
    unmodified state, the signal will get emitted again.
*/




void PlainTextEditPrivate::append(const QString &text, Qt::TextFormat format)
{

    QTextDocument *document = control->document();
    PlainTextDocumentLayout *documentLayout = qobject_cast<PlainTextDocumentLayout*>(document->documentLayout());
    Q_ASSERT(documentLayout);

    int maximumBlockCount = document->maximumBlockCount();
    if (maximumBlockCount)
        document->setMaximumBlockCount(0);

    const bool atBottom =  q->isVisible()
                          && (control->blockBoundingRect(document->lastBlock()).bottom() - verticalOffset()
                              <= viewport()->rect().bottom());

    if (!q->isVisible())
        showCursorOnInitialShow = true;

    bool documentSizeChangedBlocked = documentLayout->d->blockDocumentSizeChanged;
    documentLayout->d->blockDocumentSizeChanged = true;

    switch (format) {
    case Qt::RichText:
        control->appendHtml(text);
        break;
    case Qt::PlainText:
        control->appendPlainText(text);
        break;
    default:
        control->append(text);
        break;
    }

    if (maximumBlockCount > 0) {
        if (document->blockCount() > maximumBlockCount) {
            bool blockUpdate = false;
            if (control->topBlock) {
                control->topBlock--;
                blockUpdate = true;
                emit q->updateRequest(viewport()->rect(), 0);
            }

            bool updatesBlocked = documentLayout->d->blockUpdate;
            documentLayout->d->blockUpdate = blockUpdate;
            QTextCursor cursor(document);
            cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            documentLayout->d->blockUpdate = updatesBlocked;
        }
        document->setMaximumBlockCount(maximumBlockCount);
    }

    documentLayout->d->blockDocumentSizeChanged = documentSizeChangedBlocked;
    adjustScrollbars();


    if (atBottom) {
        const bool needScroll =  !centerOnScroll
                                || control->blockBoundingRect(document->lastBlock()).bottom() - verticalOffset()
                                       > viewport()->rect().bottom();
        if (needScroll)
            vbar()->setValue(vbar()->maximum());
    }
}


/*!
    Appends a new paragraph with \a text to the end of the text edit.

    \sa appendHtml()
*/

void PlainTextEdit::appendPlainText(const QString &text)
{
    d->append(text, Qt::PlainText);
}

/*!
    Appends a new paragraph with \a html to the end of the text edit.

    appendPlainText()
*/

void PlainTextEdit::appendHtml(const QString &html)
{
    d->append(html, Qt::RichText);
}

void PlainTextEditPrivate::ensureCursorVisible(bool center)
{
    QRect visible = viewport()->rect();
    QRect cr = q->cursorRect();
    if (cr.top() < visible.top() || cr.bottom() > visible.bottom()) {
        ensureVisible(control->textCursor().position(), center);
    }

    const bool rtl = q->isRightToLeft();
    if (cr.left() < visible.left() || cr.right() > visible.right()) {
        int x = cr.center().x() + horizontalOffset() - visible.width()/2;
        hbar()->setValue(rtl ? hbar()->maximum() - x : x);
    }
}

/*!
    Ensures that the cursor is visible by scrolling the text edit if
    necessary.

    \sa centerCursor(), centerOnScroll
*/
void PlainTextEdit::ensureCursorVisible()
{
    d->ensureCursorVisible(d->centerOnScroll);
}


/*!  Scrolls the document in order to center the cursor vertically.

\sa ensureCursorVisible(), centerOnScroll
 */
void PlainTextEdit::centerCursor()
{
    d->ensureVisible(textCursor().position(), true, true);
}

/*!
  Returns the first visible block.

  \sa blockBoundingRect()
 */
QTextBlock PlainTextEdit::firstVisibleBlock() const
{
    return d->control->firstVisibleBlock();
}

/*!  Returns the content's origin in viewport coordinates.

     The origin of the content of a plain text edit is always the top
     left corner of the first visible text block. The content offset
     is different from (0,0) when the text has been scrolled
     horizontally, or when the first visible block has been scrolled
     partially off the screen, i.e. the visible text does not start
     with the first line of the first visible block, or when the first
     visible block is the very first block and the editor displays a
     margin.

     \sa firstVisibleBlock(), horizontalScrollBar(), verticalScrollBar()
 */
QPointF PlainTextEdit::contentOffset() const
{
    return QPointF(-d->horizontalOffset(), -d->verticalOffset());
}


/*!  Returns the bounding rectangle of the text \a block in content
  coordinates. Translate the rectangle with the contentOffset() to get
  visual coordinates on the viewport.

  \sa firstVisibleBlock(), blockBoundingRect()
 */
QRectF PlainTextEdit::blockBoundingGeometry(const QTextBlock &block) const
{
    return d->control->blockBoundingRect(block);
}

/*!
  Returns the bounding rectangle of the text \a block in the block's own coordinates.

  \sa blockBoundingGeometry()
 */
QRectF PlainTextEdit::blockBoundingRect(const QTextBlock &block) const
{
    PlainTextDocumentLayout *documentLayout = qobject_cast<PlainTextDocumentLayout*>(document()->documentLayout());
    Q_ASSERT(documentLayout);
    return documentLayout->blockBoundingRect(block);
}

/*!
    \property PlainTextEdit::blockCount
    \brief the number of text blocks in the document.

    By default, in an empty document, this property contains a value of 1.
*/
int PlainTextEdit::blockCount() const
{
    return document()->blockCount();
}

/*!  Returns the paint context for the viewport(), useful only when
  reimplementing paintEvent().
 */
QAbstractTextDocumentLayout::PaintContext PlainTextEdit::getPaintContext() const
{
    return d->control->getPaintContext(viewport());
}

/*!
    \property PlainTextEdit::maximumBlockCount
    \brief the limit for blocks in the document.

    Specifies the maximum number of blocks the document may have. If there are
    more blocks in the document that specified with this property blocks are removed
    from the beginning of the document.

    A negative or zero value specifies that the document may contain an unlimited
    amount of blocks.

    The default value is 0.

    Note that setting this property will apply the limit immediately to the document
    contents. Setting this property also disables the undo redo history.

*/


/*!
    \fn void PlainTextEdit::textChanged()

    This signal is emitted whenever the document's content changes; for
    example, when text is inserted or deleted, or when formatting is applied.
*/

/*!
    \fn void PlainTextEdit::undoAvailable(bool available)

    This signal is emitted whenever undo operations become available
    (\a available is true) or unavailable (\a available is false).
*/

/*!
    \fn void PlainTextEdit::redoAvailable(bool available)

    This signal is emitted whenever redo operations become available
    (\a available is true) or unavailable (\a available is false).
*/

} // namespace TextEditor

#include "plaintextedit.moc"
