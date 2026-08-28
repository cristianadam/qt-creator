// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

const vscode = require('vscode');

// A two-level tree: Root A / Root B, with A having Child A1.
function activate(context) {
    const tree = {
        'root': ['Root A', 'Root B'],
        'Root A': ['Child A1'],
    };

    const provider = {
        getChildren(element) {
            return tree[element || 'root'] || [];
        },
        getTreeItem(element) {
            const hasChildren = !!tree[element];
            const item = new vscode.TreeItem(
                element,
                hasChildren ? vscode.TreeItemCollapsibleState.Collapsed
                            : vscode.TreeItemCollapsibleState.None);
            item.tooltip = 'Alien: ' + element;
            item.iconPath = new vscode.ThemeIcon('folder');
            return item;
        },
    };

    context.subscriptions.push(
        vscode.window.registerTreeDataProvider('alienExplorer', provider));

    const view = vscode.window.createTreeView('alienWatched', {treeDataProvider: provider});
    view.onDidChangeSelection(event => vscode.window.showInformationMessage(
        'picked:' + (event.selection || []).join(',')));
    context.subscriptions.push(view);

    context.subscriptions.push(vscode.commands.registerCommand(
        'alien.tree.setReady',
        () => vscode.commands.executeCommand('setContext', 'alien.ready', true)));
    context.subscriptions.push(
        vscode.commands.registerCommand('alien.tree.always', () => {}));
    context.subscriptions.push(
        vscode.commands.registerCommand('alien.tree.gated', () => {}));
}

module.exports = { activate };
