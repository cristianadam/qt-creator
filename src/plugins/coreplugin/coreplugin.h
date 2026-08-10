// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <functional>

namespace ExtensionSystem { class IPlugin; }

namespace Core::Internal {

ExtensionSystem::IPlugin *corePlugin();

void updateActionsForOptionsPages();

// Runs \a update on the next event loop turn and drops the calls that arrive
// until then. Registrations come in batches, and each update rebuilds from the
// full list anyway.
void scheduleRegistryUpdate(bool &pending, const std::function<void()> &update);

} // Core::Internal

