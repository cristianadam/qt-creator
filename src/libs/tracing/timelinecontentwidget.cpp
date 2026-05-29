// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "timelinecontentwidget.h"

#include "rangedetailswidget.h"
#include "selectionrangeoverlay.h"
#include "timelinemodel.h"
#include "timelinemodelaggregator.h"
#include "timelinenotesmodel.h"
#include "timelinescrollsync.h"
#include "timelinezoomcontrol.h"
#include "timeruler.h"
#include "tracklabels.h"
#include "trackpainter.h"

#include <utils/theme/theme.h>

#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QSplitterHandle>
#include <QVariantMap>
#include <QVBoxLayout>

#include <cmath>

namespace Timeline {

class StripedBackground : public QWidget
{
public:
    explicit StripedBackground(QWidget *parent = nullptr) : QWidget(parent) {}

protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto *theme = Utils::creatorTheme();
        if (!theme)
            return;
        const QColor bg1 = theme->color(Utils::Theme::Timeline_BackgroundColor1);
        const QColor bg2 = theme->color(Utils::Theme::Timeline_BackgroundColor2);
        const int rowH = TimelineModel::defaultRowHeight();
        QPainter p(this);
        for (int y = 0, row = 0; y < height(); y += rowH, ++row)
            p.fillRect(0, y, width(), rowH, (row % 2 == 0) ? bg1 : bg2);
    }
};

class DividerHandle : public QSplitterHandle
{
public:
    DividerHandle(Qt::Orientation orientation, QSplitter *parent)
        : QSplitterHandle(orientation, parent) {}

protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto *theme = Utils::creatorTheme();
        if (!theme)
            return;
        QPainter p(this);
        p.fillRect(rect(), theme->color(Utils::Theme::Timeline_DividerColor));
    }
};

