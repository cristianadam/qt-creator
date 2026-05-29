// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "trackpainter.h"

#include "timelinecoordinates.h"
#include "timelinemodel.h"
#include "timelinenotesmodel.h"

#include <utils/theme/theme.h>

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QWheelEvent>

namespace Timeline {

TrackPainter::TrackPainter(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void TrackPainter::setModel(TimelineModel *model)
{
    m_model = model;
    update();
}

void TrackPainter::setMarkers(const QList<qint64> &markers)
{
    m_markers = markers;
    update();
}

void TrackPainter::setNotes(const TimelineNotesModel *notes)
{
    if (m_notes == notes)
        return;
    if (m_notes)
        disconnect(m_notes, nullptr, this, nullptr);
    m_notes = notes;
    if (m_notes)
        connect(m_notes, &TimelineNotesModel::changed, this, [this] { update(); });
    update();
}

void TrackPainter::setRange(qint64 rangeStart, qint64 rangeEnd)
{
    if (m_rangeStart == rangeStart && m_rangeEnd == rangeEnd)
        return;
    m_rangeStart = rangeStart;
    m_rangeEnd = rangeEnd;
    update();
}

void TrackPainter::setSelectedItem(int index)
{
    if (m_selectedItem == index)
        return;
    m_selectedItem = index;
    update();
}

void TrackPainter::setHoveredItem(int index)
{
    if (m_hoveredItem == index)
        return;
    m_hoveredItem = index;
    update();
}

void TrackPainter::setSelectionLocked(bool locked)
{
    if (m_selectionLocked == locked)
        return;
    m_selectionLocked = locked;
    update();
}

QSize TrackPainter::sizeHint() const
{
    int h = m_model ? m_model->height() : TimelineModel::defaultRowHeight();
    return QSize(200, h);
}

static QColor themeColor(Utils::Theme::Color role)
{
    if (Utils::creatorTheme())
        return Utils::creatorTheme()->color(role);
    return QColor();
}

void TrackPainter::paintEvent(QPaintEvent *)
{
    const QColor bg1 = themeColor(Utils::Theme::Timeline_BackgroundColor1);
    const QColor bg2 = themeColor(Utils::Theme::Timeline_BackgroundColor2);

    QPainter p(this);

    if (!m_model || m_model->hidden()) {
        p.fillRect(rect(), bg1);
        return;
    }

    // Draw alternating row backgrounds
    const int rowCount = m_model->rowCount();
    for (int row = 0; row < rowCount; ++row) {
        const int rowY = m_model->rowOffset(row);
        const int rowH = m_model->rowHeight(row);
        p.fillRect(0, rowY, width(), rowH, (row % 2 == 0) ? bg1 : bg2);
    }

    const qint64 rangeDuration = m_rangeEnd - m_rangeStart;
    if (m_model->isEmpty() || rangeDuration <= 0 || width() == 0)
        return;

    const QColor selectionColor = m_selectionLocked ? QColor(96, 0, 255) : Qt::blue;
    const QColor hoverColor = Qt::white;

    // Draw vertical grid lines (same block geometry as TimeRuler)
    {
        const double scale = double(width()) / double(rangeDuration);
        const qint64 timePerBlock = rulerBlockDuration(rangeDuration, double(width()));
        const double pixelsPerBlock = double(timePerBlock) * scale;
        const double pixelsPerSection = pixelsPerBlock / 5.0;
        const qint64 alignedStart = m_rangeStart - (m_rangeStart % timePerBlock);
        const QColor gridColor = themeColor(Utils::Theme::Timeline_DividerColor);
        p.setPen(QPen(gridColor, 1));
        for (qint64 t = alignedStart; ; t += timePerBlock) {
            const double x = timeToPixel(t, m_rangeStart, m_rangeEnd, double(width()));
            if (x > double(width()))
                break;
            for (int s = 1; s <= 4; ++s) {
                const double sx = x + s * pixelsPerSection;
                if (sx >= 0.0 && sx <= double(width()))
                    p.drawLine(qRound(sx), 0, qRound(sx), height() - 1);
            }
            const double tickX = x + pixelsPerBlock;
            if (tickX >= 0.0 && tickX <= double(width()))
                p.drawLine(qRound(tickX), 0, qRound(tickX), height() - 1);
        }
    }

    // Draw events
    const int first = m_model->firstIndex(m_rangeStart);
    const int last = m_model->lastIndex(m_rangeEnd);

    if (first < 0 || last < first)
        return;

    for (int i = first; i <= last; ++i) {
        const int row = m_model->row(i);
        const int rowH = m_model->rowHeight(row);
        const int rowY = m_model->rowOffset(row);

        const double relH = m_model->relativeHeight(i);
        const double itemH = rowH * relH;
        const double itemY = rowY + rowH - itemH;

        const qint64 start = m_model->startTime(i);
        const qint64 end = m_model->endTime(i);

        double x1 = timeToPixel(start, m_rangeStart, m_rangeEnd, double(width()));
        double x2 = timeToPixel(end, m_rangeStart, m_rangeEnd, double(width()));
        if (x2 - x1 < 1.0)
            x2 = x1 + 1.0;

        // Clip to widget bounds
        if (x2 < 0.0 || x1 > double(width()))
            continue;
        x1 = qMax(x1, 0.0);
        x2 = qMin(x2, double(width()));

        const QRectF itemRect(x1, itemY, x2 - x1, itemH);

        // Fill with model color
        const QColor color = QColor::fromRgb(m_model->color(i));
        p.fillRect(itemRect, color);

        // Draw selection / hover border on top
        if (i == m_selectedItem) {
            p.setPen(QPen(selectionColor, 4));
            p.drawRect(itemRect.adjusted(2.0, 2.0, -2.0, -2.0));
        } else if (i == m_hoveredItem) {
            p.setPen(QPen(hoverColor, 1));
            p.drawRect(itemRect.adjusted(0.5, 0.5, -0.5, -0.5));
        }
    }

    // Draw time ruler marker lines — 2px vertical lines in Timeline_HandleColor
    if (!m_markers.isEmpty()) {
        const QColor handleColor = themeColor(Utils::Theme::Timeline_HandleColor);
        p.setPen(QPen(handleColor, 2));
        for (qint64 ts : m_markers) {
            const double mx = timeToPixel(ts, m_rangeStart, m_rangeEnd, double(width()));
            p.drawLine(QPointF(mx, 0), QPointF(mx, height()));
        }
    }

    // Draw note markers: exclamation mark shape matching the QSG notes shader.
    // The shader draws d < 2/3 (stick) and d > 5/6 (dot), masking the gap between them.
    if (m_notes && m_model) {
        const QColor noteColor = themeColor(Utils::Theme::Timeline_HighlightColor);
        p.setPen(QPen(noteColor, 3));
        const int modelId = m_model->modelId();
        for (int i = 0; i < m_notes->count(); ++i) {
            if (m_notes->timelineModel(i) != modelId)
                continue;
            const int idx = m_notes->timelineIndex(i);
            if (m_model->startTime(idx) > m_rangeEnd || m_model->endTime(idx) < m_rangeStart)
                continue;
            const int row = m_model->row(idx);
            const double rowH = m_model->rowHeight(row);
            const double rowY = m_model->rowOffset(row);
            const qint64 center = (m_model->startTime(idx) + m_model->endTime(idx)) / 2;
            const double cx = timeToPixel(center, m_rangeStart, m_rangeEnd, double(width()));
            const double span = 0.8 * rowH;
            const double top = rowY + 0.1 * rowH;
            const double stickEnd = top + (2.0 / 3.0) * span;
            const double dotStart = top + (5.0 / 6.0) * span;
            const double dotEnd   = top + span;
            p.drawLine(QPointF(cx, top), QPointF(cx, stickEnd));
            p.drawPoint(QPointF(cx, (dotStart + dotEnd) / 2.0));
        }
    }
}

int TrackPainter::indexAt(const QPoint &pos) const
{
    if (!m_model || m_model->isEmpty())
        return -1;
    const qint64 rangeDuration = m_rangeEnd - m_rangeStart;
    if (rangeDuration <= 0 || width() == 0)
        return -1;

    const qint64 t = pixelToTime(pos.x(), double(width()), m_rangeStart, m_rangeEnd);

    // Search around bestIndex for the smallest item that contains the cursor
    int best = -1;
    qint64 bestWidth = std::numeric_limits<qint64>::max();

    const int candidate = m_model->bestIndex(t);
    if (candidate < 0)
        return -1;

    // Check a window of items around the candidate
    const int first = m_model->firstIndex(m_rangeStart);
    const int last = m_model->lastIndex(m_rangeEnd);
    if (first < 0 || last < first)
        return -1;
    const int lo = qMax(first, candidate - 10);
    const int hi = qMin(last, candidate + 10);

    for (int i = lo; i <= hi; ++i) {
        const int row = m_model->row(i);
        const int rowH = m_model->rowHeight(row);
        const int rowY = m_model->rowOffset(row);

        const double relH = m_model->relativeHeight(i);
        const double itemH = rowH * relH;
        const double itemY = rowY + rowH - itemH;

        if (pos.y() < itemY || pos.y() >= itemY + itemH)
            continue;

        const qint64 start = m_model->startTime(i);
        const qint64 end = m_model->endTime(i);

        double x1 = timeToPixel(start, m_rangeStart, m_rangeEnd, double(width()));
        double x2 = timeToPixel(end, m_rangeStart, m_rangeEnd, double(width()));
        if (x2 - x1 < 1.0)
            x2 = x1 + 1.0;

        if (pos.x() < x1 || pos.x() >= x2)
            continue;

        const qint64 w = end - start;
        if (best == -1 || w < bestWidth) {
            best = i;
            bestWidth = w;
        }
    }

    return best;
}

void TrackPainter::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && !m_panning) {
        const int dx = event->pos().x() - m_pressPos.x();
        const int dy = event->pos().y() - m_pressPos.y();
        if (qAbs(dx) + qAbs(dy) > 4)
            m_panning = true;
    }

