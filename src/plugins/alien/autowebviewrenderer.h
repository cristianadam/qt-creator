// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "webviewrenderer.h"

#include <QHash>

#include <memory>

namespace Alien::Internal {

// Picks a webview backend per panel, and switches one over when it turns out
// to need more than the cheap backend can give.
//
// Panels start on litehtml, which renders static HTML with no JavaScript engine
// and no extra process. A panel is moved to QtWebEngine when it becomes clear
// that it cannot work without one:
//
//   - its HTML contains a <script>, i.e. the content is built in the page, or
//   - the extension sends it a postMessage, i.e. it patches the DOM from the
//     host; this is the common VS Code idiom of setting the HTML once and then
//     updating through messages, and it is silently inert on litehtml.
//
// The upgrade replaces the widget in place and replays the stored HTML, so the
// extension sees no difference. The point of the arrangement is that the first
// QWebEngineView - and with it a render process - is only ever constructed if
// some panel genuinely needs it; a session that only shows static previews
// never pays for QtWebEngine at all.
class AutoWebviewRenderer final : public WebviewRenderer
{
public:
    AutoWebviewRenderer();
    ~AutoWebviewRenderer() override;

    void createPanel(const QString &id, const QString &viewType, const QString &title) override;
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
    };

    // Returns the backend the panel is currently on, or nullptr if unknown.
    WebviewRenderer *backendFor(const QString &id) const;
    // Moves the panel to QtWebEngine. Returns false if that backend is not
    // available in this build, in which case the panel stays where it is.
    bool upgrade(const QString &id);
    void adopt(WebviewRenderer *backend);
    static bool needsEngine(const QString &html);

    std::unique_ptr<WebviewRenderer> m_static;   // litehtml, if built
    std::unique_ptr<WebviewRenderer> m_engine;   // QtWebEngine, created on demand
    QHash<QString, Panel> m_panels;
};

} // namespace Alien::Internal