class DividerSplitter : public QSplitter
{
public:
    explicit DividerSplitter(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSplitter(orientation, parent) {}

protected:
    QSplitterHandle *createHandle() override
    {
        return new DividerHandle(orientation(), this);
    }
};

TimelineContentWidget::TimelineContentWidget(TimelineModelAggregator *aggregator,
                                             TimelineZoomControl *zoom,
                                             QWidget *parent)
    : QWidget(parent)
    , m_aggregator(aggregator)
    , m_zoom(zoom)
    , m_sync(new TimelineScrollSync(zoom, this))
{
    m_ruler = new TimeRuler;
    m_sync->registerRuler(m_ruler);

    m_labels = new TrackLabels;

    m_trackContainer = new StripedBackground;
    m_trackLayout = new QVBoxLayout(m_trackContainer);
    m_trackLayout->setContentsMargins(0, 0, 0, 0);
    m_trackLayout->setSpacing(0);

    m_scrollArea = new QScrollArea;
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidget(m_trackContainer);

    m_overlay = new SelectionRangeOverlay(zoom, m_scrollArea->viewport());
    m_overlay->resize(m_scrollArea->viewport()->size());
    m_overlay->raise();
    m_scrollArea->viewport()->installEventFilter(this);

    m_details = new RangeDetailsWidget(this);
    m_details->move(200, 25);
    connect(m_details, &RangeDetailsWidget::lockChanged,
            this, &TimelineContentWidget::setSelectionLocked);
    connect(m_details, &RangeDetailsWidget::noteChanged, this, [this](const QString &text) {
        if (m_selectedModelIndex < 0 || m_selectedItemIndex < 0)
            return;
        if (TimelineNotesModel *notes = m_aggregator->notes()) {
            const int modelId = m_painters[m_selectedModelIndex]->model()->modelId();
            notes->setText(modelId, m_selectedItemIndex, text);
        }
    });

    m_sync->registerLabels(m_labels);
    m_sync->setVerticalScrollBar(m_scrollArea->verticalScrollBar());

    // Left panel: header placeholder matching ruler height, then labels below
    const int rulerH = m_ruler->sizeHint().height();
    m_leftPanel = new QWidget;
    m_leftPanel->setMinimumWidth(m_labels->sizeHint().width());
    m_leftLayout = new QVBoxLayout(m_leftPanel);
    m_leftLayout->setContentsMargins(0, 0, 0, 0);
    m_leftLayout->setSpacing(0);
    m_leftHeader = new QWidget;
    m_leftHeader->setFixedHeight(rulerH);
    m_leftLayout->addWidget(m_leftHeader);
    m_leftLayout->addWidget(m_labels);

    // Right panel: ruler at top, scroll area below
    auto rightPanel = new QWidget;
    auto rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(m_ruler);
    rightLayout->addWidget(m_scrollArea, 1);

    auto splitter = new DividerSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(1);
    splitter->addWidget(m_leftPanel);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setCollapsible(0, false);
    splitter->setCollapsible(1, false);

    auto mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(splitter);

    connect(m_labels, &TrackLabels::expandToggled, this, [this](int trackIndex) {
        if (trackIndex < 0 || trackIndex >= m_painters.size())
            return;
        const int modelId = m_painters[trackIndex]->model()->modelId();
        for (const QVariant &v : m_aggregator->models()) {
            auto mutableModel = qvariant_cast<TimelineModel *>(v);
            if (mutableModel && mutableModel->modelId() == modelId) {
                mutableModel->setExpanded(!mutableModel->expanded());
                break;
            }
        }
        rebuildTracks();
    });

    connect(aggregator, &TimelineModelAggregator::modelsChanged,
            this, &TimelineContentWidget::rebuildTracks);
    connect(aggregator, &TimelineModelAggregator::notesChanged,
            this, &TimelineContentWidget::updateNotes);
    connect(m_ruler, &TimeRuler::markersChanged, this, [this](const QList<qint64> &markers) {
        for (auto painter : m_painters)
            painter->setMarkers(markers);
    });

    rebuildTracks();
}

void TimelineContentWidget::setLeftHeaderWidget(QWidget *widget)
{
    widget->setParent(m_leftPanel);
    m_leftLayout->replaceWidget(m_leftHeader, widget);
    delete m_leftHeader;
    m_leftHeader = nullptr;
    m_leftPanel->setMinimumWidth(qMax(m_leftPanel->minimumWidth(), widget->sizeHint().width()));
}

void TimelineContentWidget::addLeftPanelWidget(QWidget *widget)
{
    widget->setParent(m_leftPanel);
    m_leftLayout->insertWidget(m_leftLayout->indexOf(m_labels), widget);
}

QSize TimelineContentWidget::sizeHint() const
{
    return QSize(700, 300);
}

void TimelineContentWidget::setSelectionRangeMode(bool active)
{
    m_overlay->reset();
    m_overlay->setActive(active);
}

bool TimelineContentWidget::selectionRangeMode() const
{
    return m_overlay->isActive();
}

bool TimelineContentWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_scrollArea->viewport() && event->type() == QEvent::Resize) {
        auto *re = static_cast<QResizeEvent *>(event);
        m_overlay->resize(re->size());
    }
    return false;
}

