// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <functional>

namespace Alien::Internal {

// Backend that displays extension webview panels. Kept abstract so the core
// plugin carries no HTML-engine dependency: a litehtml renderer (static HTML)
// ships in-tree, and an optional QtWebEngine backend can be added later for
// interactive content. Panels are addressed by the host-assigned id.
class WebviewRenderer
{
public:
    virtual ~WebviewRenderer() = default;

    virtual void createPanel(const QString &id, const QString &viewType, const QString &title) = 0;

    // Runs script in the panel's page and hands back what it evaluated to. Only
    // a backend that has a script engine can; the rest say so.
    using ScriptResult = std::function<void(const QJsonValue &result, const QString &error)>;
    virtual void runScript(const QString &id, const QString &script, const ScriptResult &done)
    {
        Q_UNUSED(id)
        Q_UNUSED(script)
        done({}, QString("This webview backend cannot run scripts."));
    }

    // What the extension said the panel needs, which is not the same as what
    // its first page happens to contain.
    virtual void setPanelOptions(const QString &id, const QJsonObject &options)
    {
        Q_UNUSED(id)
        Q_UNUSED(options)
    }
    virtual void setHtml(const QString &id, const QString &html) = 0;
    virtual void postMessage(const QString &id, const QJsonValue &message) = 0;
    virtual void reveal(const QString &id) { Q_UNUSED(id) }
    virtual void disposePanel(const QString &id) = 0;

    // Renderer -> host callbacks (set by ExtensionHost).
    std::function<void(const QString &id, const QJsonValue &message)> onMessage;
    std::function<void(const QString &id)> onDisposed;
};

} // namespace Alien::Internal
