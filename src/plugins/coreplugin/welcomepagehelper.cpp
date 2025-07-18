// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "welcomepagehelper.h"

#include "coreplugintr.h"

#include <utils/algorithm.h>
#include <utils/elidinglabel.h>
#include <utils/fancylineedit.h>
#include <utils/icon.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcassert.h>
#include <utils/qtcwidgets.h>
#include <utils/stylehelper.h>
#include <utils/theme/theme.h>

#include <QEasingCurve>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHoverEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QScrollArea>
#include <QTimer>

#include <qdrawutil.h>


QT_BEGIN_NAMESPACE
void qt_blurImage(QImage &blurImage, qreal radius, bool quality, int transposed = 0);
QT_END_NAMESPACE

using namespace Utils;

namespace Core {

using namespace WelcomePageHelpers;
using namespace StyleHelper::SpacingTokens;

static QColor themeColor(Theme::Color role)
{
    return creatorColor(role);
}

namespace WelcomePageHelpers {

void setBackgroundColor(QWidget *widget, Theme::Color colorRole)
{
    QPalette palette = creatorTheme()->palette();
    const QPalette::ColorRole role = QPalette::Window;
    palette.setBrush(role, {});
    palette.setColor(role, creatorColor(colorRole));
    widget->setPalette(palette);
    widget->setBackgroundRole(role);
    widget->setAutoFillBackground(true);
}

QWidget *createRule(Qt::Orientation orientation, QWidget *parent)
{
    auto rule = new QWidget(parent);
    if (orientation == Qt::Horizontal)
        rule->setFixedHeight(1);
    else
        rule->setFixedWidth(1);
    setBackgroundColor(rule, Theme::Token_Stroke_Subtle);
    return rule;
}

} // namespace WelcomePageHelpers

GridView::GridView(QWidget *parent)
    : QListView(parent)
{
    setResizeMode(QListView::Adjust);
    setMouseTracking(true); // To enable hover.
    setSelectionMode(QAbstractItemView::NoSelection);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setViewMode(IconMode);
    setUniformItemSizes(true);

    QPalette pal;
    pal.setColor(QPalette::Base, themeColor(Theme::Token_Background_Default));
    setPalette(pal); // Makes a difference on Mac.
}

void GridView::leaveEvent(QEvent *)
{
    QHoverEvent hev(QEvent::HoverLeave, QPointF(), QPointF(), QPointF());
    viewportEvent(&hev); // Seemingly needed to kill the hover paint.
}

SectionGridView::SectionGridView(QWidget *parent)
    : GridView(parent)
{}

void SectionGridView::setMaxRows(std::optional<int> max)
{
    m_maxRows = max;
    updateGeometry();
}

std::optional<int> SectionGridView::maxRows() const
{
    return m_maxRows;
}

bool SectionGridView::hasHeightForWidth() const
{
    return true;
}

int SectionGridView::heightForWidth(int width) const
{
    const QSize itemSize = ListItemDelegate::itemSize();
    const int columnCount = qMax(1, width / itemSize.width());
    const int rowCount = (model()->rowCount() + columnCount - 1) / columnCount;
    const int maxRowCount = m_maxRows ? std::min(*m_maxRows, rowCount) : rowCount;
    return maxRowCount * itemSize.height();
}

void SectionGridView::wheelEvent(QWheelEvent *e)
{
    if (m_maxRows) // circumvent scrolling of the list view
        QWidget::wheelEvent(e);
    else
        GridView::wheelEvent(e);
}

bool SectionGridView::event(QEvent *e)
{
    if (e->type() == QEvent::Resize) {
        const auto itemsFit = [this](const QSize &size) {
            const QSize itemSize = ListItemDelegate::itemSize();
            const int maxColumns = std::max(size.width() / itemSize.width(), 1);
            const int maxRows = std::max(size.height() / itemSize.height(), 1);
            const int maxItems = maxColumns * maxRows;
            const int items = model()->rowCount();
            return maxItems >= items;
        };
        auto resizeEvent = static_cast<QResizeEvent *>(e);
        const bool itemsCurrentyFit = itemsFit(size());
        if (!resizeEvent->oldSize().isValid()
            || itemsFit(resizeEvent->oldSize()) != itemsCurrentyFit) {
            emit itemsFitChanged(itemsCurrentyFit);
        }
    }
    return GridView::event(e);
}

ListModel::ListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

ListModel::~ListModel()
{
    clear();
}

void ListModel::appendItems(const QList<ListItem *> &items)
{
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size() + items.size());
    m_items.append(items);
    endInsertRows();
}

