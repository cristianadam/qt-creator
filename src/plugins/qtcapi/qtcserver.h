// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcapi_global.h"

#include <QtHttpServer/QtHttpServer>

namespace QtcApi {

class QtcServerPrivate;

class QTCAPI_EXPORT QtcServer
{
public:
    QtcServer();
    ~QtcServer();

    static QtcServer &instance();

public:
    static QUrl url();
    static QString socket();

    template<typename Rule = QHttpServerRouterRule, typename... Args>
    void route(Args &&...args)
    {
        server().route(args...);
    }

private:
    QHttpServer &server() const;

    std::unique_ptr<QtcServerPrivate> d;
};

} // namespace QtcApi
