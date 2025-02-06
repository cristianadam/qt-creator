// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "filepath.h"
#include "utils_global.h"

#include <solutions/tasking/tasktree.h>

#include <QObject>
#include <QPromise>

namespace Utils {

QTCREATOR_UTILS_EXPORT Result
unarchive(const Utils::FilePath &archive, const Utils::FilePath &destination);

QTCREATOR_UTILS_EXPORT void unarchivePromised(
    QPromise<Result> &promise,
    const Utils::FilePath &archive,
    const Utils::FilePath &destination,
    std::function<void(FilePath)> callback);

} // namespace Utils