const QList<ListItem *> ListModel::items() const
{
    return m_items;
}

void ListModel::clear()
{
    beginResetModel();
    if (m_ownsItems)
        qDeleteAll(m_items);
    m_items.clear();
    endResetModel();
}

int ListModel::rowCount(const QModelIndex &) const
{
    return m_items.size();
}

QVariant ListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.count())
        return QVariant();

    ListItem *item = m_items.at(index.row());
    switch (role) {
    case Qt::DisplayRole: // for search only
        return QString(item->name + ' ' + item->tags.join(' '));
    case ItemRole:
        return QVariant::fromValue(item);
    case ItemImageRole: {
        QPixmap pixmap;
        if (QPixmapCache::find(item->imageUrl, &pixmap))
            return pixmap;
        if (pixmap.isNull() && m_fetchPixmapAndUpdatePixmapCache)
            pixmap = m_fetchPixmapAndUpdatePixmapCache(item->imageUrl);
        return pixmap;
    }
    case ItemTagsRole:
        return item->tags;
    default:
        return QVariant();
    }
}

void ListModel::setPixmapFunction(const PixmapFunction &fetchPixmapAndUpdatePixmapCache)
{
    m_fetchPixmapAndUpdatePixmapCache = fetchPixmapAndUpdatePixmapCache;
}

void ListModel::setOwnsItems(bool owns)
{
    m_ownsItems = owns;
}

ListModelFilter::ListModelFilter(ListModel *sourceModel, QObject *parent) :
    QSortFilterProxyModel(parent)
{
    setSourceModel(sourceModel);
    setDynamicSortFilter(true);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    sort(0);
}

bool ListModelFilter::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const ListItem *item = sourceModel()->index(sourceRow, 0, sourceParent).data(
                ListModel::ItemRole).value<Core::ListItem *>();

    if (!item)
        return false;

    bool earlyExitResult;
    if (leaveFilterAcceptsRowBeforeFiltering(item, &earlyExitResult))
        return earlyExitResult;

    if (!m_filterTags.isEmpty()) {
        return Utils::allOf(m_filterTags, [&item](const QString &filterTag) {
            return item->tags.contains(filterTag, Qt::CaseInsensitive);
        });
    }

    if (!m_filterStrings.isEmpty()) {
        for (const QString &subString : m_filterStrings) {
            bool wordMatch = false;
            wordMatch |= bool(item->name.contains(subString, Qt::CaseInsensitive));
            if (wordMatch)
                continue;
            const auto subMatch = [&subString](const QString &elem) {
                return elem.contains(subString, Qt::CaseInsensitive);
            };
            wordMatch |= Utils::contains(item->tags, subMatch);
            if (wordMatch)
                continue;
            wordMatch |= bool(item->description.contains(subString, Qt::CaseInsensitive));
            if (!wordMatch)
                return false;
        }
    }

    return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
}

struct SearchStringLexer
{
    QString code;
    const QChar *codePtr;
    QChar yychar;
    QString yytext;

    enum TokenKind {
        END_OF_STRING = 0,
        TAG,
        STRING_LITERAL,
        UNKNOWN
    };

    inline void yyinp() { yychar = *codePtr++; }

    explicit SearchStringLexer(const QString &code)
        : code(code)
        , codePtr(code.unicode())
        , yychar(QLatin1Char(' ')) { }

    int operator()() { return yylex(); }

