// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include <QStringList>

namespace Utils { class FilePath; }

namespace Designer::Internal {

// Goto slot invoked by the designer context menu. Either navigates
// to an existing slot function or create a new one.
bool navigateToSlot(const Utils::FilePath &uiFilePath,
                    const QString &objectName,
                    const QString &signalSignature,
                    const QStringList &parameterNames,
                    QString *errorMessage);

} // Designer::Internnal
