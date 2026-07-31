// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

const vscode = require('vscode');

function activate(context) {
    const provider = {
        provideCompletionItems(document, position) {
            const first = new vscode.CompletionItem(
                'alienComplete', vscode.CompletionItemKind.Function);
            first.detail = 'Alien completion';
            const second = new vscode.CompletionItem(
                'alienOther', vscode.CompletionItemKind.Variable);
            return [first, second];
        },
    };

    context.subscriptions.push(vscode.languages.registerCompletionItemProvider(
        [{ scheme: 'file', language: 'plaintext' }], provider, '.'));
}

module.exports = { activate };