    int yylex() {
        while (yychar.isSpace())
            yyinp(); // skip all the spaces

        yytext.clear();

        if (yychar.isNull())
            return END_OF_STRING;

        QChar ch = yychar;
        yyinp();

        switch (ch.unicode()) {
        case '"':
        case '\'':
        {
            const QChar quote = ch;
            yytext.clear();
            while (!yychar.isNull()) {
                if (yychar == quote) {
                    yyinp();
                    break;
                }
                if (yychar == QLatin1Char('\\')) {
                    yyinp();
                    switch (yychar.unicode()) {
                    case '"': yytext += QLatin1Char('"'); yyinp(); break;
                    case '\'': yytext += QLatin1Char('\''); yyinp(); break;
                    case '\\': yytext += QLatin1Char('\\'); yyinp(); break;
                    }
                } else {
                    yytext += yychar;
                    yyinp();
                }
            }
            return STRING_LITERAL;
        }

        default:
            if (ch.isLetterOrNumber() || ch == QLatin1Char('_')) {
                yytext.clear();
                yytext += ch;
                while (yychar.isLetterOrNumber() || yychar == QLatin1Char('_')) {
                    yytext += yychar;
                    yyinp();
                }
                if (yychar == QLatin1Char(':') && yytext == QLatin1String("tag")) {
                    yyinp();
                    return TAG;
                }
                return STRING_LITERAL;
            }
        }

        yytext += ch;
        return UNKNOWN;
    }
};

void ListModelFilter::setSearchString(const QString &arg)
{
    if (m_searchString == arg)
        return;

    m_searchString = arg;
    m_filterTags.clear();
    m_filterStrings.clear();

    // parse and update
    SearchStringLexer lex(arg);
    bool isTag = false;
    while (int tk = lex()) {
        if (tk == SearchStringLexer::TAG) {
            isTag = true;
            m_filterStrings.append(lex.yytext);
        }

        if (tk == SearchStringLexer::STRING_LITERAL) {
            if (isTag) {
                m_filterStrings.pop_back();
                m_filterTags.append(lex.yytext);
                isTag = false;
            } else {
                m_filterStrings.append(lex.yytext);
            }
        }
    }

    invalidateFilter();
    emit layoutChanged();
}

ListModel *ListModelFilter::sourceListModel() const
{
    return static_cast<ListModel *>(sourceModel());
}

bool ListModelFilter::leaveFilterAcceptsRowBeforeFiltering(const ListItem *, bool *) const
{
    return false;
}

constexpr TextFormat titleTF {Theme::Token_Text_Default, StyleHelper::UiElementH6};
constexpr TextFormat descriptionTF {titleTF.themeColor, StyleHelper::UiElementCaptionStrong};
constexpr TextFormat tagsLabelTF {Theme::Token_Text_Muted, StyleHelper::UiElementCaptionStrong};
constexpr TextFormat tagsTF {Theme::Token_Text_Accent, tagsLabelTF.uiElement};

constexpr qreal itemOutlineWidth = 1;
constexpr qreal itemCornerRounding = 6;
constexpr int thumbnailAreaBorderWidth = 1;
constexpr QSize thumbnailAreaSize =
    WelcomeThumbnailSize.grownBy({thumbnailAreaBorderWidth, thumbnailAreaBorderWidth,
                                  thumbnailAreaBorderWidth, thumbnailAreaBorderWidth});
constexpr int tagsRowsCount = 1;
constexpr int tagsHGap = GapHS;

constexpr QEasingCurve::Type hoverEasing = QEasingCurve::OutCubic;
constexpr std::chrono::milliseconds hoverDuration(300);
constexpr int hoverBlurRadius = 50;
constexpr qreal hoverBlurOpacity = 0.175;

QSize ListItemDelegate::itemSize()
{
    const int tagsTfLineHeight = tagsTF.lineHeight();
    const int width =
        PaddingHL
        + thumbnailAreaSize.width()
        + PaddingHL;
    const int height =
        PaddingVL
        + thumbnailAreaSize.height()
        + GapVM
        + titleTF.lineHeight()
        + GapVM
        + tagsTfLineHeight
        + (tagsTfLineHeight + PaddingVXxs) * (tagsRowsCount - 1) // If more than one row
        + PaddingVL;
    return {width + GapHXxl, height + GapVXxl};
}

void ListItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const
{
    // Unhovered tile
    // +-----------+------------------+-----------+---------+
    // |           |    (PaddingVL)   |           |         |
    // |           +------------------+           |         |
    // |           |    <thumbnail>   |           |         |
    // |           +------------------+           |         |
    // |           |      (GapVM)     |           |         |
    // |           +------------------+           |         |
    // |(PaddingHL)|      <title>     |(PaddingHL)|(GapHXxl)|
    // |           +------------------+           |         |
    // |           |      (GapVM)     |           |         |
    // |           +-----------+------+           |         |
    // |           |<tagsLabel>|<tags>|           |         |
    // |           +-----------+------+           |         |
    // |           |    (PaddingVL)   |           |         |
    // +-----------+------------------+-----------+---------+
    // |                      (GapVXxl)                     |
    // +----------------------------------------------------+
    //
    // Hovered, final animation state of the are above tagsLabel
    // +-----------+-------------+-----------+
    // |           | (PaddingVL) |           |
    // |           +-------------+           |
    // |           |   <title>   |           |
    // |           +-------------+           |
    // |           |  (GapVXxs)  |           |
    // |(PaddingHL)+-------------+(PaddingHL)| ...
    // |           |     <hr>    |           |
    // |           +-------------+           |
    // |           |  (GapVXxs)  |           |
    // |           +-------------+           |
    // |           |<description>|           |
    // +-----------+-------------+-----------+
    //                   ...

    const ListItem *item = index.data(ListModel::ItemRole).value<Core::ListItem *>();

    const QFont tagsLabelFont = tagsLabelTF.font();
    const QFontMetrics tagsLabelFM(tagsLabelFont);

    const QRect bgRGlobal = option.rect.adjusted(0, 0, -PaddingHXxl, -PaddingVXxl);
    const QRect bgR = bgRGlobal.translated(-option.rect.topLeft());
    const QRect thumbnailAreaR(bgR.left() + PaddingHL, bgR.top() + PaddingVL,
                               thumbnailAreaSize.width(), thumbnailAreaSize.height());
    const QRect titleR(thumbnailAreaR.left(), thumbnailAreaR.bottom() + GapVM + 1,
                       thumbnailAreaR.width(), titleTF.lineHeight());
    const QString tagsLabelText = Tr::tr("Tags:");
    const int tagsLabelTextWidth = tagsLabelFM.horizontalAdvance(tagsLabelText);
    const QRect tagsLabelR(titleR.left(), titleR.bottom() + GapVM + 1,
                           tagsLabelTextWidth, tagsTF.lineHeight());
    const QRect tagsR(tagsLabelR.right() + 1 + tagsHGap, tagsLabelR.top(),
                      bgR.right() - PaddingHL - tagsLabelR.right() - tagsHGap,
                      tagsLabelR.height()
                          + (tagsLabelR.height() + PaddingVXxs) * (tagsRowsCount - 1));
    QTC_CHECK(option.rect.height() == tagsR.bottom() + 1 + GapVL + PaddingVXxl);
    QTC_CHECK(option.rect.width() == tagsR.right() + 1 + GapHL + PaddingHXxl);

    QTextOption wrapTO;
    wrapTO.setWrapMode(QTextOption::WordWrap);

    const bool hovered = option.state & QStyle::State_MouseOver;

    painter->save();
    painter->translate(bgRGlobal.topLeft());

    const QColor fill(themeColor(hovered ? cardHoverBackground : cardDefaultBackground));
    const QPen pen(themeColor(hovered ? cardHoverStroke : cardDefaultStroke), itemOutlineWidth);
    StyleHelper::drawCardBg(painter, bgR, fill, pen, itemCornerRounding);

    const int shiftY = thumbnailAreaR.bottom();
    int offset = 0;
    qreal animationProgress = 0; // Linear increase from 0.0 to 1.0 during hover animation
    if (hovered) {
        if (index != m_previousIndex) {
            m_previousIndex = index;
            m_currentTagRects.clear();
            m_blurredThumbnail = QPixmap();
            m_startTime.start();
            m_currentWidget = qobject_cast<QAbstractItemView *>(
                const_cast<QWidget *>(option.widget));
        }
        animationProgress = qreal(m_startTime.elapsed()) / hoverDuration.count();
        if (animationProgress < 1) {
            static const QEasingCurve animationCurve(hoverEasing);
            offset = animationCurve.valueForProgress(animationProgress) * shiftY;
            using namespace std::chrono_literals;
            QTimer::singleShot(10ms, this, &ListItemDelegate::goon);
        } else {
            offset = shiftY;
        }
    } else if (index == m_previousIndex) {
        m_previousIndex = QModelIndex();
    }

    // The pixmap.
    const QPixmap pm = index.data(ListModel::ItemImageRole).value<QPixmap>();
    QPoint thumbnailPos = thumbnailAreaR.center();
    if (!pm.isNull()) {
        painter->fillRect(thumbnailAreaR, themeColor(Theme::Token_Background_Default));

        thumbnailPos.rx() -= pm.width() / pm.devicePixelRatio() / 2 - 1;
        thumbnailPos.ry() -= pm.height() / pm.devicePixelRatio() / 2 - 1;
        painter->drawPixmap(thumbnailPos, pm);

        painter->setPen(titleTF.color());
        drawPixmapOverlay(item, painter, option, thumbnailAreaR);
    } else {
        // The description text as fallback.
        painter->setPen(descriptionTF.color());
        painter->setFont(descriptionTF.font());
        painter->drawText(thumbnailAreaR, item->description, wrapTO);
    }

    // The description background
    QRect backgroundPortionR = bgR;
    if (offset) {
        backgroundPortionR.setTop(shiftY - offset);
        if (!pm.isNull()) {
            if (m_blurredThumbnail.isNull()) {
                constexpr int filterMargin = hoverBlurRadius;
                QImage thumbnail(bgR.size() + QSize(filterMargin, filterMargin) * 2,
                                 QImage::Format_ARGB32_Premultiplied);
                thumbnail.fill(themeColor(cardHoverBackground));
                QPainter thumbnailPainter(&thumbnail);
                thumbnailPainter.translate(filterMargin, filterMargin);
                thumbnailPainter.fillRect(thumbnailAreaR,
                                          themeColor(Theme::Token_Background_Default));
                thumbnailPainter.drawPixmap(thumbnailPos, pm);
                thumbnailPainter.setPen(titleTF.color());
                drawPixmapOverlay(item, &thumbnailPainter, option, thumbnailAreaR);
                thumbnailPainter.setOpacity(1.0 - hoverBlurOpacity);
                thumbnailPainter.fillRect(thumbnail.rect(), themeColor(cardHoverBackground));
                thumbnailPainter.end();
                qt_blurImage(thumbnail, hoverBlurRadius, false, false);

                QImage mask(thumbnail.size(), QImage::Format_Grayscale8);
                mask.fill(Qt::black);
                QPainter maskPainter(&mask);
                const QRect maskR = bgR.translated(filterMargin, filterMargin)
                                        .adjusted(1, 1, -1, -1);
                StyleHelper::drawCardBg(&maskPainter, maskR, Qt::white, Qt::NoPen,
                                        itemCornerRounding);
                thumbnail.setAlphaChannel(mask);

                m_blurredThumbnail = QPixmap::fromImage(
                    thumbnail.copy({filterMargin, filterMargin, bgR.width(), bgR.height()}));
            }
            const QPixmap thumbnailPortionPM = m_blurredThumbnail.copy(backgroundPortionR);
            painter->drawPixmap(backgroundPortionR.topLeft(), thumbnailPortionPM);
        } else {
            painter->fillRect(thumbnailAreaR, themeColor(cardHoverBackground));
        }
    }

    // The description Text (unhovered or hovered)
    painter->setPen(titleTF.color());
    painter->setFont(titleTF.font());
    if (offset) {
        // The title of the example
        const QRect shiftedTitleR = thumbnailAreaR.translated(backgroundPortionR.topLeft());
        const QRect titleR = painter->boundingRect(shiftedTitleR, item->name, wrapTO).toRect();
        painter->drawText(shiftedTitleR, item->name, wrapTO);

        painter->setOpacity(animationProgress); // "fade in" separator line and description

        // The separator line below the example title.
        const QRect hrR(titleR.x(), titleR.bottom() + 1 + GapVXxs, thumbnailAreaR.width(), 1);
        painter->fillRect(hrR, themeColor(Theme::Token_Stroke_Muted));

        // The description text.
        const QRect descriptionR(hrR.x(), hrR.bottom() + 1 + GapVXxs,
                                 thumbnailAreaR.width(), shiftY);
        painter->setPen(descriptionTF.color());
        painter->setFont(descriptionTF.font());
        painter->drawText(descriptionR, item->description, wrapTO);
        painter->setOpacity(1);
    } else {
        // The title of the example
        const QString elidedName = painter->fontMetrics()
                                       .elidedText(item->name, Qt::ElideRight, titleR.width());
        painter->drawText(titleR, titleTF.drawTextFlags, elidedName);
    }

    // The 'Tags:' section
    if (!item->tags.empty()) {
        painter->setPen(tagsLabelTF.color());
        painter->setFont(tagsLabelTF.font());
        painter->drawText(tagsLabelR, tagsLabelTF.drawTextFlags, tagsLabelText);

        const QFontMetrics fm = painter->fontMetrics();

        painter->setPen(tagsTF.color());
        painter->setFont(tagsTF.font());
        int emptyTagRowsLeft = tagsRowsCount;
        int xx = 0;
        int yy = 0;
        const bool populateTagsRects = m_currentTagRects.empty();
        for (const QString &tag : item->tags) {
            const int ww = fm.horizontalAdvance(tag);
            if (xx + ww > tagsR.width()) {
                if (--emptyTagRowsLeft == 0)
                    break;
                yy += fm.lineSpacing();
                xx = 0;
            }
            const QRect tagRect = QRect(xx, yy, ww, tagsLabelR.height())
                                      .translated(tagsR.topLeft());
            painter->drawText(tagRect, tagsTF.drawTextFlags, tag);
            if (populateTagsRects) {
                constexpr int grow = tagsHGap / 2;
                const QRect tagMouseArea = tagRect.adjusted(-grow, -grow, grow, grow);
                m_currentTagRects.append({ tag, tagMouseArea });
            }
            xx += ww + tagsHGap;
        }
    }

    painter->restore();
}

