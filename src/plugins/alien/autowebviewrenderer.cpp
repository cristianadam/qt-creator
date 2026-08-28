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

#include <texteditor/fontsettings.h>

#include <utils/theme/theme.h>

#include <QApplication>
#include <QFont>
#include <QFontInfo>

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

// An extension declaring enableScripts has said what its panel needs, and that
// answer beats guessing from the markup: a page that only quotes a script tag
// does not need a browser engine started for it, and one whose scripts arrive
// later does.
bool AutoWebviewRenderer::needsEngine(const QString &id, const QString &html) const
{
    const auto it = m_panels.constFind(id);
    if (it != m_panels.constEnd() && it->scripts)
        return *it->scripts;
    return html.contains("<script", Qt::CaseInsensitive);
}

// Running script means the engine, so a panel asked to is moved onto it.
void AutoWebviewRenderer::runScript(const QString &id, const QString &script,
                                    const ScriptResult &done)
{
    if (!m_panels.contains(id)) {
        done({}, QString("There is no webview \"%1\".").arg(id));
        return;
    }
    if (!m_panels.value(id).upgraded && !upgrade(id)) {
        done({}, QString("This build has no webview backend that can run scripts."));
        return;
    }
    if (WebviewRenderer *backend = backendFor(id))
        backend->runScript(id, script, done);
}

void AutoWebviewRenderer::setPanelOptions(const QString &id, const QJsonObject &options)
{
    const auto it = m_panels.find(id);
    if (it == m_panels.end() || !options.contains("enableScripts"))
        return;
    it->scripts = options.value("enableScripts").toBool();
    if (*it->scripts && !it->upgraded)
        upgrade(id);
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


// The colors an extension's webview styles itself with: it writes
// var(--vscode-editor-background) and expects the editor to have said what that
// is. Without them a webview renders in whatever the engine defaults to - black
// on white inside a dark IDE - and the extension has no way to notice.
static QString themeStyle()
{
    // CSS wants the alpha last where it wants it at all, and Qt's HexArgb puts
    // it first, which a browser reads as a different colour entirely.
    const auto css = [](const QColor &c) {
        return QString("rgba(%1, %2, %3, %4)")
            .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alphaF());
    };
    const auto color = [css](Utils::Theme::Color role) {
        return css(Utils::creatorColor(role));
    };
    const QFont font = QApplication::font();
    const TextEditor::FontSettingsData fixed = TextEditor::globalFontSettings().data();
    // A webview shows document-like content, so the editor's own colors are the
    // ones it should read, not the window chrome's.
    const QTextCharFormat textFormat = fixed.toTextCharFormat(TextEditor::C_TEXT);
    const QString editorBackground = css(textFormat.background().color());
    const QString editorForeground = css(textFormat.foreground().color());

    const QList<QPair<QString, QString>> variables = {
        {"foreground", editorForeground},
        {"editor-foreground", editorForeground},
        {"editor-background", editorBackground},
        {"editorWidget-background", editorBackground},
        {"sideBar-background", color(Utils::Theme::BackgroundColorNormal)},
        {"panel-background", color(Utils::Theme::BackgroundColorNormal)},
        {"panel-border", color(Utils::Theme::SplitterColor)},
        {"widget-border", color(Utils::Theme::SplitterColor)},
        {"focusBorder", color(Utils::Theme::TextColorLink)},
        {"descriptionForeground", color(Utils::Theme::TextColorDisabled)},
        {"disabledForeground", color(Utils::Theme::TextColorDisabled)},
        {"errorForeground", color(Utils::Theme::TextColorError)},
        {"textLink-foreground", color(Utils::Theme::TextColorLink)},
        {"textLink-activeForeground", color(Utils::Theme::TextColorLink)},
        {"textPreformat-foreground", color(Utils::Theme::TextColorNormal)},
        {"button-background", color(Utils::Theme::BackgroundColorSelected)},
        {"button-foreground", color(Utils::Theme::TextColorNormal)},
        {"button-hoverBackground", color(Utils::Theme::BackgroundColorHover)},
        {"input-background", color(Utils::Theme::BackgroundColorNormal)},
        {"input-foreground", color(Utils::Theme::TextColorNormal)},
        {"input-border", color(Utils::Theme::SplitterColor)},
        {"list-hoverBackground", color(Utils::Theme::BackgroundColorHover)},
        {"list-activeSelectionBackground", color(Utils::Theme::BackgroundColorSelected)},
        {"list-activeSelectionForeground", color(Utils::Theme::TextColorNormal)},
    };

    QString style = ":root {";
    for (const auto &[name, value] : variables)
        style += QString("--vscode-%1: %2;").arg(name, value);
    // Everything a webview sizes is relative to these two, and CSS counts in
    // pixels where the fonts here are described in points.
    const auto pixelSize = [](const QFont &f) { return QFontInfo(f).pixelSize(); };
    style += QString("--vscode-font-family: '%1';").arg(font.family());
    style += QString("--vscode-font-size: %1px;").arg(pixelSize(font));
    style += QString("--vscode-editor-font-family: '%1';").arg(fixed.family());
    style += QString("--vscode-editor-font-size: %1px;").arg(pixelSize(fixed.font()));
    style += "}";
    style += "body { color: var(--vscode-editor-foreground);"
             " background-color: var(--vscode-editor-background);"
             " font-family: var(--vscode-font-family);"
             " font-size: var(--vscode-font-size); }";
    return style;
}

// Extensions also branch on which kind of theme it is, both in CSS and in
// script, so the body says so the way they expect to read it.
QString themedWebviewHtml(const QString &html)
{
    const bool dark = Utils::creatorTheme()
                      && Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface);
    const QString kind = dark ? QString("vscode-dark") : QString("vscode-light");
    const QString head = QString("<style>%1</style>").arg(themeStyle());

    QString themed = html;
    const qsizetype headEnd = themed.indexOf("</head>", 0, Qt::CaseInsensitive);
    if (headEnd >= 0)
        themed.insert(headEnd, head);
    else
        themed.prepend(head);

    const qsizetype bodyStart = themed.indexOf("<body", 0, Qt::CaseInsensitive);
    if (bodyStart >= 0) {
        const qsizetype bodyEnd = themed.indexOf('>', bodyStart);
        if (bodyEnd >= 0) {
            themed.insert(bodyEnd, QString(" class=\"%1\" data-vscode-theme-kind=\"%1\"")
                                       .arg(kind));
        }
    }
    return themed;
}

void AutoWebviewRenderer::setHtml(const QString &id, const QString &html)
{
    const auto it = m_panels.find(id);
    if (it == m_panels.end())
        return;
    it->html = themedWebviewHtml(html);

    if (!it->upgraded && needsEngine(id, html))
        upgrade(id); // scripted content is useless on the static backend

    if (WebviewRenderer *backend = backendFor(id))
        backend->setHtml(id, it->html);
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
