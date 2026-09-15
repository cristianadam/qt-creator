// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "webviewrenderer.h"

#include <QHash>

#include <memory>
#include <optional>

namespace Alien::Internal {

// The colors, fonts and theme kind an extension's webview styles itself with.
// Exposed for the test, the renderer applies it to everything it shows.
QString themedWebviewHtml(const QString &html);

// Picks a webview backend per panel. A panel starts on litehtml, which renders
// static HTML without a JavaScript engine and without a second process, and is
// moved to QtWebEngine once it turns out to need one: its HTML carries a
// <script>, or the extension patches the page through postMessage, which is
// inert on litehtml. The upgrade replaces the widget in place and replays the
// stored HTML, so the extension sees no difference, and a session that only
// shows static pages never starts a render process at all.
class AutoWebviewRenderer final : public WebviewRenderer
{
public:
    AutoWebviewRenderer();
    ~AutoWebviewRenderer() override;

    void createPanel(const QString &id, const QString &viewType, const QString &title) override;
    void setPanelOptions(const QString &id, const QJsonObject &options) override;
    bool runsScripts(const QString &id) const override;
    void runScript(const QString &id, const QString &script,
                   const ScriptResult &done) override;
    void setHtml(const QString &id, const QString &html) override;
    void postMessage(const QString &id, const QJsonValue &message) override;
    void reveal(const QString &id) override;
    void disposePanel(const QString &id) override;

    // True once a QtWebEngine view exists, i.e. the engine has been booted.
    bool engineStarted() const { return bool(m_engine); }

private:
    struct Panel
    {
        QString viewType;
        QString title;
        QString html;      // last HTML, replayed when the panel is upgraded
        bool upgraded = false;
        std::optional<bool> scripts; // what the extension asked for, if it said
    };

    // Returns the backend the panel is currently on, or nullptr if unknown.
    WebviewRenderer *backendFor(const QString &id) const;
    // Moves the panel to QtWebEngine. Returns false if that backend is not
    // available in this build, in which case the panel stays where it is.
    bool upgrade(const QString &id);
    void adopt(WebviewRenderer *backend);
    bool needsEngine(const QString &id, const QString &html) const;

    std::unique_ptr<WebviewRenderer> m_static;   // litehtml, if built
    std::unique_ptr<WebviewRenderer> m_engine;   // QtWebEngine, created on demand
    QHash<QString, Panel> m_panels;
};

} // namespace Alien::Internal
