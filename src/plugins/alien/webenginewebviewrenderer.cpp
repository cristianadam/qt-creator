// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "webenginewebviewrenderer.h"

#include "alientr.h"

#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>

#include <utils/filepath.h>

#include <QDockWidget>
#include <QJsonDocument>
#include <QLibraryInfo>
#include <QMainWindow>
#include <QRegularExpression>
#include <QWebEngineSettings>
#include <QWebChannel>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineView>

using namespace Core;
using namespace Utils;

namespace Alien::Internal {

// Carries page -> host messages: acquireVsCodeApi().postMessage() in the page
// calls received() over the web channel. The other direction does not need the
// channel and is delivered with runJavaScript().
class WebviewBridge final : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

public slots:
    void received(const QString &json)
    {
        emit messageFromPage(QJsonDocument::fromJson(json.toUtf8()).toVariant());
    }

signals:
    void messageFromPage(const QVariant &message);
};

// qwebchannel.js is shipped as a FILE in the Qt data directory, not as a Qt
// resource - loading it from ":/qtwebchannel/qwebchannel.js" silently yields
// nothing and leaves the page without a channel.
static QString webChannelScript()
{
    const FilePath js = FilePath::fromString(QLibraryInfo::path(QLibraryInfo::DataPath))
                        / "webchannel" / "qwebchannel.js";
    const Result<QByteArray> contents = js.fileContents();
    if (!contents) {
        MessageManager::writeSilently(
            Tr::tr("Alien: %1 is missing, so webviews cannot send messages back to their "
                   "extension. Rendering and updates are unaffected.").arg(js.toUserOutput()));
        return {};
    }
    return QString::fromUtf8(*contents);
}

// Injected before the page's own scripts, so acquireVsCodeApi() exists by the
// time they run. Only the page -> host direction goes through the channel; the
// host -> page direction is delivered with runJavaScript(), which needs no
// channel and works even if qwebchannel.js is unavailable.
static QString bridgeScript(bool withChannel)
{
    const QString channelSetup = withChannel ? R"JS(
    new QWebChannel(qt.webChannelTransport, function (channel) {
        bridge = channel.objects.alienBridge;
        for (var i = 0; i < pending.length; ++i)
            bridge.received(pending[i]);
        pending = [];
    });
)JS" : QString();

    return R"JS(
(function () {
    if (window.__alienBridgeInstalled)
        return;
    window.__alienBridgeInstalled = true;

    var pending = [];
    var bridge = null;
)JS" + channelSetup + R"JS(
    var state = undefined;
    window.acquireVsCodeApi = function () {
        return {
            postMessage: function (message) {
                var json = JSON.stringify(message);
                if (bridge)
                    bridge.received(json);
                else
                    pending.push(json);   // channel not up yet, or unavailable
            },
            getState: function () { return state; },
            setState: function (value) { state = value; return value; },
        };
    };
})();
)JS";
}

// Extension webviews reference their own bundled stylesheets and scripts, some
// through a <base>, most as absolute file: URLs. setHtml() without a base URL
// gives the page an about:blank origin, and such a page is not allowed to load
// file: subresources at all - it then renders unstyled and, worse, without the
// script that applies later updates, which looks exactly like a frozen preview.
//
// Falling back to "file:///" is enough to make the page count as local content:
// absolute file: URLs in the document then resolve normally, and a document
// that carries its own <base> keeps using it.
static QUrl baseUrlOf(const QString &html)
{
    static const QRegularExpression re(R"(<base\s+href\s*=\s*["']([^"']+)["'])",
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(html);
    return match.hasMatch() ? QUrl(match.captured(1)) : QUrl("file:///");
}

WebEngineWebviewRenderer::WebEngineWebviewRenderer() = default;

WebEngineWebviewRenderer::~WebEngineWebviewRenderer()
{
    for (const Panel &panel : std::as_const(m_panels))
        delete panel.dock;
}

void WebEngineWebviewRenderer::createPanel(const QString &id, const QString &viewType,
                                           const QString &title)
{
    Q_UNUSED(viewType)
    if (m_panels.contains(id))
        return;

    auto mainWindow = qobject_cast<QMainWindow *>(Core::ICore::mainWindow());
    if (!mainWindow)
        return;

    auto view = new QWebEngineView;

    // qwebchannel.js must be in the page before the bridge script runs, so the
    // two are injected as one script at document creation.
    const QString channelJs = webChannelScript();
    const QString setup = channelJs + bridgeScript(!channelJs.isEmpty());

    QWebEngineScript script;
    script.setName("alienBridge");
    script.setSourceCode(setup);
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setRunsOnSubFrames(false);
    view->page()->scripts().insert(script);

    view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);

    auto bridge = new WebviewBridge(view);
    auto channel = new QWebChannel(view);
    channel->registerObject("alienBridge", bridge);
    view->page()->setWebChannel(channel);

    QObject::connect(bridge, &WebviewBridge::messageFromPage, view,
                     [this, id](const QVariant &message) {
                         if (onMessage)
                             onMessage(id, QJsonValue::fromVariant(message));
                     });

    auto dock = new QDockWidget(title.isEmpty() ? QString("Webview") : title, mainWindow);
    dock->setObjectName("Alien.Webview." + id);
    dock->setWidget(view);
    mainWindow->addDockWidget(Qt::RightDockWidgetArea, dock);

    m_panels.insert(id, {dock, view, bridge});
}

void WebEngineWebviewRenderer::setHtml(const QString &id, const QString &html)
{
    if (const Panel panel = m_panels.value(id); panel.view)
        panel.view->setHtml(html, baseUrlOf(html));
}

void WebEngineWebviewRenderer::postMessage(const QString &id, const QJsonValue &message)
{
    const Panel panel = m_panels.value(id);
    if (!panel.view)
        return;
    // Delivered by script rather than over the channel: this works from the
    // first paint, and does not depend on qwebchannel.js being available.
    const QString json
        = QString::fromUtf8(QJsonDocument(message.toObject()).toJson(QJsonDocument::Compact));
    panel.view->page()->runJavaScript(
        QString("window.dispatchEvent(new MessageEvent('message', {data: %1}));").arg(json));
}

void WebEngineWebviewRenderer::reveal(const QString &id)
{
    if (const Panel panel = m_panels.value(id); panel.dock) {
        panel.dock->show();
        panel.dock->raise();
    }
}

void WebEngineWebviewRenderer::disposePanel(const QString &id)
{
    if (const Panel panel = m_panels.take(id); panel.dock)
        delete panel.dock;
}

} // namespace Alien::Internal

#include "webenginewebviewrenderer.moc"
