// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

const vscode = require('vscode');

function activate(context) {
    const collection = vscode.languages.createDiagnosticCollection('alien-diag');
    context.subscriptions.push(collection);

    const publish = doc => {
        const range = new vscode.Range(0, 0, 0, 3);
        const diagnostic = new vscode.Diagnostic(
            range, 'Alien diagnostic', vscode.DiagnosticSeverity.Warning);
        diagnostic.source = 'alien';
        collection.set(doc.uri, [diagnostic]);
    };

    for (const doc of vscode.workspace.textDocuments)
        publish(doc);
    context.subscriptions.push(vscode.workspace.onDidOpenTextDocument(publish));
}

module.exports = { activate };
