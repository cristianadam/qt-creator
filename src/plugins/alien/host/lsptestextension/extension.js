// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

const path = require('path');
const { LanguageClient } = require('vscode-languageclient/node');

let client;

function activate(context) {
    const serverModule = path.join(__dirname, '..', 'mockserver', 'server.js');
    const serverOptions = { command: process.execPath, args: [serverModule] };
    const clientOptions = { documentSelector: [{ scheme: 'file', language: 'plaintext' }] };

    client = new LanguageClient('alienMock', 'Alien Mock LSP', serverOptions, clientOptions);
    context.subscriptions.push(client.start());
}

function deactivate() {
    return client ? client.stop() : undefined;
}

module.exports = { activate, deactivate };
