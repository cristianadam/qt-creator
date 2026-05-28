// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "tracing_global.h"

#include <QList>
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

private:
    void rebuildTracks();

    TimelineModelAggregator *m_aggregator;
    TimelineZoomControl *m_zoom;
    TimelineScrollSync *m_sync;
    TimeRuler *m_ruler;
    TrackLabels *m_labels;
    QScrollArea *m_scrollArea;
    QWidget *m_trackContainer;
    QVBoxLayout *m_trackLayout;
    QList<TrackPainter *> m_painters;
};

} // namespace Timeline
