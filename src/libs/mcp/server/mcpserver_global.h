// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <qglobal.h>

#if defined(MCPSERVERLIB_LIBRARY)
#define MCPSERVER_EXPORT Q_DECL_EXPORT
#elif defined(MCPSERVERLIB_STATIC_LIBRARY)
#define MCPSERVER_EXPORT
#else
#define MCPSERVER_EXPORT Q_DECL_IMPORT
#endif
