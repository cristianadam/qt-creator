// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
#pragma once

#include <QEvent>
#include <QImage>
#include <QPointer>
#include <QWidget>

namespace QmlDesigner {

class Import3dCanvas : public QWidget
{
    Q_OBJECT

public:
    Import3dCanvas(QWidget *parent);

    void updateRenderImage(const QImage &img);

signals:
    void requestImageUpdate();

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    QImage m_image;
};

} // namespace QmlDesigner
