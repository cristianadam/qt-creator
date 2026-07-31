// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "litehtmlwebviewrenderer.h"

#include <coreplugin/icore.h>

#include <qlitehtmlwidget.h>

#include <QDockWidget>
#include <QMainWindow>

namespace Alien::Internal {

struct LiteHtmlWebviewRenderer::Panel
{
    QPointer<QDockWidget> dock;
    QPointer<QLiteHtmlWidget> view;
};

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
