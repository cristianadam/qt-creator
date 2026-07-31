// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <coreplugin/inavigationwidgetfactory.h>

namespace Alien::Internal {

class ExtensionHost;

// A sidebar navigation view backed by an in-host TreeDataProvider. Children are
// fetched lazily over JSON-RPC as the user expands nodes.
class AlienTreeViewFactory final : public Core::INavigationWidgetFactory
{
public:
    AlienTreeViewFactory(ExtensionHost *host, const QString &viewId, const QString &displayName);

    Core::NavigationView createWidget() override;

private:
    ExtensionHost *m_host;
    QString m_viewId;
};

} // namespace Alien::Internal