void TimelineContentWidget::rebuildTracks()
{
    // Delete painters; Qt removes their layout items automatically on destruction
    qDeleteAll(m_painters);
    m_painters.clear();
    m_selectedModelIndex = -1;
    m_selectedItemIndex = -1;
    m_hoveredModelIndex = -1;
    m_hoveredItemIndex = -1;
    // Remove any remaining non-widget items (e.g. the trailing stretch)
    while (m_trackLayout->count() > 0)
        delete m_trackLayout->takeAt(0);

    QList<TrackInfo> tracks;
    for (const QVariant &v : m_aggregator->models()) {
        auto model = qvariant_cast<TimelineModel *>(v);
        if (!model || model->hidden())
            continue;

        auto painter = new TrackPainter(m_trackContainer);
        painter->setModel(model);
        painter->setNotes(m_aggregator->notes());
        m_trackLayout->addWidget(painter);
        m_sync->registerContent(painter);
        m_painters.append(painter);
        const int modelIndex = m_painters.size() - 1;
        connect(painter, &TrackPainter::itemClicked, this,
                [this, modelIndex](int itemIndex) { selectItem(modelIndex, itemIndex); });
        connect(painter, &TrackPainter::itemHovered, this,
                [this, modelIndex](int itemIndex) { onItemHovered(modelIndex, itemIndex); });
        connect(painter, &TrackPainter::horizontalPan, this,
                [this](int dx) { applyHorizontalPan(dx); });
        connect(painter, &TrackPainter::verticalPan, this, [this](int dy) {
            QScrollBar *vbar = m_scrollArea->verticalScrollBar();
            vbar->setValue(vbar->value() - dy);
        });
        connect(painter, &TrackPainter::zoomRequested, this,
                [this](double cursorX, int dy) { applyZoom(cursorX, dy); });

        TrackInfo info;
        info.name = model->displayName();
        info.color = model->categoryColor();
        info.expanded = model->expanded();
        for (int row = 0; row < model->rowCount(); ++row)
            info.rowHeights.append(model->rowHeight(row));
        if (model->expanded()) {
            for (const QVariant &label : model->labels())
                info.rowLabels.append(label.toMap().value("description").toString());
        }
        tracks.append(info);
    }
    m_trackLayout->addStretch(1);
    m_labels->setTracks(tracks);
}

void TimelineContentWidget::updateNotes()
{
    for (auto painter : m_painters)
        painter->setNotes(m_aggregator->notes());
}

void TimelineContentWidget::onItemHovered(int modelIndex, int itemIndex)
{
    if (m_hoveredModelIndex == modelIndex && m_hoveredItemIndex == itemIndex)
        return;
    m_hoveredModelIndex = modelIndex;
    m_hoveredItemIndex = itemIndex;
    emit itemHovered(modelIndex, itemIndex);

    if (!m_details->locked()) {
        if (itemIndex >= 0)
            showItemDetails(modelIndex, itemIndex);
        else
            showItemDetails(m_selectedModelIndex, m_selectedItemIndex);
    }
}

void TimelineContentWidget::selectItem(int modelIndex, int itemIndex)
{
    // Clear the previously selected painter if it differs from the new one
    if (m_selectedModelIndex != modelIndex
            && m_selectedModelIndex >= 0
            && m_selectedModelIndex < m_painters.size()) {
        m_painters[m_selectedModelIndex]->setSelectedItem(-1);
    }

    m_selectedModelIndex = modelIndex;
    m_selectedItemIndex = itemIndex;

    if (modelIndex >= 0 && modelIndex < m_painters.size())
        m_painters[modelIndex]->setSelectedItem(itemIndex);

    if (modelIndex >= 0 && modelIndex < m_painters.size() && itemIndex >= 0) {
        const TimelineModel *model = m_painters[modelIndex]->model();
        m_currentTypeId = model->typeId(itemIndex);
        const QVariantMap loc = model->location(itemIndex);
        m_currentFile = loc.value("file").toString();
        m_currentLine = loc.value("line", -1).toInt();
        m_currentColumn = loc.value("column", 0).toInt();
        emit m_aggregator->updateCursorPosition();
    } else {
        m_currentTypeId = -1;
        m_currentFile.clear();
        m_currentLine = -1;
        m_currentColumn = 0;
    }

    recenterOnItem(modelIndex, itemIndex);
    showItemDetails(modelIndex, itemIndex);
}

void TimelineContentWidget::applyHorizontalPan(int dx)
{
    if (m_painters.isEmpty() || dx == 0)
        return;
    const TrackPainter *painter = m_painters.first();
    const int viewW = painter->width();
    if (viewW <= 0)
        return;
    const qint64 rangeDuration = m_zoom->rangeEnd() - m_zoom->rangeStart();
    const qint64 dt = qRound64(double(dx) * double(rangeDuration) / double(viewW));
    const qint64 traceStart = m_zoom->traceStart();
    const qint64 traceEnd = m_zoom->traceEnd();
    qint64 newStart = m_zoom->rangeStart() - dt;
    qint64 newEnd = m_zoom->rangeEnd() - dt;
    if (newStart < traceStart) {
        newEnd += traceStart - newStart;
        newStart = traceStart;
    }
    if (newEnd > traceEnd) {
        newStart -= newEnd - traceEnd;
        newStart = qMax(newStart, traceStart);
        newEnd = traceEnd;
    }
    m_zoom->setRange(newStart, newEnd);
}

