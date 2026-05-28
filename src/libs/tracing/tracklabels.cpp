// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "tracklabels.h"

#include <utils/theme/theme.h>
#include <utils/utilsicons.h>

#include <QPainter>
#include <QPaintEvent>

namespace Timeline {

static constexpr int kAccentWidth = 3;
static constexpr int kTextLeftMargin = 5; // left margin after the accent strip
static constexpr int kTextRightMargin = 20; // room for the expand indicator
static constexpr int kIndicatorSize = 16;

static QColor themeColor(Utils::Theme::Color role)
{
    if (Utils::creatorTheme())
        return Utils::creatorTheme()->color(role);
    return QColor();
}

TrackLabels::TrackLabels(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

void TrackLabels::setTracks(const QList<TrackInfo> &tracks)
{
    m_tracks = tracks;
    updateTotalHeight();
    update();
}

void TrackLabels::setScrollOffset(int y)
{
    if (m_scrollOffset == y)
        return;
    m_scrollOffset = y;
    update();
}

QSize TrackLabels::sizeHint() const
{
    return QSize(162, m_totalHeight);
}

void TrackLabels::updateTotalHeight()
{
    m_totalHeight = 0;
    for (const TrackInfo &t : m_tracks) {
        for (int h : t.rowHeights)
            m_totalHeight += h;
    }
}

void TrackLabels::paintEvent(QPaintEvent *)
{
    QPainter p(this);

    const QColor bgColor = themeColor(Utils::Theme::PanelStatusBarBackgroundColor);
    const QColor dividerColor = themeColor(Utils::Theme::Timeline_DividerColor);
    const QColor textColor = themeColor(Utils::Theme::PanelTextColorLight);

    p.fillRect(rect(), bgColor);

    QFont font = p.font();
    font.setPixelSize(11);
    p.setFont(font);

    int y = -m_scrollOffset;
    bool firstTrack = true;

    for (const TrackInfo &track : m_tracks) {
        const int titleHeight = track.rowHeights.isEmpty() ? 30 : track.rowHeights[0];
        int trackHeight = 0;
        for (int h : track.rowHeights)
            trackHeight += h;

        // Skip tracks entirely above the viewport
        if (y + trackHeight <= 0) {
            y += trackHeight;
            firstTrack = false;
            continue;
        }
        // Stop once below the viewport
        if (y >= height())
            break;

        // Top divider (not for the very first track)
        if (!firstTrack) {
            p.fillRect(0, y, width(), 1, dividerColor);
        }
        firstTrack = false;

        // Left accent strip
        p.fillRect(0, y, kAccentWidth, trackHeight, track.color);

        // Title row
        const int textX = kAccentWidth + kTextLeftMargin;
        const int textW = width() - textX - kTextRightMargin;
        const QRectF titleRect(textX, y, textW, titleHeight);
        p.setPen(textColor);
        p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                   p.fontMetrics().elidedText(track.name, Qt::ElideRight, textW));

        // Expand/collapse indicator (right-aligned in title row)
        {
            const QIcon icon = track.expanded ? Utils::Icons::CLOSE_SPLIT_TOP.icon()
                                              : Utils::Icons::SPLIT_HORIZONTAL_TOOLBAR.icon();
            const int ix = width() - kTextRightMargin + (kTextRightMargin - kIndicatorSize) / 2;
            const int iy = y + (titleHeight - kIndicatorSize) / 2;
            icon.paint(&p, ix, iy, kIndicatorSize, kIndicatorSize);
        }

        // Sub-row labels (expanded state)
        if (track.expanded && track.rowLabels.size() == track.rowHeights.size() - 1) {
            int rowY = y + titleHeight;
            for (int i = 0; i < track.rowLabels.size(); ++i) {
                const int rowH = track.rowHeights[i + 1];

                // Row background + border
                p.fillRect(kAccentWidth, rowY, width() - kAccentWidth, rowH, bgColor);
                p.fillRect(kAccentWidth, rowY, width() - kAccentWidth, 1, dividerColor);
                p.fillRect(kAccentWidth, rowY, 1, rowH, dividerColor);
                p.fillRect(width() - 1, rowY, 1, rowH, dividerColor);

                // Row label text
                const int rxText = kAccentWidth + 4;
                const int rxW = width() - rxText - 2;
                const QRectF rowRect(rxText, rowY, rxW, rowH);
                p.setPen(textColor);
                p.drawText(rowRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                           p.fontMetrics().elidedText(track.rowLabels[i], Qt::ElideRight, rxW));

                rowY += rowH;
            }
        }

        y += trackHeight;
    }
}

} // namespace Timeline
