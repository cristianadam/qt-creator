// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "webviewrenderer.h"

#include <QHash>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QDockWidget;
QT_END_NAMESPACE

namespace Alien::Internal {

// The default, dependency-light webview backend: renders static HTML with the
// in-tree qlitehtml widget in a dock. It has no JavaScript engine, so
// postMessage and webview -> extension messaging are inert; interactive
// webviews need a QtWebEngine backend (not implemented here).
class LiteHtmlWebviewRenderer final : public WebviewRenderer
{
public:
    ~LiteHtmlWebviewRenderer() override;

    void createPanel(const QString &id, const QString &viewType, const QString &title) override;
    void setHtml(const QString &id, const QString &html) override;
    void postMessage(const QString &id, const QJsonValue &message) override;
    void reveal(const QString &id) override;
    void disposePanel(const QString &id) override;

private:
    struct Panel;
    QHash<QString, Panel> m_panels;
};

} // namespace Alien::Internal
