// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "../3rdparty/tl_expected/include/tl/expected.hpp"

namespace Utils {

template<class T, class E>
using expected = tl::expected<T, E>;

template<class E>
using unexpected = tl::unexpected<E>;
using unexpect_t = tl::unexpect_t;

static constexpr unexpect_t unexpect{};

template<class E>
constexpr unexpected<std::decay_t<E>> make_unexpected(E &&e)
{
    return tl::make_unexpected(e);
}

} // namespace Utils

/// Assigns the expected value or returns the error.
#define TRY(expr) \
    ({ \
        auto _temporary_result = (expr); \
        if (!_temporary_result) \
            return make_unexpected(_temporary_result.error()); \
        _temporary_result; \
    })

/// Assigns the expected value, and executes the error_action if the result is an error.
#define QTC_TRY(expr, error_action) \
    ({ \
        auto _temporary_result = (expr); \
        if (!_temporary_result) { \
            qCritical().nospace() << __FILE__ << ":" << __LINE__ << " : " \
                                  << _temporary_result.error(); \
            error_action; \
        } \
        _temporary_result; \
    })

/// Assigns either the value from the expected, or "or_value" value if the result is an error.
#define QTC_TRY_OR(expr, or_value) \
    ({ \
        auto _temporary_result = (expr); \
        if (!_temporary_result) { \
            qCritical().nospace() << __FILE__ << ":" << __LINE__ << " : " << expr.error(); \
        } \
        _temporary_result.value_or(or_value); \
    })

/// Adds more info to an error if the expected failed.
#define QTC_ADD_ERROR(error_string) \
    map_error([](const auto &error) -> QString { \
        return QString("%1:%2 : %3:\n\t %4") \
            .arg(__FILE__) \
            .arg(__LINE__) \
            .arg(error_string) \
            .arg(error); \
    })

#define FAILURE(error_message) \
    make_unexpected(QString("%1:%2 : %3").arg(__FILE__).arg(__LINE__).arg(error_message))

#define RETURN_FAILURE(error_message) return FAILURE(error_message)
