// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/settingsaccessor.h>
#include <utils/store.h>

#include <QList>

namespace ProjectExplorer {

class Toolchain;

namespace Internal {

class ToolchainSettingsAccessor : public Utils::UpgradingSettingsAccessor
{
public:
    ToolchainSettingsAccessor();

    QList<Toolchain *> restoreToolchains() const;

    // Re-restore toolchains that could not be restored during loading for lack of a factory,
    // now that more factories may be registered (e.g. by a soft-loaded plugin).
    QList<Toolchain *> retryDeferredToolchains();

    void saveToolchains(const QList<Toolchain *> &toolchains);

private:
    QList<Toolchain *> toolChains(const Utils::Store &data) const;

    // Raw data of stored toolchains no factory could restore, kept for a later retry.
    mutable QList<Utils::Store> m_deferredToolchains;
};

QObject *createToolchainSettingsTest();

} // namespace Internal
} // namespace ProjectExplorer
