// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "core_global.h"

#include <utils/aspects.h>

namespace Core {
class SecretAspectPrivate;

class CORE_EXPORT SecretAspect : public Utils::StringAspect
{
public:
    static bool isAvailable();

    explicit SecretAspect(Utils::AspectContainer *container = nullptr);
    ~SecretAspect() override;

    void readSettings() override;
    void writeSettings() const override;

private:
    std::unique_ptr<SecretAspectPrivate> d;
};

} // namespace Core
