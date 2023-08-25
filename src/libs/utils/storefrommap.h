// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "store.h"

namespace Utils {

#ifdef QTC_USE_STORE

inline Store storeFromMap(const QVariantMap &map)
{
    Store store;
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (it.value().canConvert<QVariantMap>())
            store.insert(keyFromString(it.key()), QVariant::fromValue(storeFromMap(it->toMap())));
        else
            store.insert(keyFromString(it.key()), it.value());
    }
    return store;
}

inline QVariantMap mapFromStore(const Store &store)
{
    QVariantMap map;
    for (auto it = store.begin(); it != store.end(); ++it) {
        if (it.value().canConvert<Store>())
            map.insert(stringFromKey(it.key()), mapFromStore(it->value<Store>()));
        else
            map.insert(stringFromKey(it.key()), it.value());
    }
    return map;
}

#else

inline Store storeFromMap(const QVariantMap &map)
{
    return map;
}
inline QVariantMap mapFromStore(const Store &store)
{
    return store;
}

#endif

} // namespace Utils