bool ListItemDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                   const QStyleOptionViewItem &option, const QModelIndex &index)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        const ListItem *item = index.data(ListModel::ItemRole).value<ListItem *>();
        if (!item)
            return false;
        auto mev = static_cast<QMouseEvent *>(event);

        if (mev->button() != Qt::LeftButton) // do not react on right click
            return false;

        if (index.isValid()) {
            const QPoint mousePos = mev->pos() - option.rect.topLeft();
            const auto tagUnderMouse =
                    Utils::findOrDefault(m_currentTagRects,
                                         [&mousePos](const QPair<QString, QRect> &tag) {
                return tag.second.contains(mousePos);
            });
            if (!tagUnderMouse.first.isEmpty())
                emit tagClicked(tagUnderMouse.first);
            else
                clickAction(item);
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

QSize ListItemDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    return itemSize();
}

void ListItemDelegate::drawPixmapOverlay(const ListItem *, QPainter *,
                                         const QStyleOptionViewItem &, const QRect &) const
{
}

void ListItemDelegate::clickAction(const ListItem *) const
{
}

void ListItemDelegate::goon()
{
    if (m_currentWidget)
        m_currentWidget->update(m_previousIndex);
}

SectionedGridView::SectionedGridView(QWidget *parent)
    : QStackedWidget(parent)
{
    using namespace std::chrono_literals;
    m_searchTimer.setInterval(320ms);
    m_searchTimer.setSingleShot(true);
    connect(&m_searchTimer, &QTimer::timeout, this, [this] {
        setSearchString(m_delayedSearchString);
        m_delayedSearchString.clear();
    });

    m_allItemsModel.reset(new ListModel);
    m_allItemsModel->setPixmapFunction(m_pixmapFunction);

    auto area = new QScrollArea(this);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidgetResizable(true);

    auto sectionedView = new QWidget;
    auto layout = new QVBoxLayout;
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch(1);
    sectionedView->setLayout(layout);
    area->setWidget(sectionedView);

    addWidget(area);
}

