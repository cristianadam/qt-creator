// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "tracing_global.h"

#include <QColor>
#include <QList>
#include <QString>
#include <QWidget>

namespace Timeline {

struct TrackInfo
{
    QString name;
    QColor color;         // category accent strip color
    bool expanded = false;
    QStringList rowLabels; // sub-row names when expanded (empty when collapsed)
    QList<int> rowHeights; // rowHeights[0] = title row; [1..] = sub-rows
};

class TRACING_EXPORT TrackLabels : public QWidget
{
    Q_OBJECT
public:
    explicit TrackLabels(QWidget *parent = nullptr);

    void setTracks(const QList<TrackInfo> &tracks);
    void setScrollOffset(int y);

    int scrollOffset() const { return m_scrollOffset; }

    QSize sizeHint() const override;

signals:
    void expandToggled(int trackIndex);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    QList<TrackInfo> m_tracks;
    int m_scrollOffset = 0;
    int m_totalHeight = 0;

    void updateTotalHeight();
};

} // namespace Timeline
