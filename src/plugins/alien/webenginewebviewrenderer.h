// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "webviewrenderer.h"

#include <QHash>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QDockWidget;
class QWebEngineView;
QT_END_NAMESPACE

namespace Alien::Internal {

class WebviewBridge;

// The full webview backend: a QWebEngineView in a dock, with a QWebChannel
// carrying messages both ways. Unlike the litehtml backend it runs the page's
// JavaScript, which most extensions need - the common VS Code idiom is to set
// the HTML once and then patch the DOM through postMessage.
//
// Constructing the first view is what starts a QtWebEngine render process, so
// this is created on demand (see AutoWebviewRenderer), never at startup.
class WebEngineWebviewRenderer final : public WebviewRenderer
{
public:
    WebEngineWebviewRenderer();
    ~WebEngineWebviewRenderer() override;

    void createPanel(const QString &id, const QString &viewType, const QString &title) override;
    void setHtml(const QString &id, const QString &html) override;
    void postMessage(const QString &id, const QJsonValue &message) override;
    void reveal(const QString &id) override;
    void disposePanel(const QString &id) override;

private:
    // Held by value in the hash below, so it has to be complete here.
    struct Panel
    {
        QPointer<QDockWidget> dock;
        QPointer<QWebEngineView> view;
        WebviewBridge *bridge = nullptr; // owned by the channel, parented to the view
    };

    QHash<QString, Panel> m_panels;
};

} // namespace Alien::Internal
