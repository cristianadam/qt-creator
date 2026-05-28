// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "tracing_global.h"

#include <QWidget>

namespace Timeline {

class TimelineModel;

class TRACING_EXPORT TrackPainter : public QWidget
{
    Q_OBJECT
public:
    explicit TrackPainter(QWidget *parent = nullptr);

    void setModel(TimelineModel *model);
    const TimelineModel *model() const { return m_model; }

    void setRange(qint64 rangeStart, qint64 rangeEnd);

    qint64 rangeStart() const { return m_rangeStart; }
    qint64 rangeEnd() const { return m_rangeEnd; }

    void setSelectedItem(int index);  // -1 = none
    void setHoveredItem(int index);   // -1 = none
    void setSelectionLocked(bool locked);

    QSize sizeHint() const override;

signals:
    void itemHovered(int index);
    void itemClicked(int index);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    int indexAt(const QPoint &pos) const;

    TimelineModel *m_model = nullptr;
    qint64 m_rangeStart = 0;
    qint64 m_rangeEnd = 0;
    int m_selectedItem = -1;
    int m_hoveredItem = -1;
    bool m_selectionLocked = true;
};

} // namespace Timeline
