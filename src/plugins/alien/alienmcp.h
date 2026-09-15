// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

namespace Alien::Internal {

// Lets the panels an extension puts up be looked at and driven from outside.
// Their contents are a web page, which the widget tools cannot reach into. The
// tools exist as soon as the plugin does; they find the running host when asked,
// and say so when there is none.
void registerMcpTools();

} // namespace Alien::Internal
