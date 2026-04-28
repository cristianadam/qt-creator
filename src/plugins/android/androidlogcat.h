// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

namespace Android::Internal {

// Install the Tools > Android > Logcat submenu. Triggering an entry opens
// (or raises) the Logcat tab for the corresponding ready Android device.
void initAndroidLogcat();
} // namespace Android::Internal
