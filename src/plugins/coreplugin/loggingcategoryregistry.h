// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "core_global.h"

#include <QLoggingCategory>
#include <QObject>

namespace Core::Internal {

class LoggingCategoryEntry;

class CORE_EXPORT LoggingCategoryRegistry : public QObject
{
    Q_OBJECT
public:
    QList<QLoggingCategory *> categories();

    virtual void start() = 0;

    virtual void updateCategory(const LoggingCategoryEntry &category) {}

signals:
    void newLogCategory(QLoggingCategory *category);

protected:
    LoggingCategoryRegistry() = default;

    virtual void onFilter(QLoggingCategory *category) = 0;

    QList<QLoggingCategory *> m_categories;
};

} // namespace Core::Internal
