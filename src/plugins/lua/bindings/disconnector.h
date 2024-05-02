// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QDebug>
#include <QList>
#include <QObject>
#include <QPointer>

namespace Lua {

class Disconnector final
{
    std::unique_ptr<QObject> m_guard = std::make_unique<QObject>();

public:
    Disconnector() = default;
    Disconnector(const Disconnector &){};

    QObject *guard() const { return m_guard.get(); }
    operator QObject *() const { return guard(); }
};

} // namespace Lua