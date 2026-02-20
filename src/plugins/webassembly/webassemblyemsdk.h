// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QVersionNumber>

#include <utils/result.h>

namespace Utils {
class Environment;
class FilePath;
}

namespace WebAssembly::Internal::WebAssemblyEmSdk {

void parseEmSdkEnvOutputAndAddToEnv(const QString &output, Utils::Environment &env);
void addToEnvironment(const Utils::FilePath &sdkRoot, Utils::Environment &env);
Utils::Result<QVersionNumber> version(const Utils::FilePath &sdkRoot);
void clearCaches();

} // WebAssembly::Internal::WebAssemblyEmSdk
