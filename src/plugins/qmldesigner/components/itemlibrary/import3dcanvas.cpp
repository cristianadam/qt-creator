// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "import3dcanvas.h"

#include <QImage>
#include <QLinearGradient>
#include <QPainter>

namespace QmlDesigner {


static QImage createGradientImage(int width, int height) {
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);

    QLinearGradient gradient(0, 0, 0, height);
    gradient.setColorAt(0, QColor(0x999999));
    gradient.setColorAt(1, QColor(0x222222));

    QPainter painter(&image);
    painter.fillRect(0, 0, width, height, gradient);

    return image;
}

Import3dCanvas::Import3dCanvas(QWidget *parent)
    : QWidget(parent)
{
}

void Import3dCanvas::updateRenderImage(const QImage &img)
{
    m_image = img;
    update();
}

void Import3dCanvas::paintEvent([[maybe_unused]] QPaintEvent *e)
{
    QWidget::paintEvent(e);

    QPainter painter(this);

    if (m_image.isNull()) {
        QImage image = createGradientImage(width(), height());
        painter.drawImage(rect(), image, QRect(0, 0, image.width(), image.height()));
    } else {
        painter.drawImage(rect(), m_image, QRect(0, 0, m_image.width(), m_image.height()));
    }
}

void Import3dCanvas::resizeEvent(QResizeEvent *e)
{
    emit requestImageUpdate();
}


}
