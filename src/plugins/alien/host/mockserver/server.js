// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
//
// Minimal LSP server over stdio (Content-Length framing). Just enough to
// complete the initialize handshake so the interception path can be verified.

'use strict';

let buffer = Buffer.alloc(0);

function send(message) {
    message.jsonrpc = '2.0';
    const body = Buffer.from(JSON.stringify(message), 'utf8');
    process.stdout.write('Content-Length: ' + body.length + '\r\n\r\n');
    process.stdout.write(body);
}

function handle(message) {
    if (message.method === 'initialize')
        send({id: message.id, result: {capabilities: {textDocumentSync: 1}}});
    else if (message.method === 'shutdown')
        send({id: message.id, result: null});
    else if (message.method === 'exit')
        process.exit(0);
}

process.stdin.on('data', chunk => {
    buffer = Buffer.concat([buffer, chunk]);
    for (;;) {
        const headerEnd = buffer.indexOf('\r\n\r\n');
        if (headerEnd < 0)
            break;
        const header = buffer.slice(0, headerEnd).toString('ascii');
        const match = /Content-Length: *(\d+)/i.exec(header);
        const bodyStart = headerEnd + 4;
        if (!match) {
            buffer = buffer.slice(bodyStart);
            continue;
        }
        const length = parseInt(match[1], 10);
        if (buffer.length < bodyStart + length)
            break;
        const body = buffer.slice(bodyStart, bodyStart + length).toString('utf8');
        buffer = buffer.slice(bodyStart + length);
        try {
            handle(JSON.parse(body));
        } catch (e) {
            process.stderr.write('mockserver parse error: ' + e + '\n');
        }
    }
});
