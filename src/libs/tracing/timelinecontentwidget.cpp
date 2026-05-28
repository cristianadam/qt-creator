// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "timelinecontentwidget.h"

#include "timelinemodel.h"
#include "timelinemodelaggregator.h"
#include "timelinescrollsync.h"
#include "timelinezoomcontrol.h"
#include "timeruler.h"
#include "tracklabels.h"
#include "trackpainter.h"

#include <utils/stylehelper.h>

#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace Timeline {

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

    m_trackContainer = new QWidget;
    m_trackLayout = new QVBoxLayout(m_trackContainer);
    m_trackLayout->setContentsMargins(0, 0, 0, 0);
    m_trackLayout->setSpacing(0);

    m_scrollArea = new QScrollArea;
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidget(m_trackContainer);

    m_sync->registerLabels(m_labels);
    m_sync->setVerticalScrollBar(m_scrollArea->verticalScrollBar());

    // Left panel: spacer matching ruler height, then labels below
    const int rulerH = m_ruler->sizeHint().height();
    auto leftPanel = new QWidget;
    leftPanel->setFixedWidth(m_labels->sizeHint().width());
    auto leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    auto rulerSpacer = new QWidget;
    rulerSpacer->setFixedHeight(rulerH);
    leftLayout->addWidget(rulerSpacer);
    leftLayout->addWidget(m_labels);

    // Right panel: ruler at top, scroll area below
    auto rightPanel = new QWidget;
    auto rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(m_ruler);
    rightLayout->addWidget(m_scrollArea, 1);

    auto mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(leftPanel);
    mainLayout->addWidget(rightPanel, 1);

    connect(aggregator, &TimelineModelAggregator::modelsChanged,
            this, &TimelineContentWidget::rebuildTracks);

    rebuildTracks();
}

QSize TimelineContentWidget::sizeHint() const
{
    return QSize(700, 300);
}

void TimelineContentWidget::rebuildTracks()
{
    // Delete painters; Qt removes their layout items automatically on destruction
    qDeleteAll(m_painters);
    m_painters.clear();
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
        m_trackLayout->addWidget(painter);
        m_sync->registerContent(painter);
        m_painters.append(painter);

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

} // namespace Timeline
