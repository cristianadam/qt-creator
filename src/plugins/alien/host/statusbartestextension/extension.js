// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

const vscode = require('vscode');

function activate(context) {
    vscode.window.setStatusBarMessage('Alien ready');

    const item = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
    item.text = 'AlienItem';
    item.tooltip = 'Alien tooltip';
    item.show();
    context.subscriptions.push(item);
}

module.exports = { activate };
