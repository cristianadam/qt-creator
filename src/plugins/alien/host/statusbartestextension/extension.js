// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

const vscode = require('vscode');

function activate(context) {
    vscode.window.setStatusBarMessage('Alien ready');

    context.subscriptions.push(vscode.commands.registerCommand('alien.status.clicked', () =>
        vscode.window.showInformationMessage('statusClicked')));

    const item = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
    item.text = 'AlienItem';
    item.tooltip = 'Alien tooltip';
    item.command = 'alien.status.clicked';
    item.show();
    context.subscriptions.push(item);
}

module.exports = { activate };
