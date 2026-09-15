// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// A debug adapter that does nothing but speak the protocol, so a test can find
// out whether Qt Creator reached one. It answers what is needed to get a
// session going and ends it as soon as the debuggee would have started.

let buffer = '';
let sequence = 0;

function send(message) {
    message.seq = ++sequence;
    const text = JSON.stringify(message);
    process.stdout.write('Content-Length: ' + Buffer.byteLength(text) + '\r\n\r\n' + text);
}

function respond(request, body) {
    send({type: 'response', request_seq: request.seq, command: request.command,
          success: true, body: body || {}});
}

let launchArguments = {};
let terminated = false;

// A session is over once, however it got there.
function terminate() {
    if (terminated)
        return;
    terminated = true;
    send({type: 'event', event: 'terminated'});
}

function handle(request) {
    if (request.type !== 'request')
        return;
    if (request.command === 'initialize') {
        respond(request, {supportsConfigurationDoneRequest: true});
        send({type: 'event', event: 'initialized'});
        return;
    }
    // A request the protocol does not define, answered the way cortex-debug's
    // adapter answers its own: with what the session was launched with.
    if (request.command === 'get-arguments') {
        respond(request, launchArguments);
        return;
    }
    // A session told to stay ends when it is asked to, which is a request of
    // the adapter's own like any other.
    if (request.command === 'finish') {
        respond(request);
        terminate();
        return;
    }
    if (request.command === 'launch' || request.command === 'attach')
        launchArguments = request.arguments || {};
    respond(request);
    // Nothing is being debugged, so the session is over as soon as it began -
    // unless it was asked to stay, which is what a session has to do to answer
    // anything at all.
    if ((request.command === 'launch' || request.command === 'attach')
        && !launchArguments.stayAlive) {
        terminate();
    }
    if (request.command === 'disconnect' || request.command === 'terminate') {
        // An adapter says the session is over before it goes; leaving on the
        // response alone keeps whoever waits for the news waiting.
        terminate();
        process.exit(0);
    }
}

process.stdin.on('data', chunk => {
    buffer += chunk;
    for (;;) {
        const headerEnd = buffer.indexOf('\r\n\r\n');
        if (headerEnd < 0)
            return;
        const header = buffer.slice(0, headerEnd);
        const match = /Content-Length: *(\d+)/.exec(header);
        if (!match)
            return;
        const length = Number(match[1]);
        if (buffer.length < headerEnd + 4 + length)
            return;
        const body = buffer.slice(headerEnd + 4, headerEnd + 4 + length);
        buffer = buffer.slice(headerEnd + 4 + length);
        handle(JSON.parse(body));
    }
});
