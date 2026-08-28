// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

const vscode = require('vscode');

function activate(context) {
    const channel = vscode.window.createOutputChannel('Alien Test');
    channel.appendLine('Alien test extension activated.');

    vscode.window.showInformationMessage('Alien extension host is alive.');

    const disposable = vscode.commands.registerCommand('alien.hello', () => {
        vscode.window.showInformationMessage('Hello from the Alien extension!');
    });
    context.subscriptions.push(disposable);
}

function deactivate() {}

module.exports = { activate, deactivate };
