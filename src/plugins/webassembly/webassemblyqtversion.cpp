// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "webassemblyqtversion.h"

#include "webassemblyconstants.h"
#include "webassemblytr.h"

#include <projectexplorer/abi.h>

#include <qtsupport/baseqtversion.h>
#include <qtsupport/qtversionmanager.h>

#include <utils/algorithm.h>

using namespace ProjectExplorer;
using namespace QtSupport;
using namespace Utils;

namespace WebAssembly::Internal {

bool isWebAssemblyQtVersion(const QtVersion *qtVersion)
{
    return qtVersion && anyOf(qtVersion->qtAbis(), [](const Abi &abi) {
        return abi.binaryFormat() == Abi::EmscriptenFormat;
    });
}

const QVersionNumber &minimumSupportedQtVersion()
{
    const static QVersionNumber number(5, 15);
    return number;
}

bool isQtVersionInstalled()
{
    return anyOf(QtVersionManager::versions(), &isWebAssemblyQtVersion);
}

bool isUnsupportedQtVersionInstalled()
{
    return anyOf(QtVersionManager::versions(), [](const QtVersion *v) {
        return isWebAssemblyQtVersion(v) && v->qtVersion() < minimumSupportedQtVersion();
    });
}

void setupWebAssemblyQtVersion()
{
    registerDeviceTypeForQtAbi(
        [](const Abi &abi) { return abi.binaryFormat() == Abi::EmscriptenFormat; },
        Constants::WEBASSEMBLY_DEVICE_TYPE,
        Tr::tr("WebAssembly", "Qt Version is meant for WebAssembly"));
}

} // WebAssembly::Internal
