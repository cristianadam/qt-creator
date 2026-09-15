// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "webenginewebviewrenderer.h"

#include "alientr.h"

#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>

#include <utils/algorithm.h>
#include <utils/filepath.h>

#include <QDockWidget>
#include <QPointer>
#include <QJsonArray>
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

// A message is any JSON value, and a QJsonDocument holds only an object or an
// array: both directions read and write it as the one element of an array.
// Without this a page that posts a string or a number is heard as nothing at
// all, which looks like a message that was never sent.
static QJsonValue valueFromJson(const QString &json)
{
    const QJsonDocument document = QJsonDocument::fromJson(('[' + json + ']').toUtf8());
    const QJsonArray wrapper = document.array();
    return wrapper.isEmpty() ? QJsonValue() : wrapper.first();
}

static QString jsonOfValue(const QJsonValue &message)
{
    const QByteArray wrapped
        = QJsonDocument(QJsonArray{message}).toJson(QJsonDocument::Compact).trimmed();
    return QString::fromUtf8(wrapped.mid(1, wrapped.size() - 2));
}

// Carries page -> host messages: acquireVsCodeApi().postMessage() in the page
// calls received() over the web channel. The other direction does not need the
// channel and is delivered with runJavaScript().
class WebviewBridge final : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

public slots:
    void received(const QString &json) { emit messageFromPage(valueFromJson(json)); }

signals:
    void messageFromPage(const QJsonValue &message);
};

// qwebchannel.js is shipped as a FILE in the Qt data directory, not as a Qt
// resource - loading it from ":/qtwebchannel/qwebchannel.js" silently yields
// nothing and leaves the page without a channel.
// Where it sits depends on how Qt was built: a prefix build has it under the
// data path, a development build keeps the data path at the build root and the
// file under share/qt6. Getting this wrong costs the page -> host direction
// entirely, so all the places it can be are tried.
static FilePaths webChannelScriptCandidates()
{
    FilePaths candidates;
    for (const QLibraryInfo::LibraryPath location : {QLibraryInfo::DataPath,
                                                     QLibraryInfo::PrefixPath}) {
        const FilePath root = FilePath::fromString(QLibraryInfo::path(location));
        candidates << root / "webchannel" / "qwebchannel.js"
                   << root / "share" / "qt6" / "webchannel" / "qwebchannel.js";
    }
    return candidates;
}

FilePath webChannelScriptPath()
{
    for (const FilePath &js : webChannelScriptCandidates()) {
        if (js.isReadableFile())
            return js;
    }
    return {};
}

static QString webChannelScript()
{
    const FilePaths candidates = webChannelScriptCandidates();
    for (const FilePath &js : candidates) {
        if (const Result<QByteArray> contents = js.fileContents())
            return QString::fromUtf8(*contents);
    }

    MessageManager::writeSilently(
        Tr::tr("Alien: qwebchannel.js was not found in %1, so webviews cannot send messages "
               "back to their extension. Rendering and updates are unaffected.")
            .arg(Utils::transform(candidates, &FilePath::toUserOutput).join(", ")));
    return {};
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
    // A message is structured-cloned in VS Code, so it can carry bytes, and
    // JSON cannot: the host writes them as base64 and they are made whole
    // again here. The host side of this is withBinaryEncoded() in host.js.
    var BYTES_KEY = '$alienBytes';

    function toBase64(bytes) {
        var parts = [];
        for (var i = 0; i < bytes.length; i += 0x8000)
            parts.push(String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000)));
        return btoa(parts.join(''));
    }

    function fromBase64(text) {
        var binary = atob(text);
        var bytes = new Uint8Array(binary.length);
        for (var i = 0; i < binary.length; ++i)
            bytes[i] = binary.charCodeAt(i);
        return bytes;
    }

    function encode(value) {
        if (value === null || typeof value !== 'object')
            return value;
        if (value instanceof ArrayBuffer) {
            return {[BYTES_KEY]: toBase64(new Uint8Array(value)), kind: 'ArrayBuffer'};
        }
        if (ArrayBuffer.isView(value)) {
            return {[BYTES_KEY]: toBase64(new Uint8Array(value.buffer, value.byteOffset,
                                                         value.byteLength)),
                    kind: value.constructor.name};
        }
        if (typeof value.toJSON === 'function')
            return value;
        if (Array.isArray(value))
            return value.map(encode);
        var encoded = {};
        for (var key of Object.keys(value))
            encoded[key] = encode(value[key]);
        return encoded;
    }

    function revive(value) {
        if (value === null || typeof value !== 'object')
            return value;
        if (Array.isArray(value))
            return value.map(revive);
        if (typeof value[BYTES_KEY] === 'string') {
            var bytes = fromBase64(value[BYTES_KEY]);
            if (value.kind === 'ArrayBuffer')
                return bytes.buffer;
            var type = window[value.kind];
            return typeof type === 'function' ? new type(bytes.buffer) : bytes;
        }
        var revived = {};
        for (var key of Object.keys(value))
            revived[key] = revive(value[key]);
        return revived;
    }
    window.__alienRevive = revive;

    var state = undefined;
    window.acquireVsCodeApi = function () {
        return {
            postMessage: function (message) {
                var json = JSON.stringify(encode(message));
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
                     [this, id](const QJsonValue &message) {
                         if (onMessage)
                             onMessage(id, message);
                     });

    auto dock = new QDockWidget(title.isEmpty() ? QString("Webview") : title, mainWindow);
    dock->setObjectName("Alien.Webview." + id);
    // A panel is the extension's own interface. Left to itself the dock takes
    // the little its contents ask for, which for a web view is nothing at all;
    // below this it cannot show a page.
    view->setMinimumWidth(320);
    dock->setWidget(view);
    mainWindow->addDockWidget(Qt::RightDockWidgetArea, dock);

    m_panels.insert(id, {dock, view, bridge});
}

void WebEngineWebviewRenderer::setHtml(const QString &id, const QString &html)
{
    if (const Panel panel = m_panels.value(id); panel.view)
        panel.view->setHtml(html, baseUrlOf(html));
}

void WebEngineWebviewRenderer::runScript(const QString &id, const QString &script,
                                        const ScriptResult &done)
{
    const Panel panel = m_panels.value(id);
    if (!panel.view) {
        done({}, QString("There is no webview \"%1\".").arg(id));
        return;
    }
    panel.view->page()->runJavaScript(script, [done](const QVariant &result) {
        done(QJsonValue::fromVariant(result), {});
    });
}

void WebEngineWebviewRenderer::postMessage(const QString &id, const QJsonValue &message)
{
    const Panel panel = m_panels.value(id);
    if (!panel.view)
        return;
    // Delivered by script rather than over the channel: this works from the
    // first paint, and does not depend on qwebchannel.js being available.
    //
    // A page also looks at where a message came from: qt-core's wizard ignores
    // everything whose origin is not a vscode-webview: one, and a synthesized
    // event carries no origin unless it is given one.
    panel.view->page()->runJavaScript(
        QString("window.dispatchEvent(new MessageEvent('message', "
                "{data: window.__alienRevive(%1), origin: 'vscode-webview://%2'}));")
            .arg(jsonOfValue(message), id));
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
