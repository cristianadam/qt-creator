// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "tl_expected.hpp"

namespace Utils {
template<class T, class E>
using expected = tl::expected<T, E>;

template<class E>
using unexpected = tl::unexpected<E>;

} // namespace Utils

#define QTC_ADD_ERROR(error_string) \
    map_error([](const auto &error) -> QString { \
        return QString("%1:%2 : %3:\n\t %4") \
            .arg(__FILE__) \
            .arg(__LINE__) \
            .arg(error_string) \
            .arg(error); \
    })

#define QTC_TRY(expr, error_action) \
    if (!(expr)) { \
        qCritical().nospace() << __FILE__ << ":" << __LINE__ << " : " << expr.error(); \
        error_action; \
    }

#define TRY(expr) \
    ({ \
        auto _temporary_result = (expr); \
        if (!_temporary_result) \
            return unexpected<QString>(_temporary_result.error()); \
        _temporary_result; \
    })