SectionedGridView::~SectionedGridView()
{
    clear();
}

void SectionedGridView::setItemDelegate(QAbstractItemDelegate *delegate)
{
    m_itemDelegate = delegate;
    if (m_allItemsView)
        m_allItemsView->setItemDelegate(delegate);
    for (GridView *view : std::as_const(m_gridViews))
        view->setItemDelegate(delegate);
}

void SectionedGridView::setPixmapFunction(const Core::ListModel::PixmapFunction &pixmapFunction)
{
    m_pixmapFunction = pixmapFunction;
    m_allItemsModel->setPixmapFunction(pixmapFunction);
    for (ListModel *model : std::as_const(m_sectionModels))
        model->setPixmapFunction(pixmapFunction);
}

void SectionedGridView::setSearchStringDelayed(const QString &searchString)
{
    m_delayedSearchString = searchString;
    m_searchTimer.start();
}

void SectionedGridView::setSearchString(const QString &searchString)
{
    if (searchString.isEmpty()) {
        // back to previous view
        m_allItemsView.reset();
        if (m_zoomedInWidget)
            setCurrentWidget(m_zoomedInWidget);
        else
            setCurrentIndex(0);
        return;
    }
    if (!m_allItemsView) {
        // We don't have a grid set for searching yet.
        // Create all items view for filtering.
        m_allItemsView.reset(new GridView);
        m_allItemsView->setObjectName("AllItemsView"); // used by Squish
        m_allItemsView->setModel(new ListModelFilter(m_allItemsModel.get(), m_allItemsView.get()));
        if (m_itemDelegate)
            m_allItemsView->setItemDelegate(m_itemDelegate);
        addWidget(m_allItemsView.get());
    }
    setCurrentWidget(m_allItemsView.get());
    auto filterModel = static_cast<ListModelFilter *>(m_allItemsView.get()->model());
    filterModel->setSearchString(searchString);
}

