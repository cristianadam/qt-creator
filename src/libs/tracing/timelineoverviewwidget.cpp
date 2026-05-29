// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "timelineoverviewwidget.h"

#include "timelinemodel.h"
#include "timelinemodelaggregator.h"
#include "timelinenotesmodel.h"
#include "timelinezoomcontrol.h"

#include <utils/theme/theme.h>

#include <QMouseEvent>
#include <QPainter>
#include <QPen>

namespace Timeline {

static const int HandleWidth = 7;
static const int OverviewHeight = 50;

static QColor themeColor(Utils::Theme::Color role)
{
    if (Utils::creatorTheme())
        return Utils::creatorTheme()->color(role);
    return QColor();
}

TimelineOverviewWidget::TimelineOverviewWidget(TimelineModelAggregator *aggregator,
                                               TimelineZoomControl *zoom,
                                               QWidget *parent)
    : QWidget(parent)
    , m_aggregator(aggregator)
    , m_zoom(zoom)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    connect(zoom, &TimelineZoomControl::rangeChanged, this, [this] { update(); });
    connect(zoom, &TimelineZoomControl::traceChanged, this, [this] { update(); });
    connect(aggregator, &TimelineModelAggregator::modelsChanged, this, [this] { update(); });
    connect(aggregator, &TimelineModelAggregator::notesChanged, this, [this] { update(); });
}

QSize TimelineOverviewWidget::sizeHint() const
{
    return QSize(400, OverviewHeight);
}

double TimelineOverviewWidget::timeToPixel(qint64 t) const
{
    const qint64 traceDuration = m_zoom->traceDuration();
    if (traceDuration <= 0)
        return 0;
    return double(t - m_zoom->traceStart()) * width() / double(traceDuration);
}

qint64 TimelineOverviewWidget::pixelToTime(double px) const
{
    const qint64 traceDuration = m_zoom->traceDuration();
    if (width() <= 0)
        return m_zoom->traceStart();
    return m_zoom->traceStart() + qRound64(px * double(traceDuration) / double(width()));
}

bool TimelineOverviewWidget::nearHandle(double px, double handlePx) const
{
    return px >= handlePx - HandleWidth && px <= handlePx + HandleWidth;
}

void TimelineOverviewWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), themeColor(Utils::Theme::Timeline_BackgroundColor2));

    if (m_zoom->traceDuration() <= 0)
        return;

    const QVariantList models = m_aggregator->models();
    int numModels = 0;
    for (const QVariant &v : models) {
        auto model = qvariant_cast<TimelineModel *>(v);
        if (model && !model->hidden())
            ++numModels;
    }

    if (numModels > 0) {
        const double bandH = double(height()) / double(numModels);
        int bandIdx = 0;
        for (const QVariant &v : models) {
            auto model = qvariant_cast<TimelineModel *>(v);
            if (!model || model->hidden())
                continue;

            const double bandY = bandIdx * bandH;

            const int count = model->count();
            for (int i = 0; i < count; ++i) {
                const double relH = model->relativeHeight(i);
                const double itemH = bandH * relH;
                const double itemY = bandY + bandH - itemH;

                const double x1 = timeToPixel(model->startTime(i));
                double x2 = timeToPixel(model->endTime(i));
                if (x2 - x1 < 1.0)
                    x2 = x1 + 1.0;

                if (x2 < 0 || x1 > width())
                    continue;

                p.fillRect(QRectF(qMax(x1, 0.0), itemY,
                                  qMin(x2, double(width())) - qMax(x1, 0.0), itemH),
                           QColor::fromRgb(model->color(i)));
            }

            ++bandIdx;
        }
    }

    // Note indicators: vertical bar (exclamation mark shape)
    const TimelineNotesModel *notes = m_aggregator->notes();
    if (notes && numModels > 0) {
        const QColor noteColor = themeColor(Utils::Theme::Timeline_HighlightColor);
        p.setPen(QPen(noteColor, 2));
        const double bandH = double(height()) / double(numModels);
        const double vertSpace = bandH / 7.0;
        for (int i = 0; i < notes->count(); ++i) {
            int bandIdx = 0;
            const int modelId = notes->timelineModel(i);
            TimelineModel *noteModel = nullptr;
            for (const QVariant &v : models) {
                auto model = qvariant_cast<TimelineModel *>(v);
                if (!model || model->hidden())
                    continue;
                if (model->modelId() == modelId) {
                    noteModel = model;
                    break;
                }
                ++bandIdx;
            }
            if (!noteModel)
                continue;

            const int idx = notes->timelineIndex(i);
            const qint64 mid = (noteModel->startTime(idx) + noteModel->endTime(idx)) / 2;
            const double cx = timeToPixel(mid);
            const double topY = bandIdx * bandH + vertSpace;

            p.drawLine(QPointF(cx, topY),
                       QPointF(cx, topY + vertSpace * 3));
            p.drawPoint(QPointF(cx, topY + vertSpace * 5));
        }
    }

    // Range mover overlay
    const double rangeLeft = timeToPixel(m_zoom->rangeStart());
    const double rangeRight = timeToPixel(m_zoom->rangeEnd());

    // Semi-transparent fill
    QColor rangeColor = themeColor(Utils::Theme::Timeline_RangeColor);
    rangeColor.setAlphaF(0.4);
    if (rangeRight - rangeLeft > 1.0)
        p.fillRect(QRectF(rangeLeft, 0, rangeRight - rangeLeft, height()), rangeColor);

    // Left handle
    QColor handleColor = themeColor(Utils::Theme::Timeline_HandleColor);
    if (m_leftHovered)
        handleColor = handleColor.lighter(130);
    p.fillRect(QRectF(rangeLeft - HandleWidth, 0, HandleWidth, height()), handleColor);

    // Right handle
    handleColor = themeColor(Utils::Theme::Timeline_HandleColor);
    if (m_rightHovered)
        handleColor = handleColor.lighter(130);
    p.fillRect(QRectF(rangeRight, 0, HandleWidth, height()), handleColor);
}

void TimelineOverviewWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_zoom->traceDuration() <= 0) {
        event->accept();
        return;
    }

    const double px = event->position().x();
    const double rangeLeft = timeToPixel(m_zoom->rangeStart());
    const double rangeRight = timeToPixel(m_zoom->rangeEnd());

    m_dragStartX = px;
    m_dragRangeStart = m_zoom->rangeStart();
    m_dragRangeEnd = m_zoom->rangeEnd();

    if (nearHandle(px, rangeLeft)) {
        m_dragMode = DragLeft;
    } else if (nearHandle(px, rangeRight)) {
        m_dragMode = DragRight;
    } else if (px >= rangeLeft && px <= rangeRight) {
        m_dragMode = DragRange;
    } else {
        // Jump: center the current range around the clicked position
        m_dragMode = DragRange;
        const qint64 halfRange = (m_zoom->rangeEnd() - m_zoom->rangeStart()) / 2;
        const qint64 center = pixelToTime(px);
        qint64 newStart = center - halfRange;
        qint64 newEnd = center + halfRange;
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
        m_dragRangeStart = newStart;
        m_dragRangeEnd = newEnd;
        m_dragStartX = px;
    }

    event->accept();
}

void TimelineOverviewWidget::applyDrag(double px)
{
    const double dx = px - m_dragStartX;
    const qint64 traceDuration = m_zoom->traceDuration();
    if (traceDuration <= 0 || width() <= 0)
        return;
    const qint64 dt = qRound64(dx * double(traceDuration) / double(width()));
    const qint64 traceStart = m_zoom->traceStart();
    const qint64 traceEnd = m_zoom->traceEnd();
    const qint64 minRange = m_zoom->minimumRangeLength();

    if (m_dragMode == DragLeft) {
        qint64 newStart = qBound(traceStart,
                                 m_dragRangeStart + dt,
                                 m_dragRangeEnd - minRange);
        m_zoom->setRange(newStart, m_dragRangeEnd);
    } else if (m_dragMode == DragRight) {
        qint64 newEnd = qBound(m_dragRangeStart + minRange,
                               m_dragRangeEnd + dt,
                               traceEnd);
        m_zoom->setRange(m_dragRangeStart, newEnd);
    } else if (m_dragMode == DragRange) {
        const qint64 rangeLen = m_dragRangeEnd - m_dragRangeStart;
        qint64 newStart = m_dragRangeStart + dt;
        if (newStart < traceStart)
            newStart = traceStart;
        if (newStart + rangeLen > traceEnd)
            newStart = traceEnd - rangeLen;
        m_zoom->setRange(newStart, newStart + rangeLen);
    }
}

void TimelineOverviewWidget::mouseMoveEvent(QMouseEvent *event)
{
    const double px = event->position().x();

    if (m_dragMode != DragNone) {
        applyDrag(px);
        event->accept();
        return;
    }

    // Update hover state for handles
    const double rangeLeft = timeToPixel(m_zoom->rangeStart());
    const double rangeRight = timeToPixel(m_zoom->rangeEnd());
    const bool newLeftHov = nearHandle(px, rangeLeft);
    const bool newRightHov = !newLeftHov && nearHandle(px, rangeRight);

    if (newLeftHov != m_leftHovered || newRightHov != m_rightHovered) {
        m_leftHovered = newLeftHov;
        m_rightHovered = newRightHov;
        update();
    }

    if (m_leftHovered || m_rightHovered)
        setCursor(Qt::SizeHorCursor);
    else if (px >= rangeLeft && px <= rangeRight)
        setCursor(Qt::SizeAllCursor);
    else
        setCursor(Qt::ArrowCursor);

    event->accept();
}

void TimelineOverviewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_dragMode = DragNone;
    event->accept();
}

void TimelineOverviewWidget::leaveEvent(QEvent *event)
{
    if (m_leftHovered || m_rightHovered) {
        m_leftHovered = false;
        m_rightHovered = false;
        update();
    }
    setCursor(Qt::ArrowCursor);
    QWidget::leaveEvent(event);
}

} // namespace Timeline
