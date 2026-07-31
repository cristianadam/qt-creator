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

// A vscode.Event: calling it registers a listener; .fire(arg) notifies them.
function eventEmitter() {
    const listeners = new Set();
    const event = (listener, thisArg) => {
        const bound = thisArg ? listener.bind(thisArg) : listener;
        listeners.add(bound);
        return disposable(() => listeners.delete(bound));
    };
    event.fire = arg => {
        for (const listener of [...listeners]) {
            try {
                listener(arg);
            } catch (e) {
                logToStderr('listener error', e);
            }
        }
    };
    return event;
}

// --- document model ---------------------------------------------------------

const documents = new Map(); // uri -> TextDocument
const onDidOpenTextDocument = eventEmitter();
const onDidChangeTextDocument = eventEmitter();
const onDidCloseTextDocument = eventEmitter();
const onDidChangeActiveTextEditor = eventEmitter();

function makeTextDocument(params) {
    return {
        uri: vscode.Uri.file(params.uri),
        fileName: params.uri,
        languageId: params.languageId,
        version: params.version,
        isUntitled: false,
        isDirty: false,
        isClosed: false,
        eol: 1, // EndOfLine.LF
        _text: params.text || '',
        getText() { return this._text; },
        get lineCount() { return this._text.split('\n').length; },
        lineAt(line) {
            const text = this._text.split('\n')[line] || '';
            return {lineNumber: line, text, isEmptyOrWhitespace: text.trim() === ''};
        },
        offsetAt() { return 0; },
        positionAt() { return {line: 0, character: 0}; },
        save() { return Promise.resolve(true); },
    };
}

function makeTextEditor(document) {
    return {document, selection: undefined, selections: [], options: {}, viewColumn: 1};
}

// --- geometry / diagnostics types -------------------------------------------

class Position {
    constructor(line, character) { this.line = line; this.character = character; }
    translate(dl = 0, dc = 0) { return new Position(this.line + dl, this.character + dc); }
    with(line = this.line, character = this.character) { return new Position(line, character); }
    isEqual(o) { return !!o && this.line === o.line && this.character === o.character; }
}

class Range {
    constructor(a, b, c, d) {
        if (typeof a === 'number') {
            this.start = new Position(a, b);
            this.end = new Position(c, d);
        } else {
            this.start = a;
            this.end = b;
        }
    }
    get isEmpty() { return this.start.isEqual(this.end); }
    get isSingleLine() { return this.start.line === this.end.line; }
    with(start = this.start, end = this.end) { return new Range(start, end); }
}

class Location {
    constructor(uri, rangeOrPosition) {
        this.uri = uri;
        this.range = rangeOrPosition instanceof Range
            ? rangeOrPosition
            : new Range(rangeOrPosition, rangeOrPosition);
    }
}

class Diagnostic {
    constructor(range, message, severity = 0) {
        this.range = range;
        this.message = message;
        this.severity = severity;
        this.source = undefined;
        this.code = undefined;
        this.relatedInformation = [];
        this.tags = undefined;
    }
}

class SnippetString {
    constructor(value) { this.value = value || ''; }
    appendText(s) { this.value += s; return this; }
    appendPlaceholder(s) { this.value += s; return this; }
    appendTabstop() { return this; }
}

class MarkdownString {
    constructor(value) { this.value = value || ''; }
    appendText(s) { this.value += s; return this; }
    appendMarkdown(s) { this.value += s; return this; }
}

class CompletionItem {
    constructor(label, kind) {
        this.label = label;
        this.kind = kind;
        this.insertText = undefined;
        this.detail = undefined;
        this.documentation = undefined;
        this.sortText = undefined;
        this.filterText = undefined;
        this.range = undefined;
    }
}

function uriToString(uri) {
    if (typeof uri === 'string')
        return uri;
    return (uri && (uri.fsPath || uri.path)) || String(uri);
}

class Hover {
    constructor(contents, range) {
        this.contents = contents;
        this.range = range;
    }
}

// --- language feature providers ---------------------------------------------

const completionProviders = []; // {selector, provider}
const hoverProviders = [];
const definitionProviders = [];

