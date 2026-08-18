// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "plugin2.h"

#include <extensionsystem/pluginmanager.h>

using namespace SoftPlugin2;

MySoftPlugin2::~MySoftPlugin2()
{
    ExtensionSystem::PluginManager::removeObject(m_object);
}

void MySoftPlugin2::initialize()
{
    m_object = new QObject(this);
    m_object->setObjectName("MySoftPlugin2");
    ExtensionSystem::PluginManager::addObject(m_object);
}