void TimelineContentWidget::recenterOnItem(int modelIndex, int itemIndex)
{
    if (modelIndex < 0 || modelIndex >= m_painters.size() || itemIndex < 0)
        return;
    const TimelineModel *model = m_painters[modelIndex]->model();
    const qint64 startTime = model->startTime(itemIndex);
    const qint64 endTime = model->endTime(itemIndex);
    if (endTime < m_zoom->rangeStart() || startTime > m_zoom->rangeEnd()) {
        const qint64 dur = m_zoom->rangeEnd() - m_zoom->rangeStart();
        qint64 newStart = (startTime + endTime - dur) / 2;
        newStart = qBound(m_zoom->traceStart(), newStart, m_zoom->traceEnd() - dur);
        m_zoom->setRange(newStart, newStart + dur);
    }
}

void TimelineContentWidget::applyZoom(double cursorX, int dy)
{
    if (m_painters.isEmpty())
        return;
    const int viewW = m_painters.first()->width();
    if (viewW <= 0)
        return;

    // dy > 0 = scroll up = zoom in (shrink range); dy < 0 = zoom out
    const double factor = std::pow(1.2, double(-dy) / 15.0);
    const qint64 rangeDuration = m_zoom->rangeEnd() - m_zoom->rangeStart();
    const qint64 newDuration = qBound(m_zoom->minimumRangeLength(),
                                      qRound64(double(rangeDuration) * factor),
                                      m_zoom->traceDuration());

    // Keep the time under the cursor fixed
    const double cursorFraction = qBound(0.0, cursorX / double(viewW), 1.0);
    const qint64 cursorTime = m_zoom->rangeStart()
                              + qRound64(cursorFraction * double(rangeDuration));
    qint64 newStart = cursorTime - qRound64(cursorFraction * double(newDuration));
    qint64 newEnd = newStart + newDuration;

    const qint64 traceStart = m_zoom->traceStart();
    const qint64 traceEnd = m_zoom->traceEnd();
    if (newStart < traceStart) {
        newEnd += traceStart - newStart;
        newStart = traceStart;
    }
    if (newEnd > traceEnd) {
        newStart -= newEnd - traceEnd;
        newStart = qMax(newStart, traceStart);
        newEnd = traceEnd;
    }
    m_zoom->setRange(newStart, newEnd);
}

bool TimelineContentWidget::selectionRangeReady() const
{
    return m_overlay->isReady();
}

void TimelineContentWidget::clear()
{
    selectItem(-1, -1);
    m_details->clear();
}

bool TimelineContentWidget::selectionLocked() const
{
    return m_details->locked();
}

void TimelineContentWidget::setSelectionLocked(bool locked)
{
    m_details->setLocked(locked);
    for (auto painter : m_painters)
        painter->setSelectionLocked(locked);
    emit selectionLockedChanged(locked);
}

void TimelineContentWidget::showItemDetails(int modelIndex, int itemIndex)
{
    if (modelIndex >= 0 && modelIndex < m_painters.size() && itemIndex >= 0) {
        const TimelineModel *model = m_painters[modelIndex]->model();
        const QVariantMap od = model->orderedDetails(itemIndex);
        const QString title = od.value("title").toString();
        const QVariantList content = od.value("content").toList();
        QString noteText;
        if (TimelineNotesModel *notes = m_aggregator->notes()) {
            const int noteId = notes->get(model->modelId(), itemIndex);
            if (noteId >= 0)
                noteText = notes->text(noteId);
        }
        m_details->setData(title, content, noteText);
    } else {
        m_details->clear();
    }
}

} // namespace Timeline