static QLabel *createTitleLabel(const QString &text)
{
    constexpr TextFormat headerTitleTF {Theme::Token_Text_Muted, StyleHelper::UiElementH4};
    auto label = new ElidingLabel(text);
    applyTf(label, headerTitleTF);
    label->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    return label;
}

static QLabel *createLinkLabel(const QString &text, QWidget *parent)
{
    constexpr TextFormat headerLinkTF {Theme::Token_Text_Accent, StyleHelper::UiElementH6};
    const QString linkColor = themeColor(headerLinkTF.themeColor).name();
    auto link = new QLabel("<a href=\"link\" style=\"color: " + linkColor + ";\">"
                               + text + "</a>", parent);
    link->setFont(headerLinkTF.font());
    return link;
}

ListModel *SectionedGridView::addSection(const Section &section, const QList<ListItem *> &items)
{
    auto model = new ListModel(this);
    model->setPixmapFunction(m_pixmapFunction);
    // the sections only keep a weak reference to the items,
    // they are owned by the allProducts model, since multiple sections can contain duplicates
    // of the same item
    model->setOwnsItems(false);
    model->appendItems(items);

    auto gridView = new SectionGridView(this);
    gridView->setItemDelegate(m_itemDelegate);
    gridView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    gridView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    gridView->setModel(model);
    gridView->setMaxRows(section.maxRows);

    m_sectionModels.insert(section, model);
    const auto it = m_gridViews.insert(section, gridView);

    constexpr TextFormat headerTitleTF {Theme::Token_Text_Muted, StyleHelper::UiElementH4};
    auto sectionNameLabel = new ElidingLabel(section.name);
    applyTf(sectionNameLabel, headerTitleTF);

    QLabel *seeAllLink = createLinkLabel(Tr::tr("Show All") + " &gt;", this);
    if (gridView->maxRows().has_value()) {
        seeAllLink->setVisible(true);
        connect(gridView, &SectionGridView::itemsFitChanged, seeAllLink, [seeAllLink](bool fits) {
            seeAllLink->setVisible(!fits);
        });
    } else {
        seeAllLink->setVisible(false);
    }
    connect(seeAllLink, &QLabel::linkActivated, this, [this, section] { zoomInSection(section); });
    using namespace Layouting;
    QWidget *sectionLabel = Row {
        createTitleLabel(section.name),
        st,
        seeAllLink,
        Space(GapHXxl),
        customMargins(0, PaddingVS, 0, PaddingVS),
    }.emerge();
    m_sectionLabels.append(sectionLabel);
    auto scrollArea = qobject_cast<QScrollArea *>(widget(0));
    auto vbox = qobject_cast<QVBoxLayout *>(scrollArea->widget()->layout());

    // insert new section depending on its priority, but before the last (stretch) item
    int position = std::distance(m_gridViews.begin(), it) * 2; // a section has a label and a grid
    QTC_ASSERT(position <= vbox->count() - 1, position = vbox->count() - 1);
    vbox->insertWidget(position, sectionLabel);
    vbox->insertWidget(position + 1, gridView);

    struct ItemHash
    {
        std::size_t operator()(ListItem *item) const { return std::hash<QString>{}(item->name); }
    };
    struct ItemEqual
    {
        bool operator()(ListItem *lhs, ListItem *rhs) const
        {
            return lhs->name == rhs->name && lhs->description == rhs->description;
        }
    };

    // add the items also to the all products model to be able to search correctly
    const QList<ListItem *> allItems = m_allItemsModel->items();
    const std::unordered_set<ListItem *, ItemHash, ItemEqual> uniqueItems{allItems.constBegin(),
                                                                          allItems.constEnd()};
    const QList<ListItem *> newItems = filtered(items, [&uniqueItems](ListItem *item) {
        return uniqueItems.find(item) == uniqueItems.end();
    });
    m_allItemsModel->appendItems(newItems);

    // only show section label(s) if there is more than one section
    m_sectionLabels.at(0)->setVisible(m_sectionLabels.size() > 1);

    return model;
}

