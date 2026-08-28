// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

const vscode = require('vscode');

function activate(context) {
    context.subscriptions.push(vscode.commands.registerCommand('alien.pick', async () => {
        const choice = await vscode.window.showQuickPick(
            ['Alpha', 'Beta', 'Gamma'], { placeHolder: 'Pick one' });
        vscode.window.showInformationMessage('picked:' + choice);
    }));

    context.subscriptions.push(vscode.commands.registerCommand('alien.pickFolder', async () => {
        const folder = await vscode.window.showWorkspaceFolderPick();
        vscode.window.showInformationMessage('folder:' + (folder && folder.name));
    }));
}

module.exports = { activate };
