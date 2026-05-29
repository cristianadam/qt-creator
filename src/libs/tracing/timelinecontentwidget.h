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
class QWidget;
QT_END_NAMESPACE

namespace Timeline {

class TimelineModelAggregator;
class TimelineZoomControl;
class TimelineScrollSync;
class RangeDetailsWidget;
class SelectionRangeOverlay;
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

    void setSelectionRangeMode(bool active);
    bool selectionRangeMode() const;

    QString currentFile() const { return m_currentFile; }
    int currentLine() const { return m_currentLine; }
    int currentColumn() const { return m_currentColumn; }
    int currentTypeId() const { return m_currentTypeId; }

    int selectedModelIndex() const { return m_selectedModelIndex; }
    int selectedItemIndex() const { return m_selectedItemIndex; }

    int hoveredModelIndex() const { return m_hoveredModelIndex; }
    int hoveredItemIndex() const { return m_hoveredItemIndex; }

    void setLeftHeaderWidget(QWidget *widget);
    void addLeftPanelWidget(QWidget *widget);

    bool selectionRangeReady() const;
    void selectItem(int modelIndex, int itemIndex);
    void clear();

    bool selectionLocked() const;
    void setSelectionLocked(bool locked);

signals:
    void itemHovered(int modelIndex, int itemIndex);
    void selectionLockedChanged(bool locked);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuildTracks();
    void updateNotes();
    void applyHorizontalPan(int dx);
    void applyZoom(double cursorX, int dy);
    void recenterOnItem(int modelIndex, int itemIndex);
    void onItemHovered(int modelIndex, int itemIndex);
    void showItemDetails(int modelIndex, int itemIndex);

    TimelineModelAggregator *m_aggregator;
    TimelineZoomControl *m_zoom;
    TimelineScrollSync *m_sync;
    TimeRuler *m_ruler;
    TrackLabels *m_labels;
    QScrollArea *m_scrollArea;
    QWidget *m_trackContainer;
    QVBoxLayout *m_trackLayout;
    QWidget *m_leftPanel = nullptr;
    QVBoxLayout *m_leftLayout = nullptr;
    QWidget *m_leftHeader = nullptr;
    SelectionRangeOverlay *m_overlay;
    RangeDetailsWidget *m_details;
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