void SectionedGridView::clear()
{
    m_allItemsModel->clear();
    qDeleteAll(m_sectionModels);
    qDeleteAll(m_sectionLabels);
    qDeleteAll(m_gridViews);
    m_sectionModels.clear();
    m_sectionLabels.clear();
    m_gridViews.clear();
    m_allItemsView.reset();
}

void SectionedGridView::zoomInSection(const Section &section)
{
    auto zoomedInWidget = new QWidget(this);
    auto layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    zoomedInWidget->setLayout(layout);

    QLabel *backLink = createLinkLabel("&lt; " + Tr::tr("Back"), this);
    connect(backLink, &QLabel::linkActivated, this, [this, zoomedInWidget] {
        removeWidget(zoomedInWidget);
        delete zoomedInWidget;
        setCurrentIndex(0);
    });
    using namespace Layouting;
    QWidget *sectionLabel = Row {
        createTitleLabel(section.name),
        st,
        backLink,
        Space(GapHXxl),
        customMargins(0, PaddingVS, 0, PaddingVS),
    }.emerge();

    auto gridView = new GridView(zoomedInWidget);
    gridView->setItemDelegate(m_itemDelegate);
    gridView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    gridView->setModel(m_sectionModels.value(section));

    layout->addWidget(sectionLabel);
    layout->addWidget(gridView);

    m_zoomedInWidget = zoomedInWidget;
    addWidget(zoomedInWidget);
    setCurrentWidget(zoomedInWidget);
}

Section::Section(const QString &name, int priority)
    : name(name)
    , priority(priority)
{}

Section::Section(const QString &name, int priority, std::optional<int> maxRows)
    : name(name)
    , priority(priority)
    , maxRows(maxRows)
{}

ResizeSignallingWidget::ResizeSignallingWidget(QWidget *parent)
    : QWidget(parent)
{
}

void ResizeSignallingWidget::resizeEvent(QResizeEvent *event)
{
    emit resized(event->size(), event->oldSize());
}

} // namespace Core
