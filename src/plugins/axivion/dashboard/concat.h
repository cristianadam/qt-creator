#pragma once

/*
 * Copyright (C) 2022-current by Axivion GmbH
 * https://www.axivion.com/
 *
 * SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
 *
 * Purpose: Helper functions to concatenate strings/bytes
 *
 * !!!!!! GENERATED, DO NOT EDIT !!!!!!
 *
 * This file was generated with the script at
 * <AxivionSuiteRepo>/projects/libs/dashboard_cpp_api/generator/generate_dashboard_cpp_api.py
 */

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringView>

#include <algorithm>
#include <string>
#include <string_view>
#include <type_traits>

namespace Axivion::Internal::Dto
{

template<typename OutputT, typename ...InputT>
OutputT concat_to_output(InputT&... args)
{
    auto size = std::accumulate(args.size()...);
    OutputT output;
    output.reserve(size);
    output.append(args...);
    return output;
}


template <typename T>
struct is_basic_string_view : std::false_type {};
template <typename CharT, typename Traits>
struct is_basic_string_view<std::basic_string_view<CharT, Traits>> : std::true_type {};
template <typename CharT, typename Traits>
struct is_basic_string_view<std::basic_string_view<CharT, Traits>&> : std::true_type {};
template <typename CharT, typename Traits>
struct is_basic_string_view<const std::basic_string_view<CharT, Traits>> : std::true_type {};
template <typename CharT, typename Traits>
struct is_basic_string_view<const std::basic_string_view<CharT, Traits>&> : std::true_type {};


template<typename ...StrT,
         typename = typename std::enable_if<
             std::conjunction<
                 std::disjunction<
                     is_basic_string_view<StrT>...,
                     std::is_same<std::string, StrT>...>,
                 std::is_convertible<StrT, std::string>...>::value
             >
         >
std::string concat(StrT&&... args)
{
    return concat_to_output<std::string, StrT...>(std::forward<StrT>(args)...);
}

template<typename ...StrT,
         typename = typename std::enable_if<
             std::conjunction<
                 std::disjunction<
                     std::is_same<QStringView, StrT>...,
                     std::is_same<QString, StrT>...>,
                 std::is_convertible<StrT, QString>...>::value
             >
         >
QString concat(StrT&&... args)
{
    return concat_to_output<QString, StrT...>(std::forward<StrT>(args)...);
}

template<typename ...StrT,
         typename = typename std::enable_if<
             std::conjunction<
                 std::disjunction<
                     std::is_same<QByteArrayView, StrT>...,
                     std::is_same<QByteArray, StrT>...>,
                 std::is_convertible<StrT, QByteArray>...>::value
             >
         >
QByteArray concat(StrT&&... args)
{
    return concat_to_output<QByteArray, StrT...>(std::forward<StrT>(args)...);
}


} // namespace Axivion::Internal::Dto
