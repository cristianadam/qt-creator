// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <extensionsystem/iplugin.h>

#include "qtmarketplacewelcomepage.h"

namespace Marketplace {

class MarketplacePlugin : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "Marketplace.json")

public:
    MarketplacePlugin() = default;

    bool initialize(const QStringList &, QString *) final { return true; }

private:
    Internal::QtMarketplaceWelcomePage welcomePage;
};

} // namespace Marketplace
