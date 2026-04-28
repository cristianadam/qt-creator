// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifndef ANDROIDLOGCAT_H
#define ANDROIDLOGCAT_H

namespace Android::Internal {

//wire DeviceManager so that every Android device get a LogcatStream after the user has
// decided to open logcat
// call at plugin setup time.
void initAndroidLogcat();
} // namespace Android::Internal

#endif
