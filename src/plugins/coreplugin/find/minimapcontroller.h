// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../core_global.h"

#include <QPointer>

QT_BEGIN_NAMESPACE
class QAbstractScrollArea;
class QScrollBar;
class QTextBlock;
QT_END_NAMESPACE

namespace Core {

class MinimapOverlay;

class CORE_EXPORT MinimapController
{
public:
    MinimapController() = default;
    ~MinimapController();

    QScrollBar *scrollBar() const;
    QAbstractScrollArea *scrollArea() const;
    void setScrollArea(QAbstractScrollArea *scrollArea);

    void setOverrideBlockColorFunction(
        const std::function<std::optional<QColor>(const QTextBlock &)> &func);

private:
    QAbstractScrollArea *m_scrollArea = nullptr;
    QPointer<MinimapOverlay> m_minimap;
    std::function<std::optional<QColor> (const QTextBlock &)> m_overrideBlockColor;
};

} // namespace Core
