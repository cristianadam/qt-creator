// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "timelinecontentwidget.h"

#include "timelinemodel.h"
#include "timelinemodelaggregator.h"

#include <QVariantMap>
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
        m_trackLayout->addWidget(painter);
        m_sync->registerContent(painter);
        m_painters.append(painter);
        const int modelIndex = m_painters.size() - 1;
        connect(painter, &TrackPainter::itemClicked, this,
                [this, modelIndex](int itemIndex) { selectItem(modelIndex, itemIndex); });
        connect(painter, &TrackPainter::itemHovered, this,
                [this, modelIndex](int itemIndex) { onItemHovered(modelIndex, itemIndex); });

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

void TimelineContentWidget::onItemHovered(int modelIndex, int itemIndex)
{
    if (m_hoveredModelIndex == modelIndex && m_hoveredItemIndex == itemIndex)
        return;
    m_hoveredModelIndex = modelIndex;
    m_hoveredItemIndex = itemIndex;
    emit itemHovered(modelIndex, itemIndex);
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
}

} // namespace Timeline
