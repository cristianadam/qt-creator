// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

const vscode = require('vscode');

function activate(context) {
    const report = doc =>
        vscode.window.showInformationMessage(
            'opened:' + doc.languageId + ':' + doc.uri.fsPath + ':len=' + doc.getText().length);

    for (const doc of vscode.workspace.textDocuments)
        report(doc);

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
