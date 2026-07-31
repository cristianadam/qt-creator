// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
//
// Alien VS Code extension host.
//
// Runs in Node.js, spawned by the Alien plugin. Loads VS Code extensions and
// provides the "vscode" module as a set of JSON-RPC stubs that reflect every
// call to the Qt Creator side (the "main side"). Protocol is newline-delimited
// JSON-RPC 2.0 over stdio; stdout carries only protocol messages, everything
// else goes to stderr.

'use strict';

const Module = require('module');

// Keep stdout clean for the protocol: route all console output to stderr.
const logToStderr = (...args) => process.stderr.write('[alien-host] ' + args.join(' ') + '\n');
console.log = logToStderr;
console.info = logToStderr;
console.warn = logToStderr;
console.error = logToStderr;

// --- JSON-RPC transport -----------------------------------------------------

let nextId = 1;
const pending = new Map(); // id -> {resolve, reject}
const requestHandlers = new Map(); // method -> (params) => result|Promise

function send(message) {
    message.jsonrpc = '2.0';
    process.stdout.write(JSON.stringify(message) + '\n');
}

function notify(method, params) {
    send({method, params});
}

function request(method, params) {
    const id = nextId++;
    return new Promise((resolve, reject) => {
        pending.set(id, {resolve, reject});
        send({id, method, params});
    });
}

function onRequest(method, handler) {
    requestHandlers.set(method, handler);
}

async function dispatch(message) {
    if (message.method !== undefined && message.id !== undefined) {
        // Inbound request from the main side.
        const handler = requestHandlers.get(message.method);
        if (!handler) {
            send({id: message.id, error: {code: -32601, message: 'Method not found: ' + message.method}});
            return;
        }
        try {
            const result = await handler(message.params || {});
            send({id: message.id, result: result === undefined ? null : result});
        } catch (e) {
            send({id: message.id, error: {code: -32000, message: String(e && e.stack || e)}});
        }
    } else if (message.method !== undefined) {
        // Inbound notification.
        const handler = requestHandlers.get(message.method);
        if (handler)
            Promise.resolve(handler(message.params || {})).catch(e => logToStderr('notify error', e));
    } else if (message.id !== undefined) {
        // Response to one of our requests.
        const p = pending.get(message.id);
        if (!p)
            return;
        pending.delete(message.id);
        if (message.error)
            p.reject(new Error(message.error.message));
        else
            p.resolve(message.result);
    }
}

let stdinBuffer = '';
process.stdin.setEncoding('utf8');
process.stdin.on('data', chunk => {
    stdinBuffer += chunk;
    let index;
    while ((index = stdinBuffer.indexOf('\n')) >= 0) {
        const line = stdinBuffer.slice(0, index).trim();
        stdinBuffer = stdinBuffer.slice(index + 1);
        if (!line)
            continue;
        try {
            dispatch(JSON.parse(line));
        } catch (e) {
            logToStderr('parse error', e, line);
        }
    }
});
process.stdin.on('end', () => process.exit(0));

// --- vscode API shim --------------------------------------------------------
//
// Only the surface needed by the first extensions is implemented. Anything
// missing throws, which surfaces on the main side as an activation error.

const commandHandlers = new Map(); // command id -> callback

function disposable(dispose) {
    return {dispose};
}

const vscode = {
    commands: {
        registerCommand(command, callback, thisArg) {
            commandHandlers.set(command, thisArg ? callback.bind(thisArg) : callback);
            notify('commands/register', {command});
            return disposable(() => {
                commandHandlers.delete(command);
                notify('commands/unregister', {command});
            });
        },
        executeCommand(command, ...args) {
            const handler = commandHandlers.get(command);
            if (handler)
                return Promise.resolve().then(() => handler(...args));
            return Promise.reject(new Error('Unknown command: ' + command));
        },
    },
    window: {
        showInformationMessage: (message, ...items) =>
            request('window/showMessage', {level: 'info', message, items: flattenItems(items)}),
        showWarningMessage: (message, ...items) =>
            request('window/showMessage', {level: 'warn', message, items: flattenItems(items)}),
        showErrorMessage: (message, ...items) =>
            request('window/showMessage', {level: 'error', message, items: flattenItems(items)}),
        createOutputChannel(name) {
            return {
                name,
                append: value => notify('output/append', {channel: name, value, line: false}),
                appendLine: value => notify('output/append', {channel: name, value, line: true}),
                clear() {},
                show() {},
                hide() {},
                replace() {},
                dispose() {},
            };
        },
    },
    workspace: {
        workspaceFolders: undefined,
        getConfiguration(section) {
            return {
                get: (key, defaultValue) => defaultValue,
                has: () => false,
                update: () => Promise.resolve(),
                inspect: () => undefined,
            };
        },
        onDidChangeConfiguration: () => disposable(() => {}),
    },
    Uri: {
        file: p => ({scheme: 'file', path: p, fsPath: p, toString: () => 'file://' + p}),
        parse: s => ({scheme: '', path: s, fsPath: s, toString: () => s}),
    },
    Disposable: {from: (...items) => disposable(() => items.forEach(i => i && i.dispose && i.dispose()))},
};

function flattenItems(items) {
    // showInformationMessage(message, options?, ...items) - we ignore an
    // options object and keep string items (or item.title).
    return items
        .filter(i => i !== null && i !== undefined)
        .map(i => (typeof i === 'string' ? i : i.title))
        .filter(i => typeof i === 'string');
}

// Make require('vscode') resolve to the shim.
const originalLoad = Module._load;
Module._load = function (request, parent, isMain) {
    if (request === 'vscode')
        return vscode;
    return originalLoad.apply(this, arguments);
};

// --- extension lifecycle ----------------------------------------------------

const activated = new Map(); // extension id -> module exports

onRequest('activate', async params => {
    const {id, main} = params;
    const exports = require(main);
    const context = {
        subscriptions: [],
        extensionPath: params.path,
        extensionUri: vscode.Uri.file(params.path),
        globalState: {get: (k, d) => d, update: () => Promise.resolve(), keys: () => []},
        workspaceState: {get: (k, d) => d, update: () => Promise.resolve(), keys: () => []},
        asAbsolutePath: rel => require('path').join(params.path, rel),
    };
    activated.set(id, {exports, context});
    if (typeof exports.activate === 'function')
        await exports.activate(context);
    return {ok: true};
});

onRequest('executeCommand', async params => {
    return vscode.commands.executeCommand(params.command, ...(params.args || []));
});

onRequest('deactivate', async params => {
    const entry = activated.get(params.id);
    if (!entry)
        return null;
    activated.delete(params.id);
    for (const d of entry.context.subscriptions) {
        try { d && d.dispose && d.dispose(); } catch (e) { logToStderr('dispose error', e); }
    }
    if (typeof entry.exports.deactivate === 'function')
        await entry.exports.deactivate();
    return null;
});

onRequest('ping', async () => ({pong: true}));

notify('host/ready', {pid: process.pid, node: process.version});
