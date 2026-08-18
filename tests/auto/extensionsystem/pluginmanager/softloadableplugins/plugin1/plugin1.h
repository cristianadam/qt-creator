// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <extensionsystem/iplugin.h>

#if defined(SOFTPLUGIN1_LIBRARY)
#  define SOFTPLUGIN1_EXPORT Q_DECL_EXPORT
#elif defined(SOFTPLUGIN1_STATIC_LIBRARY)
#  define SOFTPLUGIN1_EXPORT
#else
#  define SOFTPLUGIN1_EXPORT Q_DECL_IMPORT
#endif

namespace SoftPlugin1 {

class SOFTPLUGIN1_EXPORT MySoftPlugin1 final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "plugin" FILE "plugin1.json")

public:
    MySoftPlugin1() = default;
    ~MySoftPlugin1() final;

    void initialize() final;

private:
    QObject *m_object = nullptr;
};

} // namespace SoftPlugin1
