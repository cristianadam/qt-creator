// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

const vscode = require('vscode');

function activate(context) {
    const panel = vscode.window.createWebviewPanel(
        'alienDemo', 'Alien Demo', vscode.ViewColumn.One, { enableScripts: true });

    panel.webview.html = '<html><body><h1>Alien Webview</h1></body></html>';

    panel.webview.onDidReceiveMessage(message => {
        vscode.window.showInformationMessage('got:' + message.text);
    });

    panel.webview.postMessage({ from: 'extension' });

    context.subscriptions.push(panel);
}

module.exports = { activate };
