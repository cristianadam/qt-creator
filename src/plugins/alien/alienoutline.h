// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <texteditor/ioutlinewidget.h>

namespace Alien::Internal {

class ExtensionHost;

// The outline of a document an extension knows the contents of. Registered for
// as long as the plugin runs; it answers for an editor only where an extension
// has claimed that editor's language.
void setupAlienOutline(ExtensionHost *host);

} // namespace Alien::Internal
