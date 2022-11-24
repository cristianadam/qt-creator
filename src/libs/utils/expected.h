// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "qtcassert.h"

#include "../3rdparty/tl_expected/include/tl/expected.hpp"

namespace Utils {

template<class T, class E>
using expected = tl::expected<T, E>;

template<class T>
using expected_str = tl::expected<T, QString>;

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

//! Assigns either the value from the expected, or "or_value" value if the result is an error.
#define QTC_EXPECT_OR(expected, action) \
    { \
        const auto tmp = expected; \
        if (Q_LIKELY(tmp)) { \
        } else { \
            ::Utils::writeAssertLocation( \
                QString("%1:%2: %3").arg(__FILE__).arg(__LINE__).arg(tmp.error()).toUtf8().data()); \
            action; \
        } \
        do { \
        } while (0); \
    }

//! Assigns either the value from the expected, or "or_value" value if the result is an error.
#define QTC_EXPECT(expected) \
    { \
        const auto tmp = expected; \
        if (Q_LIKELY(tmp)) { \
        } else { \
            ::Utils::writeAssertLocation( \
                QString("%1:%2: %3").arg(__FILE__).arg(__LINE__).arg(tmp.error()).toUtf8().data()); \
        } \
        do { \
        } while (0); \
    }

//! Return the expected if its error is set. Otherwise continue.
#define RETURN_IF_FAILED(expected) \
    { \
        const auto tmp = expected; \
        if (!expected) \
            return make_unexpected(expected.error()); \
    }

//! Adds more info to an error if the expected failed.
#define QTC_ADD_ERROR(error_string) \
    map_error([](const auto &error) -> QString { \
        return QString("%1:%2 : %3:\n\t %4") \
            .arg(__FILE__) \
            .arg(__LINE__) \
            .arg(error_string) \
            .arg(error); \
    })

//! Creates a new unexpected with the given error string including File and Line.
#define FAILURE(error_message) \
    make_unexpected(QString("%1:%2 : %3").arg(__FILE__).arg(__LINE__).arg(error_message))

//! Creates and returns a new unexpected with the given error string including File and Line.
#define RETURN_FAILURE(error_message) return FAILURE(error_message)
