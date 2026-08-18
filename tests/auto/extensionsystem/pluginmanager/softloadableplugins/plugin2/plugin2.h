// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <extensionsystem/iplugin.h>

#if defined(SOFTPLUGIN2_LIBRARY)
#  define SOFTPLUGIN2_EXPORT Q_DECL_EXPORT
#elif defined(SOFTPLUGIN2_STATIC_LIBRARY)
#  define SOFTPLUGIN2_EXPORT
#else
#  define SOFTPLUGIN2_EXPORT Q_DECL_IMPORT
#endif

namespace SoftPlugin2 {

class SOFTPLUGIN2_EXPORT MySoftPlugin2 final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "plugin" FILE "plugin2.json")

public:
    MySoftPlugin2() = default;
    ~MySoftPlugin2() final;

    void initialize() final;

private:
    QObject *m_object = nullptr;
};

} // namespace SoftPlugin2
