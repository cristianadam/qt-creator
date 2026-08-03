// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "litehtmlwebviewrenderer.h"

#include <coreplugin/icore.h>

#include <utils/filepath.h>

#include <qlitehtmlwidget.h>

#include <QDockWidget>
#include <QMainWindow>
#include <QUrl>

namespace Alien::Internal {

LiteHtmlWebviewRenderer::~LiteHtmlWebviewRenderer()
{
    for (const Panel &panel : std::as_const(m_panels))
        delete panel.dock;
}

void LiteHtmlWebviewRenderer::createPanel(const QString &id, const QString &viewType,
                                          const QString &title)
{
    Q_UNUSED(viewType)
    if (m_panels.contains(id))
        return;

    auto mainWindow = qobject_cast<QMainWindow *>(Core::ICore::mainWindow());
    if (!mainWindow)
        return;

    auto view = new QLiteHtmlWidget;
    // Mandatory: litehtml calls this for every <link>, <img> and @import it
    // meets, and an unset handler is an empty std::function - calling it
    // throws bad_function_call and takes the whole application down.
    // Extensions reference their own bundled assets, so only local files are
    // served; anything else (network, custom schemes) resolves to nothing.
    view->setResourceHandler([](const QUrl &url) -> QByteArray {
        if (!url.isLocalFile())
            return {};
        const Utils::Result<QByteArray> contents
            = Utils::FilePath::fromUrl(url).fileContents();
        return contents ? *contents : QByteArray();
    });
    auto dock = new QDockWidget(title.isEmpty() ? QString("Webview") : title, mainWindow);
    dock->setObjectName("Alien.Webview." + id);
    dock->setWidget(view);
    mainWindow->addDockWidget(Qt::RightDockWidgetArea, dock);

    m_panels.insert(id, {dock, view});
}

void LiteHtmlWebviewRenderer::setHtml(const QString &id, const QString &html)
{
    if (const Panel panel = m_panels.value(id); panel.view)
        panel.view->setHtml(html);
}

void LiteHtmlWebviewRenderer::postMessage(const QString &id, const QJsonValue &message)
{
    // No JavaScript engine: extension -> webview messaging is not supported by
    // this backend.
    Q_UNUSED(id)
    Q_UNUSED(message)
}

void LiteHtmlWebviewRenderer::reveal(const QString &id)
{
    if (const Panel panel = m_panels.value(id); panel.dock) {
        panel.dock->show();
        panel.dock->raise();
    }
}

void LiteHtmlWebviewRenderer::disposePanel(const QString &id)
{
    if (const Panel panel = m_panels.take(id); panel.dock)
        delete panel.dock;
}

} // namespace Alien::Internal
