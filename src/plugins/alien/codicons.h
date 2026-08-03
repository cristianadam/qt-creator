// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QIcon>
#include <QString>

namespace Alien::Internal {

// VS Code labels carry icon markup: "$(gear) Build", "$(sync~spin) Loading".
// The icons come from a font we do not have, so the markup is removed and,
// where an equivalent Qt Creator icon exists, handed out separately.
QString stripCodicons(const QString &text);
QIcon firstCodicon(const QString &text);

} // namespace Alien::Internal
