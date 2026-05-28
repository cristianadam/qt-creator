// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "tracing_global.h"

#include <QWidget>

namespace Timeline {

class TRACING_EXPORT TimeRuler : public QWidget
{
    Q_OBJECT
public:
    explicit TimeRuler(QWidget *parent = nullptr);

    void setRange(qint64 rangeStart, qint64 rangeEnd);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    qint64 m_rangeStart = 0;
    qint64 m_rangeEnd = 0;
};

} // namespace Timeline
