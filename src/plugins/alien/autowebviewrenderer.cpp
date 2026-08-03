// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "autowebviewrenderer.h"

#include "alientr.h"

#ifdef ALIEN_WITH_LITEHTML
#include "litehtmlwebviewrenderer.h"
#endif
#ifdef ALIEN_WITH_WEBENGINE
#include "webenginewebviewrenderer.h"
#endif

#include <coreplugin/messagemanager.h>

namespace Alien::Internal {

AutoWebviewRenderer::AutoWebviewRenderer()
{
#ifdef ALIEN_WITH_LITEHTML
    m_static = std::make_unique<LiteHtmlWebviewRenderer>();
    adopt(m_static.get());
#endif
}

AutoWebviewRenderer::~AutoWebviewRenderer() = default;

// Forwards a backend's callbacks to our own, so the host sees one renderer no
// matter which backend a panel currently sits on. The guards matter: these are
// std::functions, and calling an unset one throws.
void AutoWebviewRenderer::adopt(WebviewRenderer *backend)
{
    backend->onMessage = [this](const QString &id, const QJsonValue &message) {
        if (onMessage)
            onMessage(id, message);
    };
    backend->onDisposed = [this](const QString &id) {
        m_panels.remove(id);
        if (onDisposed)
            onDisposed(id);
    };
}

bool AutoWebviewRenderer::needsEngine(const QString &html)
{
    return html.contains("<script", Qt::CaseInsensitive);
}

WebviewRenderer *AutoWebviewRenderer::backendFor(const QString &id) const
{
    const auto it = m_panels.constFind(id);
    if (it == m_panels.constEnd())
        return nullptr;
    return it->upgraded ? m_engine.get() : m_static.get();
}

bool AutoWebviewRenderer::upgrade(const QString &id)
{
    const auto it = m_panels.find(id);
    if (it == m_panels.end())
        return false;
    if (it->upgraded)
        return true;

#ifndef ALIEN_WITH_WEBENGINE
    return false;
#else
    if (!m_engine) {
        // First view of the session: this is where QtWebEngine actually starts.
        m_engine = std::make_unique<WebEngineWebviewRenderer>();
        adopt(m_engine.get());
    }

    if (m_static)
        m_static->disposePanel(id);
    m_engine->createPanel(id, it->viewType, it->title);
    if (!it->html.isEmpty())
        m_engine->setHtml(id, it->html);
    it->upgraded = true;
    return true;
#endif
}

void AutoWebviewRenderer::createPanel(const QString &id, const QString &viewType,
                                      const QString &title)
{
    m_panels.insert(id, Panel{viewType, title, {}, false});

    if (m_static) {
        m_static->createPanel(id, viewType, title);
        return;
    }
    // No static backend in this build: go straight to the engine.
    if (!upgrade(id)) {
        Core::MessageManager::writeFlashing(
            Tr::tr("Cannot show the webview \"%1\": this build has no webview backend.")
                .arg(title.isEmpty() ? id : title));
    }
}

void AutoWebviewRenderer::setHtml(const QString &id, const QString &html)
{
    const auto it = m_panels.find(id);
    if (it == m_panels.end())
        return;
    it->html = html;

    if (!it->upgraded && needsEngine(html))
        upgrade(id); // scripted content is useless on the static backend

    if (WebviewRenderer *backend = backendFor(id))
        backend->setHtml(id, html);
}

void AutoWebviewRenderer::postMessage(const QString &id, const QJsonValue &message)
{
    const auto it = m_panels.find(id);
    if (it == m_panels.end())
        return;

    // A message means the extension expects script in the page to act on it,
    // which the static backend cannot do - so this is the other upgrade point.
    if (!it->upgraded && !upgrade(id)) {
        Core::MessageManager::writeSilently(
            Tr::tr("Webview \"%1\" was sent a message, but this build has no QtWebEngine "
                   "backend to run it. The panel will not update.").arg(id));
        return;
    }

    if (WebviewRenderer *backend = backendFor(id))
        backend->postMessage(id, message);
}

void AutoWebviewRenderer::reveal(const QString &id)
{
    if (WebviewRenderer *backend = backendFor(id))
        backend->reveal(id);
}

void AutoWebviewRenderer::disposePanel(const QString &id)
{
    if (WebviewRenderer *backend = backendFor(id))
        backend->disposePanel(id);
    m_panels.remove(id);
}

} // namespace Alien::Internal
