// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

const vscode = require('vscode');

function activate(context) {
    const report = doc =>
        vscode.window.showInformationMessage(
            'opened:' + doc.languageId + ':' + doc.uri.fsPath + ':len=' + doc.getText().length);

    for (const doc of vscode.workspace.textDocuments)
        report(doc);

    context.subscriptions.push(vscode.commands.registerCommand(
        'alien.test.openDocument', async path => {
            const doc = await vscode.workspace.openTextDocument(path);
            vscode.window.showInformationMessage('read:' + doc.getText().trim());
            const editor = await vscode.window.showTextDocument(doc);
            vscode.window.showInformationMessage('shown:' + editor.document.uri.fsPath);
        }));

    context.subscriptions.push(vscode.commands.registerCommand(
        'alien.test.applyEdit', async path => {
            const uri = vscode.Uri.file(path);
            const edit = new vscode.WorkspaceEdit();
            // Two edits, given in the order that only works if they are applied
            // back to front.
            edit.replace(uri, new vscode.Range(0, 0, 0, 5), 'FIRST');
            edit.insert(uri, new vscode.Position(1, 0), 'inserted\n');
            const applied = await vscode.workspace.applyEdit(edit);
            vscode.window.showInformationMessage(
                'edited:' + applied + ':size=' + edit.size);
        }));

    context.subscriptions.push(vscode.workspace.registerTextDocumentContentProvider(
        'alientest', {provideTextDocumentContent: uri => 'provided for ' + uri.path}));

    context.subscriptions.push(vscode.commands.registerCommand(
        'alien.test.openVirtualDocument', async () => {
            const uri = vscode.Uri.parse('alientest:/greeting');
            const doc = await vscode.workspace.openTextDocument(uri);
            vscode.window.showInformationMessage('virtual:' + doc.getText());
            await vscode.window.showTextDocument(doc);
            vscode.window.showInformationMessage('virtualShown');
        }));

    context.subscriptions.push(vscode.workspace.onDidOpenTextDocument(report));
    context.subscriptions.push(vscode.workspace.onDidChangeTextDocument(event => {
        const change = event.contentChanges[0];
        const hasRange = !!(change && change.range && change.range.start);
        vscode.window.showInformationMessage(
            'changed:' + event.document.uri.fsPath
            + ':len=' + event.document.getText().length + ':range=' + hasRange);
    }));
}

module.exports = { activate };
