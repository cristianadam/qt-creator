// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "environmentprovider.h"

#include "algorithm.h"

namespace Utils {

Q_GLOBAL_STATIC(QVector<EnvironmentProvider>, environmentProviders)

void EnvironmentProvider::addProvider(EnvironmentProvider &&provider)
{
    environmentProviders->append(std::move(provider));
}

const QVector<EnvironmentProvider> EnvironmentProvider::providers()
{
    return *environmentProviders;
}

Utils::optional<EnvironmentProvider> EnvironmentProvider::provider(const QByteArray &id)
{
    const int index = indexOf(*environmentProviders, equal(&EnvironmentProvider::id, id));
    if (index >= 0)
        return Utils::make_optional(environmentProviders->at(index));
    return Utils::nullopt;
}

} // namespace Utils
