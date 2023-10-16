// Copyright (C) 2023 Tasuku Suzuki <tasuku.suzuki@signal-slot.co.jp>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "guiutils.h"

#include <QEvent>
#include <QWidget>

namespace Utils {

namespace Internal {

class WheelEventFilter : public QObject
{
public:
    bool eventFilter(QObject *watched, QEvent *event) override {
        QWidget *widget = qobject_cast<QWidget *>(watched);
        if (widget) {
            QObject *parent = widget->parentWidget();
            const bool propagateToParent = parent && event->type() == QEvent::Wheel
                                           && widget->focusPolicy() != Qt::WheelFocus
                                           && !widget->hasFocus();
            if (propagateToParent)
                return parent->event(event);
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace Internal

void QTCREATOR_UTILS_EXPORT attachWheelBlocker(QWidget *widget)
{
    static Internal::WheelEventFilter instance;
    widget->installEventFilter(&instance);
    widget->setFocusPolicy(Qt::StrongFocus);
}

} // namespace Utils