function normalizeSelector(selector) {
    return Array.isArray(selector) ? selector : [selector];
}

function selectorLanguageIds(selector) {
    const ids = new Set();
    for (const filter of normalizeSelector(selector)) {
        if (typeof filter === 'string')
            ids.add(filter);
        else if (filter && filter.language)
            ids.add(filter.language);
    }
    return [...ids];
}

function matchDocumentSelector(document, selector) {
    for (const filter of normalizeSelector(selector)) {
        if (typeof filter === 'string') {
            if (filter === document.languageId || filter === '*')
                return true;
        } else if (filter) {
            const languageOk = !filter.language || filter.language === document.languageId
                || filter.language === '*';
            const schemeOk = !filter.scheme || filter.scheme === '*'
                || filter.scheme === (document.uri.scheme || 'file');
            if (languageOk && schemeOk)
                return true;
        }
    }
    return false;
}

function textOf(value) {
    if (value === null || value === undefined)
        return undefined;
    return typeof value === 'object' ? value.value : value;
}

function serializeRange(range) {
    if (!range)
        return {start: {line: 0, character: 0}, end: {line: 0, character: 0}};
    return {
        start: {line: range.start.line, character: range.start.character},
        end: {line: range.end.line, character: range.end.character},
    };
}

function hoverContentsToString(contents) {
    const parts = Array.isArray(contents) ? contents : [contents];
    const out = [];
    for (const part of parts) {
        if (part === null || part === undefined)
            continue;
        if (typeof part === 'string')
            out.push(part);
        else if (part.language !== undefined) // MarkedString {language, value}
            out.push('```' + part.language + '\n' + part.value + '\n```');
        else if (part.value !== undefined) // MarkdownString
            out.push(part.value);
    }
    return out.join('\n\n');
}

function serializeLocation(location) {
    if (!location)
        return null;
    const uri = location.uri || location.targetUri;
    const range = location.range || location.targetSelectionRange || location.targetRange;
    if (!uri)
        return null;
    return {uri: uriToString(uri), range: serializeRange(range)};
}

function serializeCompletion(item) {
    if (typeof item === 'string')
        item = new CompletionItem(item);
    const label = typeof item.label === 'string' ? item.label : (item.label && item.label.label) || '';
    const insert = item.insertText;
    const isSnippet = !!(insert && typeof insert === 'object');
    return {
        label,
        insertText: isSnippet ? insert.value : (insert !== undefined ? insert : label),
        isSnippet,
        kind: item.kind,
        detail: typeof item.label === 'object' ? item.label.detail : item.detail,
        documentation: textOf(item.documentation),
        sortText: item.sortText,
        filterText: item.filterText,
    };
}

