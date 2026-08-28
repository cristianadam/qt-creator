// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

const vscode = require('vscode');

function activate(context) {
    const selector = [{ scheme: 'file', language: 'plaintext' }];

    context.subscriptions.push(vscode.languages.registerHoverProvider(selector, {
        provideHover(document, position) {
            return new vscode.Hover('Alien hover at ' + position.line + ':' + position.character);
        },
    }));

    context.subscriptions.push(vscode.languages.registerDefinitionProvider(selector, {
        provideDefinition(document, position) {
            return new vscode.Location(document.uri, new vscode.Position(2, 4));
        },
    }));
}

module.exports = { activate };
