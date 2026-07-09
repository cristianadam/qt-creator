// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QVersionNumber>

namespace QtSupport { class QtVersion; }

namespace WebAssembly::Internal {

// A WebAssembly Qt is recognized by its Emscripten ABI, not by a dedicated QtVersion type, so
// that it is recognized regardless of whether the (soft-loadable) plugin was loaded when the Qt
// version was detected.
bool isWebAssemblyQtVersion(const QtSupport::QtVersion *qtVersion);

const QVersionNumber &minimumSupportedQtVersion();
bool isQtVersionInstalled();
bool isUnsupportedQtVersionInstalled();

void setupWebAssemblyQtVersion();

} // WebAssembly::Internal
