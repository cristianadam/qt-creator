// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "tracing_global.h"

#include <QList>
#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QScrollArea;
class QVBoxLayout;
QT_END_NAMESPACE

namespace Timeline {

class TimelineModelAggregator;
class TimelineZoomControl;
class TimelineScrollSync;
class TimeRuler;
class TrackLabels;
class TrackPainter;

class TRACING_EXPORT TimelineContentWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TimelineContentWidget(TimelineModelAggregator *aggregator,
                                   TimelineZoomControl *zoom,
                                   QWidget *parent = nullptr);

    QSize sizeHint() const override;

    QString currentFile() const { return m_currentFile; }
    int currentLine() const { return m_currentLine; }
    int currentColumn() const { return m_currentColumn; }
    int currentTypeId() const { return m_currentTypeId; }

    int hoveredModelIndex() const { return m_hoveredModelIndex; }
    int hoveredItemIndex() const { return m_hoveredItemIndex; }

signals:
    void itemHovered(int modelIndex, int itemIndex);

private:
    void rebuildTracks();
    void selectItem(int modelIndex, int itemIndex);
    void onItemHovered(int modelIndex, int itemIndex);

    TimelineModelAggregator *m_aggregator;
    TimelineZoomControl *m_zoom;
    TimelineScrollSync *m_sync;
    TimeRuler *m_ruler;
    TrackLabels *m_labels;
    QScrollArea *m_scrollArea;
    QWidget *m_trackContainer;
    QVBoxLayout *m_trackLayout;
    QList<TrackPainter *> m_painters;

    int m_selectedModelIndex = -1;
    int m_selectedItemIndex = -1;
    QString m_currentFile;
    int m_currentLine = -1;
    int m_currentColumn = 0;
    int m_currentTypeId = -1;

    int m_hoveredModelIndex = -1;
    int m_hoveredItemIndex = -1;
};

} // namespace Timeline