function serializeDiagnostic(d) {
    const r = d.range || new Range(0, 0, 0, 0);
    return {
        range: {
            start: {line: r.start.line, character: r.start.character},
            end: {line: r.end.line, character: r.end.character},
        },
        message: d.message || '',
        severity: d.severity === undefined ? 0 : d.severity,
        source: d.source,
        code: (d.code && typeof d.code === 'object') ? d.code.value : d.code,
    };
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
        activeTextEditor: undefined,
        visibleTextEditors: [],
        onDidChangeActiveTextEditor,
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
        textDocuments: [],
        onDidOpenTextDocument,
        onDidChangeTextDocument,
        onDidCloseTextDocument,
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
    languages: {
        createDiagnosticCollection(name) {
            name = name || 'default';
            const store = new Map(); // uri string -> Diagnostic[]
            const publish = key => notify('diagnostics/publish', {
                collection: name,
                uri: key,
                diagnostics: (store.get(key) || []).map(serializeDiagnostic),
            });
            const collection = {
                name,
                set(uriOrEntries, diagnostics) {
                    if (Array.isArray(uriOrEntries)) {
                        for (const [uri, diags] of uriOrEntries)
                            collection.set(uri, diags);
                        return;
                    }
                    const key = uriToString(uriOrEntries);
                    if (!diagnostics)
                        store.delete(key);
                    else
                        store.set(key, diagnostics);
                    publish(key);
                },
                delete(uri) {
                    const key = uriToString(uri);
                    store.delete(key);
                    publish(key);
                },
                clear() {
                    const keys = [...store.keys()];
                    store.clear();
                    for (const key of keys)
                        publish(key);
                },
                get(uri) { return store.get(uriToString(uri)); },
                has(uri) { return store.has(uriToString(uri)); },
                forEach(callback) {
                    for (const [key, diags] of store)
                        callback(vscode.Uri.file(key), diags, collection);
                },
                dispose() { collection.clear(); },
            };
            return collection;
        },
        registerCompletionItemProvider(selector, provider, ...triggerCharacters) {
            const entry = {selector, provider};
            completionProviders.push(entry);
            notify('completion/registerProvider', {
                languageIds: selectorLanguageIds(selector),
                triggerCharacters,
            });
            return disposable(() => {
                const index = completionProviders.indexOf(entry);
                if (index >= 0)
                    completionProviders.splice(index, 1);
            });
        },
        registerHoverProvider(selector, provider) {
            const entry = {selector, provider};
            hoverProviders.push(entry);
            notify('hover/registerProvider', {languageIds: selectorLanguageIds(selector)});
            return disposable(() => {
                const index = hoverProviders.indexOf(entry);
                if (index >= 0)
                    hoverProviders.splice(index, 1);
            });
        },
        registerDefinitionProvider(selector, provider) {
            const entry = {selector, provider};
            definitionProviders.push(entry);
            notify('definition/registerProvider', {languageIds: selectorLanguageIds(selector)});
            return disposable(() => {
                const index = definitionProviders.indexOf(entry);
                if (index >= 0)
                    definitionProviders.splice(index, 1);
            });
        },
        // Remaining feature providers are accepted but not routed yet.
        registerCodeActionsProvider: () => disposable(() => {}),
        registerDocumentSymbolProvider: () => disposable(() => {}),
        registerDocumentFormattingEditProvider: () => disposable(() => {}),
        registerReferenceProvider: () => disposable(() => {}),
        registerRenameProvider: () => disposable(() => {}),
        registerSignatureHelpProvider: () => disposable(() => {}),
        match: () => 10,
    },
    Uri: {
        file: p => ({scheme: 'file', path: p, fsPath: p, toString: () => 'file://' + p}),
        parse: s => ({scheme: '', path: s, fsPath: s, toString: () => s}),
    },
    Position,
    Range,
    Location,
    Diagnostic,
    CompletionItem,
    SnippetString,
    MarkdownString,
    Hover,
    DiagnosticSeverity: {Error: 0, Warning: 1, Information: 2, Hint: 3},
    DiagnosticTag: {Unnecessary: 1, Deprecated: 2},
    EndOfLine: {LF: 1, CRLF: 2},
    CompletionItemKind: {
        Text: 0, Method: 1, Function: 2, Constructor: 3, Field: 4, Variable: 5,
        Class: 6, Interface: 7, Module: 8, Property: 9, Unit: 10, Value: 11,
        Enum: 12, Keyword: 13, Snippet: 14, Color: 15, File: 16, Reference: 17,
        Folder: 18, EnumMember: 19, Constant: 20, Struct: 21, Event: 22,
        Operator: 23, TypeParameter: 24,
    },
    CompletionTriggerKind: {Invoke: 0, TriggerCharacter: 1, TriggerForIncompleteCompletions: 2},
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

// --- vscode-languageclient shim --------------------------------------------
//
// Language extensions start their server through vscode-languageclient rather
// than talking LSP themselves. Instead of running a second LSP stack in Node,
// we resolve the ServerOptions to a concrete command and the documentSelector
// to file patterns, and hand both to Qt Creator, which runs the server with
// its own mature Language Client. (This only fires for extensions that
// require() vscode-languageclient at runtime; bundlers that inline it are a
// later concern.)

const TransportKind = {stdio: 0, ipc: 1, pipe: 2, socket: 3};
const RevealOutputChannelOn = {Info: 1, Warn: 2, Error: 3, Never: 4};
const ClientState = {Stopped: 1, Starting: 3, Running: 2};

// The extension currently being activated, so a LanguageClient created during
// activate() can resolve language ids against that extension's manifest.
let activating = null; // {id, langMap: Map<string, string[]>}

function resolveExecutable(serverOptions) {
    let exe = serverOptions;
    if (exe && (exe.run || exe.debug))
        exe = exe.run || exe.debug;
    if (typeof exe === 'function')
        throw new Error('Function ServerOptions are not supported yet.');
    if (exe && exe.command)
        return {path: exe.command, args: exe.args || [], cwd: (exe.options || {}).cwd || ''};
    if (exe && exe.module) {
        const args = [exe.module];
        if (exe.transport === TransportKind.stdio || exe.transport === 'stdio')
            args.push('--stdio');
        return {path: process.execPath, args: args.concat(exe.args || []),
                cwd: (exe.options || {}).cwd || ''};
    }
    throw new Error('Unsupported ServerOptions shape.');
}

function stripGlob(pattern) {
    // "**/*.qml" -> "*.qml"; keep simple base patterns as-is.
    const slash = pattern.lastIndexOf('/');
    return slash >= 0 ? pattern.slice(slash + 1) : pattern;
}

function resolveSelectors(clientOptions) {
    const patterns = new Set();
    const languageIds = new Set();
    const langMap = (activating && activating.langMap) || new Map();
    const addLanguage = id => {
        languageIds.add(id);
        for (const ext of (langMap.get(id) || []))
            patterns.add('*' + ext);
    };
    for (const sel of (clientOptions && clientOptions.documentSelector) || []) {
        if (typeof sel === 'string')
            addLanguage(sel);
        else if (sel) {
            if (sel.language)
                addLanguage(sel.language);
            if (sel.pattern)
                patterns.add(stripGlob(sel.pattern));
        }
    }
    return {filePatterns: [...patterns], languageIds: [...languageIds]};
}

class LanguageClientShim {
    constructor(...args) {
        // (id, name, serverOptions, clientOptions, forceDebug?) or
        // (name, serverOptions, clientOptions, forceDebug?)
        let id, name, serverOptions, clientOptions;
        if (typeof args[1] === 'string')
            [id, name, serverOptions, clientOptions] = args;
        else
            [name, serverOptions, clientOptions] = args, id = name;
        this._id = id;
        this._name = name || id;
        this._serverOptions = serverOptions;
        this._clientOptions = clientOptions || {};
        this._key = (activating ? activating.id : this._id) + ':' + this._id;
        this.outputChannel = vscode.window.createOutputChannel(this._name);
        this.state = ClientState.Stopped;
    }
    start() {
        const exe = resolveExecutable(this._serverOptions);
        const selectors = resolveSelectors(this._clientOptions);
        let init = this._clientOptions.initializationOptions;
        if (typeof init === 'function')
            init = init();
        this.state = ClientState.Running;
        const promise = request('languageclient/start', {
            id: this._key,
            name: this._name,
            command: {path: exe.path, args: exe.args},
            cwd: exe.cwd,
            filePatterns: selectors.filePatterns,
            languageIds: selectors.languageIds,
            initializationOptions: init || {},
        });
        // start() returns a Promise (new API); older call sites push the
        // return value as a Disposable, so also expose dispose().
        promise.dispose = () => this.stop();
        return promise;
    }
    stop() {
        this.state = ClientState.Stopped;
        notify('languageclient/stop', {id: this._key});
        return Promise.resolve();
    }
    dispose() { return this.stop(); }
    onReady() { return Promise.resolve(); }
    registerProposedFeatures() {}
    setTrace() { return Promise.resolve(); }
    onDidChangeState() { return disposable(() => {}); }
    onNotification() { return disposable(() => {}); }
    onRequest() { return disposable(() => {}); }
    sendNotification() {}
    sendRequest() { return Promise.reject(new Error('sendRequest is not supported yet.')); }
}

const languageClientModule = {
    LanguageClient: LanguageClientShim,
    TransportKind,
    RevealOutputChannelOn,
    State: ClientState,
    CloseAction: {DoNotRestart: 1, Restart: 2},
    ErrorAction: {Continue: 1, Shutdown: 2},
    SettingMonitor: class { start() { return disposable(() => {}); } },
};

// Make require('vscode') and require('vscode-languageclient') resolve to shims.
const originalLoad = Module._load;
Module._load = function (request, parent, isMain) {
    if (request === 'vscode')
        return vscode;
    if (request === 'vscode-languageclient' || request === 'vscode-languageclient/node')
        return languageClientModule;
    return originalLoad.apply(this, arguments);
};

// --- extension lifecycle ----------------------------------------------------

const activated = new Map(); // extension id -> module exports

onRequest('activate', async params => {
    const {id, main} = params;
    const langMap = new Map();
    for (const language of (params.languages || []))
        langMap.set(language.id, language.extensions || []);

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
    activating = {id, langMap};
    try {
        if (typeof exports.activate === 'function')
            await exports.activate(context);
    } finally {
        activating = null;
    }
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

// --- document sync (Qt Creator -> vscode.workspace) -------------------------

onRequest('document/didOpen', params => {
    if (documents.has(params.uri))
        return;
    const document = makeTextDocument(params);
    documents.set(params.uri, document);
    vscode.workspace.textDocuments.push(document);
    onDidOpenTextDocument.fire(document);
});

onRequest('document/didChange', params => {
    const document = documents.get(params.uri);
    if (!document)
        return;
    document._text = params.text || '';
    document.version = params.version;
    onDidChangeTextDocument.fire({
        document,
        contentChanges: [{text: document._text}],
        reason: undefined,
    });
});

onRequest('document/didClose', params => {
    const document = documents.get(params.uri);
    if (!document)
        return;
    documents.delete(params.uri);
    const index = vscode.workspace.textDocuments.indexOf(document);
    if (index >= 0)
        vscode.workspace.textDocuments.splice(index, 1);
    document.isClosed = true;
    onDidCloseTextDocument.fire(document);
});

onRequest('editor/didChangeActive', params => {
    const document = params.uri ? documents.get(params.uri) : undefined;
    vscode.window.activeTextEditor = document ? makeTextEditor(document) : undefined;
    onDidChangeActiveTextEditor.fire(vscode.window.activeTextEditor);
});

// --- language features (Qt Creator -> host providers) -----------------------

const cancellationToken = {
    isCancellationRequested: false,
    onCancellationRequested: () => disposable(() => {}),
};

onRequest('completion/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return {items: []};

    const position = new Position(params.position.line, params.position.character);
    const context = {
        triggerKind: params.triggerCharacter ? 1 : 0,
        triggerCharacter: params.triggerCharacter,
    };

    const items = [];
    for (const {selector, provider} of completionProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        try {
            const result = await provider.provideCompletionItems(
                document, position, cancellationToken, context);
            if (!result)
                continue;
            const list = Array.isArray(result) ? result : (result.items || []);
            for (const item of list)
                items.push(serializeCompletion(item));
        } catch (e) {
            logToStderr('completion provider error', e);
        }
    }
    return {items};
});

onRequest('hover/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return {contents: ''};
    const position = new Position(params.position.line, params.position.character);
    for (const {selector, provider} of hoverProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        try {
            const hover = await provider.provideHover(document, position, cancellationToken);
            if (hover && hover.contents !== null && hover.contents !== undefined)
                return {contents: hoverContentsToString(hover.contents)};
        } catch (e) {
            logToStderr('hover provider error', e);
        }
    }
    return {contents: ''};
});

onRequest('definition/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return {locations: []};
    const position = new Position(params.position.line, params.position.character);
    const locations = [];
    for (const {selector, provider} of definitionProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        try {
            const result = await provider.provideDefinition(document, position, cancellationToken);
            if (!result)
                continue;
            for (const location of (Array.isArray(result) ? result : [result])) {
                const serialized = serializeLocation(location);
                if (serialized)
                    locations.push(serialized);
            }
            if (locations.length)
                break;
        } catch (e) {
            logToStderr('definition provider error', e);
        }
    }
    return {locations};
});

notify('host/ready', {pid: process.pid, node: process.version});
