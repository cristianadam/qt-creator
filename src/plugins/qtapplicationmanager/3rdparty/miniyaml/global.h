// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#pragma once
#include <QtCore/qglobal.h>
#include <QtCore/QLoggingCategory>
#include <QtCore/QDir>
#include <QtCore/QString>

#define QT_BEGIN_NAMESPACE_AM
#define QT_END_NAMESPACE_AM
#define QT_USE_NAMESPACE_AM
#define QT_PREPEND_NAMESPACE_AM(name) name

Q_DECLARE_LOGGING_CATEGORY(LogSystem)

// make the source a lot less ugly and more readable (until we can finally use user defined literals)
#define qL1S(x) QLatin1String(x)
#define qL1C(x) QLatin1Char(x)
#define qSL(x) QStringLiteral(x)

QString toAbsoluteFilePath(const QString &path, const QString &baseDir = QDir::currentPath());