    if (m_panning) {
        const int dx = event->pos().x() - m_pressPos.x();
        const int dy = event->pos().y() - m_pressPos.y();
        m_pressPos = event->pos();
        if (dx != 0)
            emit horizontalPan(dx);
        if (dy != 0)
            emit verticalPan(dy);
        return;
    }

    const int idx = indexAt(event->pos());
    if (idx != m_hoveredItem) {
        m_hoveredItem = idx;
        update();
        emit itemHovered(idx);
    }
}

void TrackPainter::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->pos();
        m_panning = false;
    }
}

void TrackPainter::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (!m_panning)
            emit itemClicked(indexAt(event->pos()));
        m_panning = false;
    }
}

void TrackPainter::wheelEvent(QWheelEvent *event)
{
    const QPoint pixelDelta = event->pixelDelta();
    const QPoint angleDelta = event->angleDelta();

    if (event->modifiers() & Qt::ControlModifier) {
        const int dy = pixelDelta.y() != 0 ? pixelDelta.y() : angleDelta.y() / 8;
        if (dy == 0) {
            event->ignore();
            return;
        }
        emit zoomRequested(event->position().x(), dy);
        event->accept();
        return;
    }

    const int dx = pixelDelta.x() != 0 ? pixelDelta.x() : angleDelta.x() / 8;
    if (dx != 0) {
        emit horizontalPan(-dx);
        event->accept();
    } else {
        event->ignore();
    }
}

void TrackPainter::leaveEvent(QEvent *)
{
    if (m_hoveredItem != -1) {
        m_hoveredItem = -1;
        update();
        emit itemHovered(-1);
    }
}

} // namespace Timeline
