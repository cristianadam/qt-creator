// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "timeruler.h"

#include "timelinecoordinates.h"
#include "timelineformattime.h"

#include <utils/stylehelper.h>
#include <utils/theme/theme.h>

#include <QPainter>
#include <QPaintEvent>

#include <cmath>

namespace Timeline {

static constexpr int kInitialBlockLength = 120; // target pixels per major block
static constexpr int kTextMargin = 5;
static constexpr int kFontPixelSize = 8;

TimeRuler::TimeRuler(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void TimeRuler::setRange(qint64 rangeStart, qint64 rangeEnd)
{
    if (m_rangeStart == rangeStart && m_rangeEnd == rangeEnd)
        return;
    m_rangeStart = rangeStart;
    m_rangeEnd = rangeEnd;
    update();
}

QSize TimeRuler::sizeHint() const
{
    return QSize(200, Utils::StyleHelper::navigationWidgetHeight());
}

void TimeRuler::paintEvent(QPaintEvent *)
{
    QPainter p(this);

    const QColor bgColor = Utils::creatorTheme()
                               ? Utils::creatorTheme()->color(Utils::Theme::PanelStatusBarBackgroundColor)
                               : palette().window().color();
    p.fillRect(rect(), bgColor);

    const qint64 rangeDuration = m_rangeEnd - m_rangeStart;
    if (rangeDuration <= 0 || width() == 0)
        return;

    const double spacing = double(width()) / double(rangeDuration);

    // Snap major-block duration to the nearest power of 2 (in ns).
    const double idealTimePerBlock = kInitialBlockLength / spacing;
    const qint64 timePerBlock = qMax(qint64(1),
                                     qint64(std::pow(2.0, std::floor(std::log2(idealTimePerBlock)))));

    // Align the first block to a timePerBlock boundary at or before rangeStart.
    const qint64 alignedStart = m_rangeStart - (m_rangeStart % timePerBlock);

    const double pixelsPerBlock = double(timePerBlock) * spacing;
    const double pixelsPerSection = pixelsPerBlock / 5.0;

    const int labelsHeight = height();
    const int ticksTop = labelsHeight / 2; // minor ticks occupy bottom half

    const QColor dividerColor = Utils::creatorTheme()
                                    ? Utils::creatorTheme()->color(Utils::Theme::Timeline_DividerColor)
                                    : QColor(Qt::gray);
    const QColor textColor = Utils::creatorTheme()
                                 ? Utils::creatorTheme()->color(Utils::Theme::PanelTextColorLight)
                                 : palette().text().color();

    QFont font = p.font();
    font.setPixelSize(kFontPixelSize);
    p.setFont(font);
    p.setPen(dividerColor);

    for (qint64 t = alignedStart; ; t += timePerBlock) {
        const double x = timeToPixel(t, m_rangeStart, m_rangeEnd, double(width()));
        if (x > double(width()))
            break;

        // Label for time t, left-aligned in [x, x+pixelsPerBlock].
        if (x + pixelsPerBlock > 0.0 && x < double(width())) {
            const QString label = formatTime(t, rangeDuration);
            const QRectF labelRect(x + kTextMargin, 0,
                                   pixelsPerBlock - kTextMargin, double(labelsHeight));
            p.setPen(textColor);
            p.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, label);
            p.setPen(dividerColor);
        }

        // 4 minor ticks at 1/5..4/5 of the block.
        for (int s = 1; s <= 4; ++s) {
            const double sx = x + s * pixelsPerSection;
            if (sx >= 0.0 && sx <= double(width())) {
                const int ix = qRound(sx);
                p.drawLine(ix, ticksTop, ix, height() - 1);
            }
        }

        // Major tick at the right edge of this block (= left edge of next block).
        const double tickX = x + pixelsPerBlock;
        if (tickX >= 0.0 && tickX <= double(width())) {
            const int ix = qRound(tickX);
            p.drawLine(ix, 0, ix, height() - 1);
        }
    }
}

} // namespace Timeline
