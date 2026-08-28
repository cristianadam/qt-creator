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
const nodeFs = require('fs');
const nodePath = require('path');

// Keep stdout clean for the protocol: route all console output to stderr.
const logToStderr = (...args) => process.stderr.write('[alien-host] ' + args.join(' ') + '\n');
console.log = logToStderr;
console.info = logToStderr;
console.warn = logToStderr;
console.error = logToStderr;

// console is not the only way out: bundled libraries write to stdout directly
// (bugsnag, shipped inside several extensions, prints a banner), which lands
// in the middle of the protocol. Keep the real stdout private and let anything
// else that writes there end up on stderr.
const writeToStdout = process.stdout.write.bind(process.stdout);
process.stdout.write = (chunk, encoding, callback) =>
    process.stderr.write(chunk, encoding, callback);

// One extension's bug must not take the host down, and with it every other
// extension: an async throw anywhere would otherwise end the process, leaving
// the main side waiting for answers that never come.
// A throw nobody caught leaves the host half-built - if it happened while
// loading, no request handler is installed at all and every extension fails
// with "Method not found". Tell Qt Creator, which is the only place anyone
// will look; the log category this used to go to is off by default.
const reportFatal = (kind, error) => {
    const message = kind + ': ' + ((error && error.stack) || error);
    logToStderr(message);
    try {
        notify('host/fatal', {message});
    } catch (ignored) {
        // The transport itself is what broke; stderr above is all we have.
    }
};
process.on('uncaughtException', error => reportFatal('uncaught exception', error));
process.on('unhandledRejection', error => reportFatal('unhandled rejection', error));

// --- JSON-RPC transport -----------------------------------------------------

let nextId = 1;
const pending = new Map(); // id -> {resolve, reject}
const requestHandlers = new Map(); // method -> (params) => result|Promise

function send(message) {
    message.jsonrpc = '2.0';
    writeToStdout(JSON.stringify(message) + '\n');
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

// What every register* hands back. Disposing twice has to be the no-op the API
// says it is: extensions push these into context.subscriptions and dispose them
// themselves as well, and on our side the second one would unregister whatever
// has taken the same name since.
function disposable(dispose) {
    let done = false;
    return {
        dispose() {
            if (done)
                return;
            done = true;
            if (typeof dispose === 'function')
                dispose();
        },
    };
}

// vscode.Disposable is a real class: `new Disposable(fn)` plus a static from().
class DisposableClass {
    constructor(callOnDispose) { this._callOnDispose = callOnDispose; }
    // Once. Extensions both push a disposable into context.subscriptions and
    // dispose it themselves, so the second call has to be the no-op the API
    // says it is - on our side it would unregister whatever took its place.
    dispose() {
        const callOnDispose = this._callOnDispose;
        this._callOnDispose = undefined;
        if (typeof callOnDispose === 'function')
            callOnDispose();
    }
    static from(...items) {
        return new DisposableClass(() => {
            for (const item of items)
                item && item.dispose && item.dispose();
        });
    }
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
                const result = listener(arg);
                if (result && typeof result.then === 'function')
                    result.catch(e => logToStderr('listener error', e));
            } catch (e) {
                logToStderr('listener error', e);
            }
        }
    };
    event.clear = () => listeners.clear();
    return event;
}

// --- document model ---------------------------------------------------------

const documents = new Map(); // uri -> TextDocument

// The tabs of the one editor area, built from the documents that are open in
// it. Rebuilt on demand rather than kept, so it cannot go stale.
const onDidChangeTabs = eventEmitter();
const onDidChangeTabGroups = eventEmitter();
let activeTabUri;

function tabGroup() {
    const group = {viewColumn: 1, isActive: true, activeTab: undefined, tabs: []};
    for (const [uri, document] of documents) {
        const tab = {
            label: nodePath.basename(uri),
            group,
            input: new TabInputText(document.uri),
            isActive: uri === activeTabUri,
            isDirty: !!document.isDirty,
            isPinned: false,
            isPreview: false,
        };
        group.tabs.push(tab);
        if (tab.isActive)
            group.activeTab = tab;
    }
    return group;
}

function fireTabsChanged(opened, closed, changed) {
    onDidChangeTabs.fire({opened: opened || [], closed: closed || [], changed: changed || []});
}
const contentProviders = new Map(); // scheme -> TextDocumentContentProvider
const onDidOpenTextDocument = eventEmitter();
const onDidChangeTextDocument = eventEmitter();
const onDidCloseTextDocument = eventEmitter();
const onDidChangeActiveTextEditor = eventEmitter();
const onDidSaveTextDocument = eventEmitter();
const onWillSaveTextDocument = eventEmitter();
const onDidChangeTextEditorSelection = eventEmitter();
const onDidChangeConfiguration = eventEmitter();

function makeTextDocument(params) {
    return {
        uri: vscode.Uri.file(params.uri),
        fileName: params.uri,
        languageId: params.languageId,
        version: params.version,
        isUntitled: !!params.isUntitled,
        // Whether it has unsaved changes, and how its lines end: both decide
        // what an extension writes back.
        isDirty: !!params.isDirty,
        isClosed: false,
        eol: params.eol === 2 ? 2 : 1, // EndOfLine.CRLF or LF
        _text: params.text || '',
        // With a range, the text in that range - an extension asking for the
        // text of a symbol means that symbol, not the whole file.
        getText(range) {
            if (!range || !range.start || !range.end)
                return this._text;
            return this._text.slice(this.offsetAt(range.start), this.offsetAt(range.end));
        },
        get lineCount() { return this._text.split('\n').length; },
        // Either a line number or a position, as extensions pass both.
        lineAt(lineOrPosition) {
            const line = typeof lineOrPosition === 'number' ? lineOrPosition
                                                           : lineOrPosition.line;
            const lines = this._text.split('\n');
            const text = lines[line] === undefined ? '' : lines[line];
            const leading = text.match(/^\s*/)[0].length;
            return {
                lineNumber: line,
                text,
                range: new Range(new Position(line, 0), new Position(line, text.length)),
                rangeIncludingLineBreak: new Range(
                    new Position(line, 0),
                    line + 1 < lines.length ? new Position(line + 1, 0)
                                            : new Position(line, text.length)),
                firstNonWhitespaceCharacterIndex: leading,
                isEmptyOrWhitespace: text.trim() === '',
            };
        },
        // What a hover or a definition is asked about: the word under the
        // cursor, which the caller then reads with getText().
        getWordRangeAtPosition(position, regexp) {
            const line = this.lineAt(position.line).text;
            const pattern = new RegExp(
                regexp ? regexp.source : "(-?\\d*\\.\\d\\w*)|([^`~!@#%^&*()\\-=+[{\\]}\\\\|;:'\",.<>/?\\s]+)",
                'g');
            for (let match = pattern.exec(line); match; match = pattern.exec(line)) {
                const start = match.index;
                const end = start + match[0].length;
                if (start <= position.character && position.character <= end) {
                    return new Range(new Position(position.line, start),
                                     new Position(position.line, end));
                }
                if (start > position.character)
                    break;
            }
            return undefined;
        },
        validatePosition(position) {
            const lines = this._text.split('\n');
            const line = Math.max(0, Math.min(position.line, lines.length - 1));
            return new Position(line, Math.max(0, Math.min(position.character, lines[line].length)));
        },
        validateRange(range) {
            return new Range(this.validatePosition(range.start),
                             this.validatePosition(range.end));
        },
        // Real conversions returning a real Position: extensions do arithmetic
        // on the result (positionAt(o).translate(...)), so a plain object with
        // the right fields is not enough - it throws, and an unhandled throw
        // here takes the whole host down.
        offsetAt(position) {
            const lines = this._text.split('\n');
            let offset = 0;
            for (let line = 0; line < position.line && line < lines.length; ++line)
                offset += lines[line].length + 1; // + the newline
            return offset + position.character;
        },
        positionAt(offset) {
            const clamped = Math.max(0, Math.min(offset, this._text.length));
            const before = this._text.slice(0, clamped);
            const line = (before.match(/\n/g) || []).length;
            return new Position(line, clamped - before.lastIndexOf('\n') - 1);
        },
        save() {
            if (this.isUntitled || (this.uri.scheme && this.uri.scheme !== 'file'))
                return Promise.resolve(false); // nowhere to save it to
            return request('document/save', {path: uriToString(this.uri)});
        },
    };
}

function makeTextEditor(document) {
    // There is always a selection, even before the cursor has moved: extensions
    // read editor.selection.active straight after being handed an editor.
    const start = new Selection(new Position(0, 0), new Position(0, 0));
    const editor = {
        document,
        selection: start,
        selections: [start],
        // What the editor is set to, which is what an extension formats to.
        options: {tabSize: editorOptions.tabSize, insertSpaces: editorOptions.insertSpaces,
                  cursorStyle: 1, lineNumbers: 1},
        viewColumn: 1,
        // The way an extension writes to the document it is showing.
        edit(callback) {
            const edits = [];
            const builder = {
                replace(rangeOrPosition, text) {
                    const range = rangeOrPosition instanceof Range
                        ? rangeOrPosition : new Range(rangeOrPosition, rangeOrPosition);
                    edits.push({range: rangeToJson(range), newText: String(text)});
                },
                insert(position, text) { builder.replace(new Range(position, position), text); },
                delete(range) { builder.replace(range, ''); },
                setEndOfLine() {},
            };
            try {
                callback(builder);
            } catch (e) {
                return Promise.reject(e);
            }
            if (!edits.length)
                return Promise.resolve(true);
            return request('workspace/applyEdit',
                           {changes: [{kind: 'edit', path: uriToString(document.uri), edits}]})
                .then(() => true);
        },
        insertSnippet(snippet, location) {
            const value = snippet && snippet.value !== undefined ? snippet.value : String(snippet);
            const at = location instanceof Range ? location.start
                                                 : (location || editor.selection.active);
            return request('editor/insertSnippet', {path: uriToString(document.uri),
                                                    snippet: value,
                                                    line: at.line,
                                                    character: at.character}).then(() => true);
        },
        // Scrolling to what the extension is talking about.
        // Where the range should end up on screen, which the caller says and
        // which is the difference between the line being visible and it being
        // where the eye already is.
        revealRange(range, revealType) {
            const at = (range && range.start) || editor.selection.active;
            return request('window/showTextDocument',
                           {path: uriToString(document.uri),
                            selection: {line: at.line, character: at.character},
                            revealType: revealType === undefined ? 0 : revealType});
        },
        setDecorations(type, rangesOrOptions) {
            const ranges = (rangesOrOptions || []).map(entry => {
                const range = entry && entry.range ? entry.range : entry;
                return rangeToJson(range);
            });
            notify('decoration/set', {path: uriToString(document.uri),
                                      key: type && type.key,
                                      ranges});
        },
        show: () => vscode.window.showTextDocument(document),
        hide: stub('TextEditor.hide', undefined),
    };
    return editor;
}

// --- geometry / diagnostics types -------------------------------------------

class Position {
    constructor(line, character) { this.line = line; this.character = character; }
    translate(dl = 0, dc = 0) { return new Position(this.line + dl, this.character + dc); }
    with(line = this.line, character = this.character) { return new Position(line, character); }
    isEqual(o) { return !!o && this.line === o.line && this.character === o.character; }
    compareTo(o) {
        if (this.line !== o.line)
            return this.line < o.line ? -1 : 1;
        if (this.character === o.character)
            return 0;
        return this.character < o.character ? -1 : 1;
    }
    isBefore(o) { return this.compareTo(o) < 0; }
    isBeforeOrEqual(o) { return this.compareTo(o) <= 0; }
    isAfter(o) { return this.compareTo(o) > 0; }
    isAfterOrEqual(o) { return this.compareTo(o) >= 0; }
}

class Range {
    constructor(a, b, c, d) {
        const [first, second] = typeof a === 'number'
            ? [new Position(a, b), new Position(c, d)] : [a, b];
        // Ends given the wrong way round are the same range, as an extension
        // that computed them from a backwards selection expects.
        const reversed = first.isAfter(second);
        this.start = reversed ? second : first;
        this.end = reversed ? first : second;
    }
    get isEmpty() { return this.start.isEqual(this.end); }
    get isSingleLine() { return this.start.line === this.end.line; }
    with(start = this.start, end = this.end) { return new Range(start, end); }
    isEqual(o) { return !!o && this.start.isEqual(o.start) && this.end.isEqual(o.end); }
    contains(positionOrRange) {
        const other = positionOrRange instanceof Range
            ? positionOrRange : new Range(positionOrRange, positionOrRange);
        return other.start.isAfterOrEqual(this.start) && other.end.isBeforeOrEqual(this.end);
    }
    intersection(other) {
        const start = this.start.isAfter(other.start) ? this.start : other.start;
        const end = this.end.isBefore(other.end) ? this.end : other.end;
        return start.isAfter(end) ? undefined : new Range(start, end);
    }
    union(other) {
        return new Range(this.start.isBefore(other.start) ? this.start : other.start,
                         this.end.isAfter(other.end) ? this.end : other.end);
    }
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

// A snippet is a little language, not a string: a placeholder is "${1:text}"
// and a tabstop "$1". Appending the bare text instead inserts the example as if
// it were the answer, and leaves the cursor nowhere.
class SnippetString {
    constructor(value) {
        this.value = value || '';
        this._tabstop = 1;
    }
    _number(number) { return number === undefined ? this._tabstop++ : number; }
    appendText(s) {
        // Literal text has to survive being read as snippet syntax.
        this.value += String(s).replace(/\\|\$|\}/g, match => '\\' + match);
        return this;
    }
    appendTabstop(number) { this.value += '$' + this._number(number); return this; }
    appendPlaceholder(value, number) {
        const stop = this._number(number);
        if (typeof value === 'function') {
            const nested = new SnippetString();
            value(nested);
            this.value += '${' + stop + ':' + nested.value + '}';
        } else {
            this.value += '${' + stop + ':' + String(value) + '}';
        }
        return this;
    }
    appendChoice(values, number) {
        const choices = (values || []).map(String).join(',');
        this.value += '${' + this._number(number) + '|' + choices + '|}';
        return this;
    }
    appendVariable(name, defaultValue) {
        if (typeof defaultValue === 'function') {
            const nested = new SnippetString();
            defaultValue(nested);
            this.value += '${' + name + ':' + nested.value + '}';
        } else if (defaultValue) {
            this.value += '${' + name + ':' + String(defaultValue) + '}';
        } else {
            this.value += '${' + name + '}';
        }
        return this;
    }
}

class MarkdownString {
    constructor(value, supportThemeIcons) {
        this.value = value || '';
        this.isTrusted = undefined;
        this.supportThemeIcons = supportThemeIcons;
        this.supportHtml = undefined;
    }
    appendText(s) {
        this.value += String(s).replace(/[\\`*_{}[\]()#+\-.!]/g, match => '\\' + match)
                          .replace(/\n/g, '\n\n');
        return this;
    }
    appendMarkdown(s) { this.value += s; return this; }
    appendCodeblock(value, language) {
        this.value += '\n```' + (language || '') + '\n' + value + '\n```\n';
        return this;
    }
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

// Additional vscode types. Many are subclassed by vscode-languageclient's
// protocol converter at module load, so they must exist as constructors.
class CodeLens {
    constructor(range, command) { this.range = range; this.command = command; }
    get isResolved() { return !!this.command; }
}
class CodeAction {
    constructor(title, kind) {
        this.title = title;
        this.kind = kind;
        this.edit = undefined;
        this.diagnostics = undefined;
        this.command = undefined;
    }
}
// The kind is Text, Read or Write; the editor marks all three the same way.
class DocumentHighlight {
    constructor(range, kind) {
        this.range = range;
        this.kind = kind === undefined ? 0 : kind;
    }
}
class DocumentLink {
    constructor(range, target) { this.range = range; this.target = target; }
}
class InlayHint {
    constructor(position, label, kind) {
        this.position = position;
        this.label = label;
        this.kind = kind;
    }
}
class SymbolInformation {
    constructor(name, kind, containerName, location) {
        this.name = name;
        this.kind = kind;
        this.containerName = containerName;
        this.location = location;
    }
}
class DocumentSymbol {
    constructor(name, detail, kind, range, selectionRange) {
        this.name = name;
        this.detail = detail;
        this.kind = kind;
        this.range = range;
        this.selectionRange = selectionRange;
        this.children = [];
    }
}
class CallHierarchyItem {
    constructor(kind, name, detail, uri, range, selectionRange) {
        Object.assign(this, {kind, name, detail, uri, range, selectionRange});
    }
}
class TypeHierarchyItem {
    constructor(kind, name, detail, uri, range, selectionRange) {
        Object.assign(this, {kind, name, detail, uri, range, selectionRange});
    }
}
class CancellationError extends Error {}

// What a file system operation fails with. Extensions read the code to tell
// "not there" from "not allowed" - a Node errno in its place means the branch
// they wrote for a missing file never runs - and throw these themselves from
// file system providers of their own.
class FileSystemError extends Error {
    constructor(messageOrUri, code) {
        const asString = messageOrUri && messageOrUri.path !== undefined
            ? uriToString(messageOrUri) : messageOrUri;
        super(asString === undefined ? code : String(asString));
        this.name = code;
        this.code = code;
    }
    static FileExists(uri) { return new FileSystemError(uri, 'FileExists'); }
    static FileNotFound(uri) { return new FileSystemError(uri, 'FileNotFound'); }
    static FileNotADirectory(uri) { return new FileSystemError(uri, 'FileNotADirectory'); }
    static FileIsADirectory(uri) { return new FileSystemError(uri, 'FileIsADirectory'); }
    static NoPermissions(uri) { return new FileSystemError(uri, 'NoPermissions'); }
    static Unavailable(uri) { return new FileSystemError(uri, 'Unavailable'); }
}

const fileSystemErrorCodes = {
    ENOENT: 'FileNotFound',
    EEXIST: 'FileExists',
    ENOTDIR: 'FileNotADirectory',
    EISDIR: 'FileIsADirectory',
    EACCES: 'NoPermissions',
    EPERM: 'NoPermissions',
};

// Anything the file system says, said the way an extension reads it.
function asFileSystemError(error, uri) {
    const code = (error && fileSystemErrorCodes[error.code]) || 'Unavailable';
    const translated = new FileSystemError(uri, code);
    translated.message = (error && error.message) || translated.message;
    return translated;
}

function fileSystemCall(uri, work) {
    return Promise.resolve().then(work).catch(error => {
        throw asFileSystemError(error, uri);
    });
}
class TelemetryTrustedValue {
    constructor(value) {
        this.value = value;
    }
}
class Selection extends Range {
    constructor(a, b, c, d) {
        super(a, b, c, d);
        // Which end the cursor is at is the selection's own business, and is
        // not the same as which end comes first in the document.
        const [anchor, active] = typeof a === 'number'
            ? [new Position(a, b), new Position(c, d)] : [a, b];
        this.anchor = anchor;
        this.active = active;
    }
    get isReversed() { return this.active.isBefore(this.anchor); }
}
class TextEdit {
    constructor(range, newText) { this.range = range; this.newText = newText; }
    static replace(range, newText) { return new TextEdit(range, newText); }
    static insert(position, newText) { return new TextEdit(new Range(position, position), newText); }
    static delete(range) { return new TextEdit(range, ''); }
    static setEndOfLine(eol) {
        const edit = new TextEdit(new Range(new Position(0, 0), new Position(0, 0)), '');
        edit.newEol = eol;
        return edit;
    }
}
class WorkspaceEdit {
    constructor() {
        this._edits = new Map(); // uri string -> {uri, edits}
        // Creating, renaming and deleting files are part of the same edit, and
        // a refactoring that does one of them and text edits means both or
        // neither. They are kept in the order they were added, because a file
        // created here is edited by the next one.
        this._operations = [];
    }
    _for(uri) {
        const key = uriToString(uri);
        if (!this._edits.has(key)) {
            this._edits.set(key, {uri, edits: []});
            this._operations.push({kind: 'edit', uri});
        }
        return this._edits.get(key);
    }
    replace(uri, range, newText) { this._for(uri).edits.push(new TextEdit(range, newText)); }
    insert(uri, position, newText) { this.replace(uri, new Range(position, position), newText); }
    delete(uri, range) { this.replace(uri, range, ''); }
    set(uri, edits) { this._for(uri).edits = (edits || []).slice(); }
    has(uri) { return this._edits.has(uriToString(uri)); }
    get(uri) { const e = this._edits.get(uriToString(uri)); return e ? e.edits : []; }
    get size() { return this._edits.size + this._operations.filter(o => o.kind !== 'edit').length; }
    entries() { return [...this._edits.values()].map(e => [e.uri, e.edits]); }
    createFile(uri, options) { this._operations.push({kind: 'create', uri, options: options || {}}); }
    deleteFile(uri, options) { this._operations.push({kind: 'delete', uri, options: options || {}}); }
    renameFile(oldUri, newUri, options) {
        this._operations.push({kind: 'rename', uri: oldUri, newUri, options: options || {}});
    }
    // The whole edit in the order it was built, which is the order it has to be
    // carried out in.
    operations() {
        return this._operations.map(operation => {
            if (operation.kind !== 'edit')
                return operation;
            return {kind: 'edit', uri: operation.uri,
                    edits: this._edits.get(uriToString(operation.uri)).edits};
        });
    }
}
function rangeToJson(range) {
    const point = position => ({line: (position && position.line) || 0,
                               character: (position && position.character) || 0});
    return {start: point(range && range.start), end: point(range && range.end)};
}

// What a tab holds. An extension asks "is this tab a file, and which one",
// which it does by testing the input's type.
class TabInputText {
    constructor(uri) { this.uri = uri; }
}
class TabInputTextDiff {
    constructor(original, modified) { this.original = original; this.modified = modified; }
}

class InlineCompletionItem {
    constructor(insertText, range, command) {
        this.insertText = insertText;
        this.range = range;
        this.command = command;
    }
}
class InlineCompletionList {
    constructor(items = []) { this.items = items; }
}
class CompletionList {
    constructor(items = [], isIncomplete = false) {
        this.items = items;
        this.isIncomplete = isIncomplete;
    }
}
class SelectionRange {
    constructor(range, parent) { this.range = range; this.parent = parent; }
}
class Color {
    constructor(red, green, blue, alpha) {
        this.red = red; this.green = green; this.blue = blue;
        this.alpha = alpha === undefined ? 1 : alpha;
    }
}
class ColorInformation {
    constructor(range, color) { this.range = range; this.color = color; }
}
class ColorPresentation {
    constructor(label) { this.label = label; }
}
class FoldingRange {
    constructor(start, end, kind) { this.start = start; this.end = end; this.kind = kind; }
}
class SignatureHelp {
    constructor() { this.signatures = []; this.activeSignature = 0; this.activeParameter = 0; }
}
class SignatureInformation {
    constructor(label, documentation) {
        this.label = label;
        this.documentation = documentation;
        this.parameters = [];
    }
}
class ParameterInformation {
    constructor(label, documentation) { this.label = label; this.documentation = documentation; }
}
class DiagnosticRelatedInformation {
    constructor(location, message) { this.location = location; this.message = message; }
}
class ThemeIcon {
    constructor(id, color) { this.id = id; this.color = color; }
}

// A single well-known button, which is how a multi-step flow offers "back".
const QuickInputButtons = {Back: {iconPath: new ThemeIcon('arrow-left'), tooltip: 'Back'}};

// Tree items are given these by name rather than constructed.
ThemeIcon.File = new ThemeIcon('file');
ThemeIcon.Folder = new ThemeIcon('folder');
class ThemeColor {
    constructor(id) { this.id = id; }
}
class RelativePattern {
    // The base is a WorkspaceFolder, a Uri or a plain path. Extensions pass the
    // folder itself as often as a Uri, and then base has to be resolved through
    // folder.uri or it ends up an object where a path is expected.
    constructor(base, pattern) {
        const uri = (base && base.uri) || base;
        this.baseUri = typeof uri === 'string' ? vscode.Uri.file(uri) : uri;
        this.base = typeof uri === 'string' ? uri : (uri && uri.fsPath);
        this.pattern = pattern;
    }
}
// Extensions hand the token to something long-running and cancel it when the
// answer stopped being wanted. A cancel that does nothing leaves that work
// running, and the extension waiting for it.
class CancellationTokenSource {
    constructor() {
        const listeners = new Set();
        this._listeners = listeners;
        this.token = {
            isCancellationRequested: false,
            onCancellationRequested: (listener) => {
                if (this.token.isCancellationRequested) {
                    setImmediate(() => listener());
                    return disposable(() => {});
                }
                listeners.add(listener);
                return disposable(() => listeners.delete(listener));
            },
        };
    }
    cancel() {
        if (this.token.isCancellationRequested)
            return;
        this.token.isCancellationRequested = true;
        for (const listener of [...this._listeners]) {
            try { listener(); } catch (e) { logToStderr('cancellation listener', e); }
        }
        this._listeners.clear();
    }
    dispose() { this._listeners.clear(); }
}

// Task and test classes. Extensions subclass and instantiate these while their
// module is still loading, i.e. before any API call, so they have to exist even
// though nothing runs tasks or tests yet -- cmake-tools does
// `class CMakeTask extends vscode.Task` at load time and dies without it. They
// are plain data holders, exactly as the real ones are.
class Task {
    // Two signatures are in use: the current one takes a scope second, the
    // pre-1.30 one goes straight to name. Detect by whether the second
    // argument is a string.
    constructor(taskDefinition, ...rest) {
        this.definition = taskDefinition;
        if (typeof rest[0] === 'string')
            [this.name, this.source, this.execution, this.problemMatchers] = rest;
        else
            [this.scope, this.name, this.source, this.execution, this.problemMatchers] = rest;
        this.problemMatchers = this.problemMatchers || [];
        this.isBackground = false;
        this.detail = undefined;
        this.group = undefined;
        this.presentationOptions = {};
        this.runOptions = {};
    }
}
class CustomExecution {
    constructor(callback) { this.callback = callback; }
}
// Two shapes, as in the API: a whole command line to hand to a shell, or a
// command with its arguments kept apart.
class ShellExecution {
    constructor(commandOrLine, argsOrOptions, options) {
        if (Array.isArray(argsOrOptions)) {
            this.command = commandOrLine;
            this.args = argsOrOptions;
            this.options = options || {};
        } else {
            this.commandLine = commandOrLine;
            this.args = [];
            this.options = argsOrOptions || {};
        }
    }
}
class ProcessExecution {
    constructor(process, argsOrOptions, options) {
        this.process = process;
        this.args = Array.isArray(argsOrOptions) ? argsOrOptions : [];
        this.options = (Array.isArray(argsOrOptions) ? options : argsOrOptions) || {};
    }
}
class TestMessage {
    constructor(message) { this.message = message; }
    static diff(message, expected, actual) {
        const m = new TestMessage(message);
        m.expectedOutput = expected;
        m.actualOutput = actual;
        return m;
    }
}
class TestRunRequest {
    constructor(include, exclude, profile, continuous) {
        this.include = include;
        this.exclude = exclude;
        this.profile = profile;
        this.continuous = !!continuous;
    }
}
class TestTag {
    constructor(id) { this.id = id; }
}
class TestCoverageCount {
    constructor(covered, total) { this.covered = covered; this.total = total; }
}
class FileCoverage {
    constructor(uri, statementCoverage, branchCoverage, declarationCoverage) {
        this.uri = uri;
        this.statementCoverage = statementCoverage;
        this.branchCoverage = branchCoverage;
        this.declarationCoverage = declarationCoverage;
    }
    static fromDetails(uri, details) {
        const coverage = new FileCoverage(uri, new TestCoverageCount(0, 0));
        coverage.detailedCoverage = details;
        return coverage;
    }
}
class StatementCoverage {
    constructor(executed, location, branches) {
        this.executed = executed;
        this.location = location;
        this.branches = branches || [];
    }
}
class BranchCoverage {
    constructor(executed, location, label) {
        this.executed = executed;
        this.location = location;
        this.label = label;
    }
}
class DeclarationCoverage {
    constructor(name, executed, location) {
        this.name = name;
        this.executed = executed;
        this.location = location;
    }
}
class DebugAdapterNamedPipeServer {
    constructor(path) { this.path = path; }
}
class DebugAdapterExecutable {
    constructor(command, args, options) {
        this.command = command;
        this.args = args || [];
        this.options = options || {};
    }
}
class DebugAdapterServer {
    constructor(port, host) {
        this.port = port;
        this.host = host;
    }
}
class DebugAdapterInlineImplementation {
    constructor(implementation) { this.implementation = implementation; }
}
class SemanticTokensLegend {
    constructor(tokenTypes, tokenModifiers) {
        this.tokenTypes = tokenTypes || [];
        this.tokenModifiers = tokenModifiers || [];
    }
}
class SemanticTokens {
    constructor(data, resultId) { this.data = data; this.resultId = resultId; }
}
class SemanticTokensBuilder {
    constructor(legend) {
        this.legend = legend || new SemanticTokensLegend([], []);
        this._tokens = [];
    }
    // Either the numbers, or a range with the names the legend lists.
    push(lineOrRange, charOrType, lengthOrModifiers, tokenType, tokenModifiers) {
        if (typeof lineOrRange === 'number') {
            this._tokens.push({line: lineOrRange, char: charOrType, length: lengthOrModifiers,
                               type: tokenType || 0, modifiers: tokenModifiers || 0});
            return;
        }
        const range = lineOrRange;
        if (!range || !range.start || range.start.line !== range.end.line)
            return; // a token is one line, as the protocol has it
        let modifiers = 0;
        for (const name of lengthOrModifiers || []) {
            const bit = this.legend.tokenModifiers.indexOf(name);
            if (bit >= 0)
                modifiers |= (1 << bit);
        }
        const type = this.legend.tokenTypes.indexOf(charOrType);
        this._tokens.push({line: range.start.line, char: range.start.character,
                           length: range.end.character - range.start.character,
                           type: type < 0 ? 0 : type, modifiers});
    }
    // The wire format is relative: each token counts from the one before it,
    // so what was collected in absolute terms is turned into deltas here.
    build(resultId) {
        const sorted = [...this._tokens].sort((a, b) => a.line - b.line || a.char - b.char);
        const data = [];
        let line = 0;
        let char = 0;
        for (const token of sorted) {
            const deltaLine = token.line - line;
            data.push(deltaLine,
                      deltaLine === 0 ? token.char - char : token.char,
                      token.length, token.type, token.modifiers);
            line = token.line;
            char = token.char;
        }
        return new SemanticTokens(new Uint32Array(data), resultId);
    }
}

function makeCodeActionKind(value) {
    return {value, append: sub => makeCodeActionKind(value ? value + '.' + sub : sub)};
}

function uriToString(uri) {
    if (typeof uri === 'string')
        return uri;
    return (uri && (uri.fsPath || uri.path)) || String(uri);
}

// vscode.Uri. A class, because extensions do "x instanceof vscode.Uri"
// (redhat.java) and read the parts of a remote authority (remote-ssh).
const isWindowsHost = process.platform === 'win32';

function isDriveLetterPath(path) {
    return path.length >= 3 && path[0] === '/' && path[2] === ':'
           && /[a-zA-Z]/.test(path[1]);
}

class Uri {
    constructor(scheme, authority, path, query, fragment) {
        this.scheme = scheme || '';
        this.authority = authority || '';
        this.path = path || '';
        this.query = query || '';
        this.fragment = fragment || '';
    }
    // The name of the file as the file system knows it, which is not the path
    // part of the URI: that keeps a slash in front of a Windows drive letter,
    // and keeps a UNC host in the authority where a path wants it back.
    get fsPath() {
        let value;
        if (this.authority && this.path.length > 1 && this.scheme === 'file')
            value = '//' + this.authority + this.path;
        else if (isDriveLetterPath(this.path))
            value = this.path[1].toLowerCase() + this.path.substr(2);
        else
            value = this.path;
        return isWindowsHost ? value.replace(/\//g, '\\') : value;
    }
    with(change) {
        const part = (name) => (change && change[name] !== undefined ? change[name] : this[name]);
        return new Uri(part('scheme'), part('authority'), part('path'), part('query'),
                       part('fragment'));
    }
    toString(skipEncoding) {
        if (!this.scheme)
            return this.path;
        const encode = value => (skipEncoding ? value : encodeURI(value).replace(/[?#]/g, match =>
            (match === '?' ? '%3F' : '%23')));
        let result = this.scheme + ':';
        // Only a URI that has an authority gets the double slash; "untitled:x"
        // with one is a different URI, and not the one an extension gave us.
        if (this.authority || this.scheme === 'file')
            result += '//' + this.authority;
        result += encode(this.path);
        if (this.query)
            result += '?' + encode(this.query);
        if (this.fragment)
            result += '#' + encode(this.fragment);
        return result;
    }
    toJSON() {
        return {scheme: this.scheme, authority: this.authority, path: this.path,
                query: this.query, fragment: this.fragment, fsPath: this.fsPath};
    }
    static file(path) {
        let value = String(path).replace(/\\/g, '/');
        let authority = '';
        // A UNC name carries its host, which belongs in the authority.
        if (value[0] === '/' && value[1] === '/') {
            const end = value.indexOf('/', 2);
            if (end === -1) {
                authority = value.substring(2);
                value = '/';
            } else {
                authority = value.substring(2, end);
                value = value.substring(end) || '/';
            }
        } else if (value[0] !== '/') {
            value = '/' + value;
        }
        return new Uri('file', authority, value);
    }
    static joinPath(base, ...segments) {
        // Normalizing matters: extensions use joinPath(file, '..') to get at
        // the parent and then mkdir it, which without this would create the
        // file itself as a directory (redhat.java).
        const joined = nodePath.posix.join(base.path, ...segments.map(String));
        return new Uri(base.scheme, base.authority, joined, base.query, base.fragment);
    }
    static parse(value) {
        const parts = /^([a-zA-Z][\w+.-]*):(?:\/\/([^/?#]*))?([^?#]*)(?:\?([^#]*))?(?:#(.*))?$/
                          .exec(value);
        if (!parts)
            return new Uri('', '', value);
        // What the URI escaped is what the path is: a file with a space in its
        // name arrives as %20 and is opened by its real name.
        const decode = part => {
            if (part === undefined)
                return part;
            try { return decodeURIComponent(part); } catch (e) { return part; }
        };
        return new Uri(parts[1], decode(parts[2]), decode(parts[3]), decode(parts[4]),
                       decode(parts[5]));
    }
}

class Hover {
    constructor(contents, range) {
        this.contents = contents;
        this.range = range;
    }
}

// What a terminal profile provider hands back: the options a terminal is to be
// made with, wrapped in the type the API names.
class TerminalProfile {
    constructor(options) { this.options = options; }
}

class FileDecoration {
    constructor(badge, tooltip, color) {
        this.badge = badge;
        this.tooltip = tooltip;
        this.color = color;
        this.propagate = false;
    }
}

// vscode.EventEmitter: extensions do `new EventEmitter(); this.onX = e.event`.
class EventEmitter {
    constructor() {
        this._emitter = eventEmitter();
        this.event = this._emitter;
        // Bound, because extensions hand it out detached: cmake-tools passes
        // emitter.fire as a plain callback argument.
        this.fire = data => this._emitter.fire(data);
    }
    // Nothing is listening to a disposed emitter any more: leaving them
    // attached keeps calling into an extension that has finished with it, and
    // keeps alive everything the listeners close over.
    dispose() { this._emitter.clear(); }
}

class TreeItem {
    constructor(label, collapsibleState) {
        this.label = label;
        this.collapsibleState = collapsibleState || 0;
        this.description = undefined;
        this.tooltip = undefined;
        this.contextValue = undefined;
        this.command = undefined;
        this.iconPath = undefined;
        this.id = undefined;
    }
}

// --- language feature providers ---------------------------------------------

const completionProviders = [];
const formattingProviders = [];
const rangeFormattingProviders = [];
const codeActionProviders = [];
const renameProviders = [];
const referenceProviders = [];
const symbolProviders = [];
const signatureProviders = [];
const workspaceSymbolProviders = [];
const typeDefinitionProviders = [];
// What languages a feature still has a provider for. Sent on every register
// and every dispose, because an extension being switched off has to take its
// folds and colours off the document with it - not leave them until the user
// happens to type.
function announceLanguages(method, providers) {
    const ids = new Set();
    for (const {selector} of providers) {
        for (const id of selectorLanguageIds(selector))
            ids.add(id);
    }
    notify(method, {languageIds: [...ids]});
}

const onTypeProviders = [];

// Which languages still have a provider, and which characters set it off. Qt
// Creator only asks when one of those is typed, so it needs both.
function announceOnTypeFormatting() {
    const ids = new Set();
    const triggers = new Set();
    for (const {selector, triggers: own} of onTypeProviders) {
        for (const id of selectorLanguageIds(selector))
            ids.add(id);
        for (const character of own)
            triggers.add(character);
    }
    notify('onTypeFormatting/registerProvider',
           {languageIds: [...ids], triggerCharacters: [...triggers]});
}

const inlineCompletionProviders = [];
const semanticProviders = [];
const foldingProviders = [];
const colorProviders = [];
const codeLensProviders = [];
const highlightProviders = [];
const linkProviders = []; // {selector, provider}
const hoverProviders = [];
const definitionProviders = [];
const treeDataProviders = new Map(); // viewId -> {provider, elements: Map, counter}
const webviews = new Map(); // id -> {onMessage, onDispose}
const registeredExtensions = new Map(); // id -> {exports, path, packageJSON}
let configuration = {}; // flat dotted-key configuration pushed from Qt Creator

// Defaults declared by extensions in contributes.configuration. VS Code hands
// these out for any setting the user has not overridden, and extensions rely on
// it: cmake-tools reads config.options.advanced straight out of its own
// declared default and crashes on undefined without it.
const configurationDefaults = new Map(); // dotted key -> default value

function registerConfigurationDefaults(packageJson) {
    const contributed = (packageJson && packageJson.contributes
                         && packageJson.contributes.configuration) || [];
    for (const block of Array.isArray(contributed) ? contributed : [contributed]) {
        for (const [key, schema] of Object.entries((block && block.properties) || {})) {
            if (schema && Object.prototype.hasOwnProperty.call(schema, 'default'))
                configurationDefaults.set(key, schema.default);
        }
    }
}
let nextStatusBarItemId = 1;
let nextWebviewId = 1;
let nextQuickPickId = 1;
let nextProgressId = 1;
const progressCancellations = new Map(); // progress id -> its token source

// Looks up how a contributed view is presented: its name, and the title of the
// container it lives in ("Gerrit AI" > "Dashboard").
function viewContributionOf(viewId) {
    for (const registered of registeredExtensions.values()) {
        const contributes = (registered.packageJSON || {}).contributes || {};
        for (const [containerId, views] of Object.entries(contributes.views || {})) {
            for (const view of views) {
                if (view.id !== viewId)
                    continue;
                let container = '';
                for (const group of Object.values(contributes.viewsContainers || {})) {
                    const match = group.find(c => c.id === containerId);
                    if (match)
                        container = match.title || '';
                }
                return {name: view.name || '', container};
            }
        }
    }
    return {name: '', container: ''};
}

function viewNameOf(viewId) { return viewContributionOf(viewId).name; }
function viewContainerOf(viewId) { return viewContributionOf(viewId).container; }

// An icon is a name from the icon font, a file, or one file per theme. Qt
// Creator resolves the name itself, so it travels as the same "$(name)" markup
// labels carry.
function iconSpecOf(iconPath) {
    if (!iconPath)
        return '';
    if (typeof iconPath === 'string')
        return iconPath;
    if (iconPath.id)
        return '$(' + iconPath.id + ')';
    if (iconPath.fsPath)
        return iconPath.fsPath;
    const themed = iconPath.dark || iconPath.light;
    return themed ? (themed.fsPath || String(themed)) : '';
}

const terminalProfileProviders = new Map(); // profile id -> provider

// What each registered profile is called, which only the contributing manifest
// knows.
function announceTerminalProfiles() {
    const profiles = [];
    for (const id of terminalProfileProviders.keys()) {
        let title = id;
        let icon = '';
        for (const registered of registeredExtensions.values()) {
            const contributed = ((registered.packageJSON || {}).contributes || {}).terminal || {};
            for (const profile of contributed.profiles || []) {
                if (profile.id === id) {
                    title = profile.title || id;
                    icon = profile.icon || '';
                }
            }
        }
        profiles.push({id, title, icon});
    }
    notify('terminalProfiles/changed', {profiles});
}

onRequest('terminalProfile/open', async params => {
    const provider = terminalProfileProviders.get(params.id);
    if (!provider || typeof provider.provideTerminalProfile !== 'function')
        return {opened: false};
    const profile = await provider.provideTerminalProfile(cancellationToken);
    const options = (profile && profile.options) || profile;
    if (!options)
        return {opened: false};
    // A profile is a terminal the user asked for, so it opens a shell whether
    // or not the extension named one - unlike a terminal an extension makes to
    // run particular commands in.
    const terminal = vscode.window.createTerminal({...options, _shellWanted: true});
    terminal.show();
    return {opened: true};
});

function registerTreeProvider(viewId, provider) {
    const entry = {provider, elements: new Map(), counter: 0};
    treeDataProviders.set(viewId, entry);
    if (provider.onDidChangeTreeData)
        provider.onDidChangeTreeData(() => notify('treeview/refresh', {viewId}));
    // The view's human name lives in the contributing manifest; the id is
    // what the provider registers with.
    notify('treeview/register', {viewId, name: viewNameOf(viewId), container: viewContainerOf(viewId)});
    return disposable(() => {
        treeDataProviders.delete(viewId);
        notify('treeview/unregister', {viewId});
    });
}

async function treeChildren(viewId, id) {
    const entry = treeDataProviders.get(viewId);
    if (!entry)
        return [];
    const parent = id ? entry.elements.get(id) : undefined;
    const children = (await entry.provider.getChildren(parent)) || [];
    const nodes = [];
    for (const child of children) {
        const item = await entry.provider.getTreeItem(child);
        const nodeId = viewId + ':' + (entry.counter++);
        entry.elements.set(nodeId, child);
        const label = (item.label && typeof item.label === 'object') ? item.label.label : item.label;
        nodes.push({
            id: nodeId,
            label: label || '',
            description: item.description === true ? '' : (item.description || ''),
            tooltip: textOf(item.tooltip) || '',
            collapsibleState: item.collapsibleState || 0,
            contextValue: item.contextValue || '',
            icon: iconSpecOf(item.iconPath),
            // What runs when the item is picked, arguments and all.
            command: item.command ? item.command.command : '',
            commandArguments: (item.command && item.command.arguments) || [],
        });
    }
    return nodes;
}

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

// Where an item says it goes, rather than where the editor would guess. The
// range is either a TextEdit's own or the item's, which may name the two cases
// (replacing what is there, inserting before it) separately.
function completionRange(item) {
    if (item.textEdit)
        return rangeToJson(item.textEdit.range);
    const range = item.range;
    if (!range)
        return null;
    return rangeToJson(range.replacing || range.inserting || range);
}

function serializeCompletion(item) {
    if (typeof item === 'string')
        item = new CompletionItem(item);
    const label = typeof item.label === 'string' ? item.label : (item.label && item.label.label) || '';
    const insert = item.insertText;
    const isSnippet = !!(insert && typeof insert === 'object');
    const insertText = isSnippet ? insert.value : (insert !== undefined ? insert : label);
    return {
        label,
        insertText: item.textEdit ? (item.textEdit.newText || '') : insertText,
        isSnippet,
        range: completionRange(item),
        // Typing one of these takes the item and the character both.
        commitCharacters: item.commitCharacters || [],
        kind: item.kind,
        detail: typeof item.label === 'object' ? item.label.detail : item.detail,
        documentation: textOf(item.documentation),
        sortText: item.sortText,
        filterText: item.filterText,
        // What else has to change for this item to make sense - the import
        // line a symbol needs, most of the time.
        additionalTextEdits: (item.additionalTextEdits || []).map(edit => ({
            range: rangeToJson(edit.range),
            newText: edit.newText || '',
        })),
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
        // Unnecessary or deprecated: what the range says about itself, beyond
        // being wrong.
        tags: d.tags || [],
        relatedInformation: (d.relatedInformation || []).map(related => ({
            uri: uriToString(related.location.uri),
            range: rangeToJson(related.location.range),
            message: related.message || '',
        })),
    };
}

// Every live collection's store, so that what one extension reported can be
// read back by another - and by itself.
const diagnosticStores = new Set(); // Set<Map<uri string, Diagnostic[]>>
const onDidChangeDiagnostics = eventEmitter();

// What "when" clauses are evaluated against: the keys an extension sets with
// setContext, over the ones the environment answers by itself.
const contextKeys = new Map();
const editorFocusState = {focused: false, readOnly: false};
const editorOptions = {tabSize: 4, insertSpaces: true};

function contextValueOfKey(key) {
    if (contextKeys.has(key))
        return contextKeys.get(key);
    const editor = vscode.window.activeTextEditor;
    const document = editor && editor.document;
    switch (key) {
    case 'isLinux': return process.platform === 'linux';
    case 'isMac': return process.platform === 'darwin';
    case 'isWindows': return process.platform === 'win32';
    case 'workspaceFolderCount': return (vscode.workspace.workspaceFolders || []).length;
    case 'editorIsOpen': return !!document;
    // Whether the editor has the keyboard, which is what gates most of what an
    // extension contributes to it. Qt Creator is the one that knows.
    case 'editorFocus':
    case 'editorTextFocus': return editorFocusState.focused;
    case 'editorReadonly': return editorFocusState.readOnly;
    case 'editorLangId':
    case 'resourceLangId': return document ? document.languageId : undefined;
    case 'resourceScheme': return document ? (document.uri.scheme || 'file') : undefined;
    case 'resourceFilename': return document ? nodePath.basename(document.uri.path) : undefined;
    case 'resourceExtname': return document ? nodePath.extname(document.uri.path) : undefined;
    case 'resourcePath': return document ? document.uri.fsPath : undefined;
    case 'resourceDirname': return document ? nodePath.dirname(document.uri.fsPath) : undefined;
    // Qt Creator asks nobody whether a project may be opened, so everything it
    // has open is trusted. Left unanswered, a clause guarded by this is read as
    // untrusted and hides what it guards.
    case 'isWorkspaceTrusted': return true;
    case 'virtualWorkspace': return false;
    case 'isWeb': return false;
    case 'remoteName': return vscode.env.remoteName || '';
    }
    if (key.startsWith('config.'))
        return vscode.workspace.getConfiguration().get(key.slice('config.'.length));
    return undefined;
}

// What the command palette - Qt Creator's locator - may offer: the commands an
// extension contributes in its manifest, minus those its "commandPalette"
// entries rule out. A command registered without being contributed is not
// offered at all: that is how an extension keeps the commands meant for a
// context menu out of the palette.
function paletteCommands() {
    const visible = [];
    for (const registered of registeredExtensions.values()) {
        const contributes = (registered.packageJSON || {}).contributes || {};
        const rules = new Map();
        for (const entry of (contributes.menus || {}).commandPalette || [])
            rules.set(entry.command, entry.when);
        for (const command of contributes.commands || []) {
            const id = command.command;
            if (!commandHandlers.has(id))
                continue; // contributed, but the extension never registered it
            const when = rules.get(id);
            if (when !== undefined && !evaluateWhen(when, contextValueOfKey))
                continue;
            visible.push(id);
        }
    }
    return visible;
}

// What an extension offers at one place in the editor - the buttons above it
// ("editor/title") and the entries in its context menu ("editor/context") - as
// its "when" clauses allow for the file being edited right now.
function contributedMenuItems(location, lookup) {
    const value = lookup || contextValueOfKey;
    const shown = [];
    for (const registered of registeredExtensions.values()) {
        const contributes = (registered.packageJSON || {}).contributes || {};
        const titles = new Map();
        for (const command of contributes.commands || [])
            titles.set(command.command, command.title || command.command);
        const submenuTitles = new Map();
        for (const submenu of contributes.submenus || [])
            submenuTitles.set(submenu.id, submenu.label || submenu.id);
        const menus = contributes.menus || {};

        // An entry names a command or a submenu; the submenu's own entries are
        // a menu of their own, listed under its id.
        const collect = (entries, into, depth) => {
            for (const entry of entries || []) {
                if (entry.when !== undefined && !evaluateWhen(entry.when, value))
                    continue;
                if (entry.submenu) {
                    if (depth > 3)
                        continue;
                    const items = [];
                    collect(menus[entry.submenu], items, depth + 1);
                    if (items.length) {
                        into.push({submenu: submenuTitles.get(entry.submenu) || entry.submenu,
                                   items});
                    }
                    continue;
                }
                const id = entry.command;
                if (!id || !commandHandlers.has(id))
                    continue;
                into.push({command: id, title: titles.get(id) || id});
            }
        };
        collect(menus[location], shown, 0);
    }
    return shown;
}

// What a view says when it has nothing to show - "no project configured yet",
// with a link to the command that fixes it. The first entry whose clause holds
// wins, the way the first matching rule does in VS Code.
function viewsWelcome() {
    const shown = [];
    const taken = new Set();
    for (const registered of registeredExtensions.values()) {
        const contributes = (registered.packageJSON || {}).contributes || {};
        for (const entry of contributes.viewsWelcome || []) {
            if (!entry.view || !entry.contents || taken.has(entry.view))
                continue;
            if (entry.when !== undefined && !evaluateWhen(entry.when, contextValueOfKey))
                continue;
            taken.add(entry.view);
            shown.push({view: entry.view, contents: entry.contents});
        }
    }
    return shown;
}

let lastPaletteCommands = '';
let lastViewsWelcome = '';
const lastMenuItems = new Map(); // location -> what was last sent
function updateContributedMenus() {
    const commands = paletteCommands();
    const key = commands.join('\n');
    if (key !== lastPaletteCommands) {
        lastPaletteCommands = key;
        notify('commands/palette', {commands});
    }
    for (const [location, method] of [['editor/title', 'menus/editorTitle'],
                                      ['editor/context', 'menus/editorContext']]) {
        const items = contributedMenuItems(location);
        const itemsKey = JSON.stringify(items);
        if (itemsKey === lastMenuItems.get(location))
            continue;
        lastMenuItems.set(location, itemsKey);
        notify(method, {items});
    }

    const welcome = viewsWelcome();
    const welcomeKey = JSON.stringify(welcome);
    if (welcomeKey !== lastViewsWelcome) {
        lastViewsWelcome = welcomeKey;
        notify('views/welcome', {items: welcome});
    }
}

// Answering one of these means running what Qt Creator would have asked for,
// so the handler is called directly rather than duplicated.
function callHandler(method, params) {
    const handler = requestHandlers.get(method);
    return handler ? Promise.resolve(handler(params)) : Promise.resolve(undefined);
}

const builtinCommands = {
    setContext: (key, value) => {
        contextKeys.set(key, value);
        updateContributedMenus(); // a clause may turn on a command
    },
    'vscode.open': (uri, options) => vscode.window.showTextDocument(uri, options),
    // Commands about the editor itself, which extensions call as readily as
    // their own: a button that opens its settings, a "format this" menu entry.
    'workbench.action.openSettings': query =>
        request('command/openSettings', {query: query === undefined ? ''
                                                                   : String(query)}),
    'editor.action.formatDocument': () => request('command/formatDocument', {}),
    'workbench.action.files.save': () => vscode.workspace.saveAll(),
    'workbench.action.files.saveAll': () => vscode.workspace.saveAll(),
    'revealFileInOS': uri => request('command/revealInFileManager', {path: uriToString(uri)}),
    'revealInExplorer': uri => request('command/revealInFileManager', {path: uriToString(uri)}),
    'vscode.openFolder': uri => request('command/openFolder', {path: uriToString(uri)}),
    'copyFilePath': uri => request('command/copyFilePath',
                                   {path: uri === undefined ? '' : uriToString(uri)}),
    'workbench.action.quickOpen': prefix =>
        request('command/quickOpen', {text: prefix === undefined ? '' : String(prefix)}),
    'workbench.panel.markers.view.focus': () => request('command/showIssues', {}),
    'markdown.showPreview': uri => request('command/showMarkdownPreview',
                                           {path: uri === undefined ? '' : uriToString(uri)}),
    'editor.action.rename': () => request('command/renameSymbol', {}),
    'extension.open': id => request('command/openExtension',
                                    {id: id === undefined ? '' : String(id)}),
    'vscode.executeDocumentSymbolProvider': uri =>
        callHandler('symbols/provide', {uri: uriToString(uri)}),
    'vscode.executeWorkspaceSymbolProvider': query =>
        callHandler('workspaceSymbols/provide', {query: query === undefined ? '' : String(query)}),
    'vscode.executeFormatDocumentProvider': uri =>
        callHandler('formatting/provide', {uri: uriToString(uri), tabSize: 4, insertSpaces: true}),
    'vscode.executeFormatRangeProvider': (uri, range) =>
        callHandler('rangeFormatting/provide',
                    {uri: uriToString(uri), range: rangeToJson(range),
                     tabSize: 4, insertSpaces: true}),
    // Running another provider's answer through, which is what an extension
    // does to reuse what a language server already knows.
    'vscode.executeHoverProvider': async (uri, position) => {
        const document = documents.get(uriToString(uri));
        if (!document)
            return [];
        const hovers = [];
        for (const {selector, provider} of hoverProviders) {
            if (!matchDocumentSelector(document, selector))
                continue;
            const hover = await provider.provideHover(document, position, cancellationToken);
            if (hover && hover.contents !== null && hover.contents !== undefined)
                hovers.push(hover);
        }
        return hovers;
    },
};

const vscode = {
    commands: {
        registerCommand(command, callback, thisArg) {
            commandHandlers.set(command, thisArg ? callback.bind(thisArg) : callback);
            notify('commands/register', {command});
            updateContributedMenus();
            return disposable(() => {
                commandHandlers.delete(command);
                notify('commands/unregister', {command});
                updateContributedMenus();
            });
        },
        executeCommand(command, ...args) {
            const handler = commandHandlers.get(command);
            if (handler)
                return Promise.resolve().then(() => handler(...args));
            const builtin = builtinCommands[command];
            if (builtin)
                return Promise.resolve().then(() => builtin(...args));
            // Asking for a command is what starts the extension that has it,
            // and extensions do lean on each other that way. Wake it and ask
            // again, so the caller gets that command's own answer and not a
            // silent undefined.
            return request('command/wakeOwner', {command}).then(woken => {
                const late = woken ? commandHandlers.get(command) : undefined;
                if (late)
                    return late(...args);
                // The rest of the built-in commands (workbench.*, the editor's
                // own) are not implemented. Resolving to undefined keeps the
                // extension running, but say which one it was.
                reportMissing('commands.executeCommand("' + command + '")');
                return undefined;
            });
        },
    },
    window: {
        activeTextEditor: undefined,
        visibleTextEditors: [],
        onDidChangeActiveTextEditor,
        showInformationMessage: (message, ...items) => showMessage('info', message, items),
        showWarningMessage: (message, ...items) => showMessage('warn', message, items),
        showErrorMessage: (message, ...items) => showMessage('error', message, items),
        async showQuickPick(items, options) {
            const resolved = await items;
            // An item is more than its label: what it is, and where it comes
            // from, is often the only thing telling two of them apart.
            const shown = resolved.map(i => (typeof i === 'string'
                ? {label: i, description: '', detail: ''}
                : {label: i.label || '', description: i.description || '',
                   detail: i.detail || ''}));
            const many = !!(options && options.canPickMany);
            const answer = await request('window/showQuickPick', {
                items: shown.map(i => i.label),
                shown,
                placeholder: (options && options.placeHolder) || '',
                canPickMany: many,
            });
            // Picking several answers with several, which is what the caller
            // then iterates; one index answers with the item itself.
            if (many) {
                return Array.isArray(answer) ? answer.map(i => resolved[i]) : undefined;
            }
            return answer >= 0 ? resolved[answer] : undefined;
        },
        async showInputBox(options) {
            const value = await request('window/showInputBox', {
                prompt: (options && options.prompt) || '',
                value: (options && options.value) || '',
                placeholder: (options && options.placeHolder) || '',
                // A prompt for a token is not one to type in the clear.
                password: !!(options && options.password),
            });
            return value === null ? undefined : value;
        },
        // The object form of the two dialogs above, which is what a multi-step
        // flow is built out of: make one, fill it in, show it, wait for an
        // event. What it cannot do is change while it is up - Qt Creator is
        // shown the items as they are at show() - and it says so rather than
        // quietly keeping the old list.
        createQuickPick() {
            const pickId = 'quickpick-' + (nextQuickPickId++);
            const onDidAccept = eventEmitter();
            const onDidHide = eventEmitter();
            const onDidChangeSelection = eventEmitter();
            const onDidChangeActive = eventEmitter();
            const onDidChangeValue = eventEmitter();
            // Buttons live in the dialog's own furniture, which Qt Creator's
            // has none of - so none is ever pressed. An extension still asks to
            // hear about them before it does anything else, and one that cannot
            // stops there.
            const onDidTriggerButton = eventEmitter();
            const onDidTriggerItemButton = eventEmitter();
            let shown = false;
            const pick = {
                title: undefined,
                placeholder: undefined,
                value: '',
                items: [],
                activeItems: [],
                selectedItems: [],
                buttons: [],
                busy: false,
                enabled: true,
                canSelectMany: false,
                ignoreFocusOut: false,
                step: undefined,
                totalSteps: undefined,
                onDidAccept,
                onDidHide,
                onDidChangeSelection,
                onDidChangeActive,
                onDidChangeValue,
                onDidTriggerButton,
                onDidTriggerItemButton,
                show() {
                    shown = true;
                    request('window/showQuickPick', shape())
                        .then(index => {
                            shown = false;
                            if (index >= 0 && index < items.length) {
                                const chosen = [items[index]];
                                pick.selectedItems = chosen;
                                pick.activeItems = chosen;
                                onDidChangeActive.fire(chosen);
                                onDidChangeSelection.fire(chosen);
                                onDidAccept.fire();
                            }
                            // Last, and not in the same turn: an extension
                            // accepts asynchronously - it validates what was
                            // picked - and treats being hidden as a
                            // cancellation. Firing both at once loses the
                            // answer to the cancellation every time.
                            hideLater(onDidHide);
                        });
                },
                // Taking it back off the screen, which an extension does with a
                // list it only put up to say "loading". Without telling Qt
                // Creator, that one stays up for good.
                hide() {
                    if (shown)
                        notify('quickPick/hide', {pickId});
                    shown = false;
                    onDidHide.fire();
                },
                dispose() {
                    if (shown)
                        notify('quickPick/hide', {pickId});
                    shown = false;
                    onDidAccept.clear();
                    onDidHide.clear();
                    onDidChangeSelection.clear();
                    onDidChangeActive.clear();
                    onDidChangeValue.clear();
                    onDidTriggerButton.clear();
                    onDidTriggerItemButton.clear();
                },
            };
            // An extension shows the list before it has one - "Loading..." -
            // and fills it in when the answer arrives. What is on screen has to
            // follow, or it waits forever on an empty list.
            let items = [];
            const shape = () => ({
                pickId,
                items: items.map(item => (typeof item === 'string' ? item : (item.label || ''))),
                shown: items.map(item => (typeof item === 'string'
                    ? {label: item, description: '', detail: ''}
                    : {label: item.label || '', description: item.description || '',
                       detail: item.detail || ''})),
                placeholder: pick.placeholder || pick.title || '',
                busy: !!pick.busy,
            });
            const changed = () => {
                if (shown)
                    notify('quickPick/update', shape());
            };
            Object.defineProperty(pick, 'items', {
                get: () => items,
                set: value => { items = value || []; changed(); },
            });
            for (const name of ['placeholder', 'title', 'busy']) {
                let held = pick[name];
                Object.defineProperty(pick, name, {
                    get: () => held,
                    set: value => { held = value; changed(); },
                });
            }
            return pick;
        },
        createInputBox() {
            const onDidAccept = eventEmitter();
            const onDidHide = eventEmitter();
            const onDidChangeValue = eventEmitter();
            const onDidTriggerButton = eventEmitter();
            const box = {
                title: undefined,
                prompt: undefined,
                placeholder: undefined,
                value: '',
                password: false,
                buttons: [],
                busy: false,
                enabled: true,
                ignoreFocusOut: false,
                validationMessage: undefined,
                step: undefined,
                totalSteps: undefined,
                onDidAccept,
                onDidHide,
                onDidChangeValue,
                onDidTriggerButton,
                show() {
                    request('window/showInputBox',
                            {prompt: box.prompt || box.title || '',
                             value: box.value || '',
                             placeholder: box.placeholder || ''})
                        .then(value => {
                            if (value !== null && value !== undefined) {
                                box.value = value;
                                onDidChangeValue.fire(value);
                                onDidAccept.fire();
                            }
                            hideLater(onDidHide);
                        });
                },
                hide() { onDidHide.fire(); },
                dispose() {
                    onDidAccept.clear();
                    onDidHide.clear();
                    onDidChangeValue.clear();
                },
            };
            return box;
        },
        // A terminal the extension knows how to set up - the environment a
        // build needs, the shell it wants - offered by name rather than left to
        // the user to recreate by hand.
        registerTerminalProfileProvider(profileId, provider) {
            terminalProfileProviders.set(profileId, provider);
            announceTerminalProfiles();
            return disposable(() => {
                terminalProfileProviders.delete(profileId);
                announceTerminalProfiles();
            });
        },
        registerTreeDataProvider(viewId, provider) {
            return registerTreeProvider(viewId, provider);
        },
        registerFileDecorationProvider: () => disposable(() => {}),
        createWebviewPanel(viewType, title, showOptions, options) {
            const id = 'webview-' + (nextWebviewId++);
            const onDidReceiveMessage = eventEmitter();
            const onDidDispose = eventEmitter();
            let html = '';
            const webview = {
                options: options || {},
                // Must match what asWebviewUri() below hands out, which is the
                // file: URI unchanged. Extensions build their
                // Content-Security-Policy from cspSource, so claiming a scheme
                // we do not actually serve makes them block their own
                // stylesheets and scripts. litehtml ignores CSP and so never
                // showed this; QtWebEngine enforces it.
                cspSource: 'file:',
                onDidReceiveMessage,
                get html() { return html; },
                set html(value) { html = value; notify('webview/setHtml', {id, html: value}); },
                postMessage(message) {
                    notify('webview/postMessage', {id, message});
                    return Promise.resolve(true);
                },
                asWebviewUri(uri) { return uri; },
            };
            const panel = {
                viewType, title, webview,
                active: true, visible: true,
                viewColumn: typeof showOptions === 'number' ? showOptions : 1,
                onDidDispose,
                onDidChangeViewState: eventEmitter(),
                reveal() { notify('webview/reveal', {id}); },
                dispose() {
                    notify('webview/dispose', {id});
                    onDidDispose.fire();
                    webviews.delete(id);
                },
            };
            webviews.set(id, {onMessage: onDidReceiveMessage, onDispose: onDidDispose});
            notify('webview/create', {id, viewType, title, options: options || {}});
            return panel;
        },
        createTreeView(viewId, options) {
            const registration = registerTreeProvider(viewId, (options || {}).treeDataProvider);
            const onDidChangeSelection = eventEmitter();
            const view = {
                // The view goes to the sidebar as it is registered, so an
                // extension asking whether it is there gets the true answer.
                visible: true,
                selection: [],
                title: viewNameOf(viewId),
                description: undefined,
                message: undefined,
                badge: undefined,
                onDidChangeSelection,
                onDidChangeVisibility: eventEmitter(),
                onDidExpandElement: eventEmitter(),
                onDidCollapseElement: eventEmitter(),
                reveal() {
                    // Naming a row from outside needs an identity that survives
                    // the trip, and the ids the tree hands out are per request.
                    reportMissing('TreeView.reveal');
                    return Promise.resolve();
                },
                dispose() { registration.dispose(); },
            };
            const entry = treeDataProviders.get(viewId);
            if (entry)
                entry.view = view;
            return view;
        },
        createOutputChannel(name) {
            const line = value => notify('output/append', {channel: name, value, line: true});
            return {
                name,
                append: value => notify('output/append', {channel: name, value, line: false}),
                appendLine: line,
                // LogOutputChannel methods.
                trace: (...args) => line('[trace] ' + args.join(' ')),
                debug: (...args) => line('[debug] ' + args.join(' ')),
                info: (...args) => line('[info] ' + args.join(' ')),
                warn: (...args) => line('[warn] ' + args.join(' ')),
                error: (...args) => line('[error] ' + args.join(' ')),
                // Info, as the editor's own default is: an extension asks
                // before it writes trace and debug lines, and Trace here would
                // be an invitation to fill the pane with them.
                logLevel: 3,
                onDidChangeLogLevel: eventEmitter(),
                clear: () => notify('output/clear', {channel: name}),
                // show(true) means "let me be seen, but do not take over".
                show: preserveFocus => notify('output/show',
                                              {channel: name,
                                               preserveFocus: preserveFocus === true}),
                hide() {},
                replace: value => {
                    notify('output/clear', {channel: name});
                    line(value);
                },
                dispose() {},
            };
        },
        setStatusBarMessage(text, hideAfterTimeout) {
            const message = typeof text === 'string' ? text : '';
            notify('statusbar/setMessage', {text: message});
            if (typeof hideAfterTimeout === 'number')
                setTimeout(() => notify('statusbar/setMessage', {text: ''}), hideAfterTimeout);
            return disposable(() => notify('statusbar/setMessage', {text: ''}));
        },
        createStatusBarItem(alignmentOrOptions, priority) {
            const id = 'statusbar-' + (nextStatusBarItemId++);
            let alignment = 1; // Left
            if (typeof alignmentOrOptions === 'number')
                alignment = alignmentOrOptions;
            else if (alignmentOrOptions && alignmentOrOptions.alignment)
                alignment = alignmentOrOptions.alignment;
            const sync = item => notify('statusbar/update', {
                id,
                text: item._text,
                tooltip: textOf(item._tooltip) || '',
                // What clicking it does. A command may be a string or a
                // {command, arguments} object, as the API allows.
                command: typeof item._command === 'string'
                    ? item._command : ((item._command || {}).command || ''),
                alignment,
                // An item saying something is wrong says so in colour.
                color: textOf(item._color) || (item._color || {}).id || '',
                backgroundColor: (item._backgroundColor || {}).id || '',
                visible: item._visible,
            });
            const item = {
                id, alignment, priority: priority || 0,
                _text: '', _tooltip: '', _command: undefined, _visible: false,
                _color: undefined, _backgroundColor: undefined,
                name: '',
                get color() { return this._color; },
                set color(v) { this._color = v; if (this._visible) sync(this); },
                get backgroundColor() { return this._backgroundColor; },
                set backgroundColor(v) {
                    this._backgroundColor = v;
                    if (this._visible)
                        sync(this);
                },
                get text() { return this._text; },
                set text(v) { this._text = v; if (this._visible) sync(this); },
                get tooltip() { return this._tooltip; },
                set tooltip(v) { this._tooltip = v; if (this._visible) sync(this); },
                get command() { return this._command; },
                set command(v) { this._command = v; if (this._visible) sync(this); },
                show() { this._visible = true; sync(this); },
                hide() { this._visible = false; sync(this); },
                dispose() { this._visible = false; notify('statusbar/remove', {id}); },
            };
            return item;
        },
    },
    workspace: {
        workspaceFolders: undefined,
        textDocuments: [],
        onDidOpenTextDocument,
        onDidChangeTextDocument,
        onDidCloseTextDocument,
        getConfiguration(section) {
            const fullKey = key => (section ? section + '.' + key : key);
            const override = full =>
                Object.prototype.hasOwnProperty.call(configuration, full)
                    ? configuration[full] : undefined;
            const lookup = key => {
                const full = fullKey(key);
                const value = override(full);
                return value === undefined ? configurationDefaults.get(full) : value;
            };
            // A section is a value too: getConfiguration('a').get('b') answers
            // with { c: ... } when what is declared is "a.b.c". Without this,
            // reading a group of settings at once finds nothing, while the
            // plain property for the same name has it.
            const group = key => {
                const under = fullKey(key) + '.';
                let tree;
                for (const full of new Set([...configurationDefaults.keys(),
                                            ...Object.keys(configuration)])) {
                    if (!full.startsWith(under))
                        continue;
                    tree = tree || {};
                    let node = tree;
                    const parts = full.slice(under.length).split('.');
                    for (const part of parts.slice(0, -1))
                        node = node[part] = node[part] || {};
                    const value = override(full);
                    node[parts[parts.length - 1]]
                        = value === undefined ? configurationDefaults.get(full) : value;
                }
                return tree;
            };
            const valueOf = key => {
                const value = lookup(key);
                return value === undefined ? group(key) : value;
            };
            const config = {
                get: (key, defaultValue) => {
                    const value = valueOf(key);
                    return value === undefined ? defaultValue : value;
                },
                has: key => valueOf(key) !== undefined,
                // Kept by Qt Creator, which sends the configuration back as it
                // does after any other change. An extension writes here to
                // remember a choice, so dropping it means being asked again on
                // every start.
                // Setting a value to nothing puts the default back, which is
                // how an extension undoes a setting it once wrote.
                update: (key, value) => request('configuration/write',
                                                {key: fullKey(key), value,
                                                 remove: value === undefined}).then(() => {
                    // Readable at once. Qt Creator sends the whole
                    // configuration back too, which agrees with this.
                    if (value === undefined)
                        delete configuration[fullKey(key)];
                    else
                        configuration[fullKey(key)] = value;
                    onDidChangeConfiguration.fire(configurationChangeEvent([fullKey(key)]));
                }),
                inspect: key => ({
                    key: fullKey(key),
                    defaultValue: configurationDefaults.get(fullKey(key)),
                    globalValue: override(fullKey(key)),
                }),
            };

            // The settings of a section are also exposed as plain
            // properties, so `getConfiguration('cmake').options.advanced`
            // works alongside get('options.advanced'). Build that tree from
            // every key we know about, without shadowing the methods above.
            const prefix = section ? section + '.' : '';
            for (const full of new Set([...configurationDefaults.keys(),
                                        ...Object.keys(configuration)])) {
                if (!full.startsWith(prefix))
                    continue;
                const parts = full.slice(prefix.length).split('.');
                let node = config;
                for (const part of parts.slice(0, -1)) {
                    if (typeof node[part] !== 'object' || node[part] === null)
                        node[part] = {};
                    node = node[part];
                }
                const leaf = parts[parts.length - 1];
                if (!(leaf in node))
                    node[leaf] = lookup(full.slice(prefix.length));
            }
            return config;
        },
        onDidChangeConfiguration,
    },
    languages: {
        getLanguages() {
            return request('languages/known', {})
                .then(result => (result && result.languageIds) || []);
        },
        // A document whose language the editor guessed wrongly, or could not
        // guess at all. Everything keyed by language id - which providers run,
        // which features attach - follows the new answer.
        setTextDocumentLanguage(document, languageId) {
            const uri = document && document.uri;
            if (!uri)
                return Promise.resolve(document);
            const known = documents.get(uriToString(uri));
            const target = known || document;
            target.languageId = languageId;
            notify('document/setLanguage', {uri: uriToString(uri), languageId});
            onDidCloseTextDocument.fire(target);
            onDidOpenTextDocument.fire(target);
            return Promise.resolve(target);
        },
        createDiagnosticCollection(name) {
            name = name || 'default';
            const store = new Map(); // uri string -> Diagnostic[]
            diagnosticStores.add(store);
            const publish = key => {
                notify('diagnostics/publish', {
                    collection: name,
                    uri: key,
                    diagnostics: (store.get(key) || []).map(serializeDiagnostic),
                });
                onDidChangeDiagnostics.fire({uris: [vscode.Uri.file(key)]});
            };
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
                entries() {
                    return [...store.entries()].map(
                        ([key, diagnostics]) => [vscode.Uri.file(key), diagnostics]);
                },
                get(uri) { return store.get(uriToString(uri)); },
                has(uri) { return store.has(uriToString(uri)); },
                forEach(callback) {
                    for (const [key, diags] of store)
                        callback(vscode.Uri.file(key), diags, collection);
                },
                dispose() {
                    collection.clear();
                    diagnosticStores.delete(store);
                },
            };
            return collection;
        },
        // What every extension has reported, which is what a diagnostic is
        // for: one asks before launching whether the project still has
        // errors, and takes an empty answer for a clean build.
        getDiagnostics(uri) {
            if (uri !== undefined) {
                const key = uriToString(uri);
                const found = [];
                for (const store of diagnosticStores)
                    found.push(...(store.get(key) || []));
                return found;
            }
            const byUri = new Map();
            for (const store of diagnosticStores) {
                for (const [key, diagnostics] of store) {
                    const found = byUri.get(key) || [];
                    found.push(...diagnostics);
                    byUri.set(key, found);
                }
            }
            return [...byUri].map(([key, found]) => [vscode.Uri.file(key), found]);
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
        // Remaining feature providers are accepted but not routed yet, and
        // say so: a provider that is taken and dropped looks like one that
        // works until the feature is missing in the editor.
        registerCodeActionsProvider(selector, provider) {
            const entry = {selector, provider};
            codeActionProviders.push(entry);
            notify('codeAction/registerProvider',
                   {languageIds: selectorLanguageIds(selector)});
            return disposable(() => {
                const index = codeActionProviders.indexOf(entry);
                if (index >= 0)
                    codeActionProviders.splice(index, 1);
            });
        },
        registerDocumentSymbolProvider(selector, provider) {
            const entry = {selector, provider};
            symbolProviders.push(entry);
            notify('symbols/registerProvider', {languageIds: selectorLanguageIds(selector)});
            return disposable(() => {
                const index = symbolProviders.indexOf(entry);
                if (index >= 0)
                    symbolProviders.splice(index, 1);
            });
        },
        registerDocumentFormattingEditProvider(selector, provider) {
            const entry = {selector, provider};
            formattingProviders.push(entry);
            notify('formatting/registerProvider',
                   {languageIds: selectorLanguageIds(selector)});
            return disposable(() => {
                const index = formattingProviders.indexOf(entry);
                if (index >= 0)
                    formattingProviders.splice(index, 1);
            });
        },
        registerDocumentRangeFormattingEditProvider(selector, provider) {
            const entry = {selector, provider};
            rangeFormattingProviders.push(entry);
            notify('rangeFormatting/registerProvider',
                   {languageIds: selectorLanguageIds(selector)});
            return disposable(() => {
                const index = rangeFormattingProviders.indexOf(entry);
                if (index >= 0)
                    rangeFormattingProviders.splice(index, 1);
            });
        },
        registerOnTypeFormattingEditProvider(selector, provider, firstTrigger, ...moreTriggers) {
            const entry = {selector, provider,
                           triggers: [firstTrigger, ...moreTriggers]
                               .filter(c => typeof c === 'string' && c.length)};
            onTypeProviders.push(entry);
            announceOnTypeFormatting();
            return disposable(() => {
                const index = onTypeProviders.indexOf(entry);
                if (index >= 0)
                    onTypeProviders.splice(index, 1);
                announceOnTypeFormatting();
            });
        },
        registerReferenceProvider(selector, provider) {
            const entry = {selector, provider};
            referenceProviders.push(entry);
            notify('references/registerProvider', {languageIds: selectorLanguageIds(selector)});
            return disposable(() => {
                const index = referenceProviders.indexOf(entry);
                if (index >= 0)
                    referenceProviders.splice(index, 1);
            });
        },
        registerRenameProvider(selector, provider) {
            const entry = {selector, provider};
            renameProviders.push(entry);
            notify('rename/registerProvider', {languageIds: selectorLanguageIds(selector)});
            return disposable(() => {
                const index = renameProviders.indexOf(entry);
                if (index >= 0)
                    renameProviders.splice(index, 1);
            });
        },
        registerSignatureHelpProvider(selector, provider, ...triggerCharacters) {
            const entry = {selector, provider};
            signatureProviders.push(entry);
            notify('signature/registerProvider',
                   {languageIds: selectorLanguageIds(selector),
                    triggerCharacters: triggerCharacters.flat()});
            return disposable(() => {
                const index = signatureProviders.indexOf(entry);
                if (index >= 0)
                    signatureProviders.splice(index, 1);
            });
        },
        registerColorProvider(selector, provider) {
            const entry = {selector, provider};
            colorProviders.push(entry);
            announceLanguages('color/registerProvider', colorProviders);
            return disposable(() => {
                const index = colorProviders.indexOf(entry);
                if (index >= 0)
                    colorProviders.splice(index, 1);
                announceLanguages('color/registerProvider', colorProviders);
            });
        },
        registerCodeLensProvider(selector, provider) {
            const entry = {selector, provider};
            codeLensProviders.push(entry);
            notify('codeLens/registerProvider', {languageIds: selectorLanguageIds(selector)});
            if (provider.onDidChangeCodeLenses) {
                provider.onDidChangeCodeLenses(() => notify('codeLens/changed', {}));
            }
            return disposable(() => {
                const index = codeLensProviders.indexOf(entry);
                if (index >= 0)
                    codeLensProviders.splice(index, 1);
            });
        },
        registerInlayHintsProvider: stub('languages.registerInlayHintsProvider', () => disposable(() => {})),
        registerDocumentLinkProvider(selector, provider) {
            const entry = {selector, provider};
            linkProviders.push(entry);
            notify('links/registerProvider', {languageIds: selectorLanguageIds(selector)});
            return disposable(() => {
                const index = linkProviders.indexOf(entry);
                if (index >= 0)
                    linkProviders.splice(index, 1);
            });
        },
        registerFoldingRangeProvider(selector, provider) {
            const entry = {selector, provider};
            foldingProviders.push(entry);
            announceLanguages('folding/registerProvider', foldingProviders);
            return disposable(() => {
                const index = foldingProviders.indexOf(entry);
                if (index >= 0)
                    foldingProviders.splice(index, 1);
                announceLanguages('folding/registerProvider', foldingProviders);
            });
        },
        registerSelectionRangeProvider: stub('languages.registerSelectionRangeProvider', () => disposable(() => {})),
        registerCallHierarchyProvider: stub('languages.registerCallHierarchyProvider', () => disposable(() => {})),
        registerTypeHierarchyProvider: stub('languages.registerTypeHierarchyProvider', () => disposable(() => {})),
        registerImplementationProvider: stub('languages.registerImplementationProvider', () => disposable(() => {})),
        registerTypeDefinitionProvider(selector, provider) {
            const entry = {selector, provider};
            typeDefinitionProviders.push(entry);
            notify('typeDefinition/registerProvider',
                   {languageIds: selectorLanguageIds(selector)});
            return disposable(() => {
                const index = typeDefinitionProviders.indexOf(entry);
                if (index >= 0)
                    typeDefinitionProviders.splice(index, 1);
            });
        },
        registerDeclarationProvider: stub('languages.registerDeclarationProvider', () => disposable(() => {})),
        registerDocumentHighlightProvider(selector, provider) {
            const entry = {selector, provider};
            highlightProviders.push(entry);
            notify('highlights/registerProvider', {languageIds: selectorLanguageIds(selector)});
            return disposable(() => {
                const index = highlightProviders.indexOf(entry);
                if (index >= 0)
                    highlightProviders.splice(index, 1);
            });
        },
        registerWorkspaceSymbolProvider(provider) {
            const entry = {provider};
            workspaceSymbolProviders.push(entry);
            notify('workspaceSymbols/registerProvider', {});
            return disposable(() => {
                const index = workspaceSymbolProviders.indexOf(entry);
                if (index >= 0)
                    workspaceSymbolProviders.splice(index, 1);
            });
        },
        registerLinkedEditingRangeProvider: stub('languages.registerLinkedEditingRangeProvider', () => disposable(() => {})),
        registerDocumentSemanticTokensProvider(selector, provider, legend) {
            const entry = {selector, provider,
                           types: (legend && legend.tokenTypes) || [],
                           names: (legend && legend.tokenModifiers) || []};
            semanticProviders.push(entry);
            announceLanguages('semanticTokens/registerProvider', semanticProviders);
            return disposable(() => {
                const index = semanticProviders.indexOf(entry);
                if (index >= 0)
                    semanticProviders.splice(index, 1);
                announceLanguages('semanticTokens/registerProvider', semanticProviders);
            });
        },
        registerDocumentRangeSemanticTokensProvider(selector, provider, legend) {
            const entry = {selector, provider,
                           types: (legend && legend.tokenTypes) || [],
                           names: (legend && legend.tokenModifiers) || []};
            semanticProviders.push(entry);
            announceLanguages('semanticTokens/registerProvider', semanticProviders);
            return disposable(() => {
                const index = semanticProviders.indexOf(entry);
                if (index >= 0)
                    semanticProviders.splice(index, 1);
                announceLanguages('semanticTokens/registerProvider', semanticProviders);
            });
        },
        registerEvaluatableExpressionProvider: stub('languages.registerEvaluatableExpressionProvider', () => disposable(() => {})),
        registerInlineValuesProvider: stub('languages.registerInlineValuesProvider', () => disposable(() => {})),
        registerInlineCompletionItemProvider(selector, provider) {
            const entry = {selector, provider};
            inlineCompletionProviders.push(entry);
            announceLanguages('inlineCompletion/registerProvider', inlineCompletionProviders);
            return disposable(() => {
                const index = inlineCompletionProviders.indexOf(entry);
                if (index >= 0)
                    inlineCompletionProviders.splice(index, 1);
                announceLanguages('inlineCompletion/registerProvider', inlineCompletionProviders);
            });
        },
        registerDocumentPasteEditProvider: stub('languages.registerDocumentPasteEditProvider', () => disposable(() => {})),
        createLanguageStatusItem: () => ({dispose() {}}),
        setLanguageConfiguration: () => disposable(() => {}),
        onDidChangeDiagnostics,
        match: () => 10,
    },
    Uri,
    Position,
    Range,
    Location,
    Diagnostic,
    CompletionItem,
    SnippetString,
    MarkdownString,
    Hover,
    FileDecoration,
    EventEmitter,
    DocumentHighlight,
    FileSystemError,
    TreeItem,
    CodeLens,
    CodeAction,
    DocumentLink,
    InlayHint,
    SymbolInformation,
    DocumentSymbol,
    CallHierarchyItem,
    TypeHierarchyItem,
    CancellationError,
    CancellationTokenSource,
    TelemetryTrustedValue,
    Selection,
    TextEdit,
    WorkspaceEdit,
    SelectionRange,
    CompletionList,
    InlineCompletionItem,
    InlineCompletionList,
    QuickInputButtons,
    // The constants an extension reads off the module. Reading one that is not
    // there is not a missing feature to it - it is a TypeError, and the
    // extension is gone.
    ShellQuoting: {Escape: 1, Strong: 2, Weak: 3},
    TaskRevealKind: {Always: 1, Silent: 2, Never: 3},
    TaskPanelKind: {Shared: 1, Dedicated: 2, New: 3},
    CompletionItemTag: {Deprecated: 1},
    TextEditorRevealType: {Default: 0, InCenter: 1, InCenterIfOutsideViewport: 2, AtTop: 3},
    TextEditorSelectionChangeKind: {Keyboard: 1, Mouse: 2, Command: 3},
    TextEditorLineNumbersStyle: {Off: 0, On: 1, Relative: 2, Interval: 3},
    TextEditorCursorStyle: {Line: 1, Block: 2, Underline: 3, LineThin: 4,
                            BlockOutline: 5, UnderlineThin: 6},
    ExtensionKind: {UI: 1, Workspace: 2},
    QuickPickItemKind: {Separator: -1, Default: 0},
    DebugConsoleMode: {Separate: 0, MergeWithParent: 1},
    DocumentHighlightKind: {Text: 0, Read: 1, Write: 2},
    CommentMode: {Editing: 0, Preview: 1},
    CommentThreadCollapsibleState: {Collapsed: 0, Expanded: 1},
    NotebookCellKind: {Markup: 1, Code: 2},
    TextDocumentSaveReason: {Manual: 1, AfterDelay: 2, FocusOut: 3},
    TextDocumentChangeReason: {Undo: 1, Redo: 2},
    FileChangeType: {Changed: 1, Created: 2, Deleted: 3},
    TabInputText,
    TabInputTextDiff,
    Color,
    ColorInformation,
    ColorPresentation,
    FoldingRange,
    SignatureHelp,
    SignatureInformation,
    ParameterInformation,
    DiagnosticRelatedInformation,
    TerminalProfile,
    ThemeIcon,
    ThemeColor,
    RelativePattern,
    Task,
    CustomExecution,
    ShellExecution,
    ProcessExecution,
    TestMessage,
    TestRunRequest,
    TestTag,
    TestCoverageCount,
    FileCoverage,
    StatementCoverage,
    BranchCoverage,
    DeclarationCoverage,
    DebugAdapterNamedPipeServer,
    DebugAdapterExecutable,
    DebugAdapterServer,
    DebugAdapterInlineImplementation,
    SemanticTokensLegend,
    SemanticTokens,
    SemanticTokensBuilder,
    TaskGroup: {
        Clean: {id: 'clean'},
        Build: {id: 'build'},
        Rebuild: {id: 'rebuild'},
        Test: {id: 'test'},
    },
    TaskScope: {Global: 1, Workspace: 2},
    IndentAction: {None: 0, Indent: 1, IndentOutdent: 2, Outdent: 3},
    ExtensionMode: {Production: 1, Development: 2, Test: 3},
    LogLevel: {Off: 0, Trace: 1, Debug: 2, Info: 3, Warning: 4, Error: 5},
    // Proposed API, but remote-ssh reads it at construction time.
    CandidatePortSource: {None: 0, Process: 1, Output: 2, Hybrid: 3},
    TestRunProfileKind: {Run: 1, Debug: 2, Coverage: 3},
    DebugConfigurationProviderTriggerKind: {Initial: 1, Dynamic: 2},
    TreeItemCollapsibleState: {None: 0, Collapsed: 1, Expanded: 2},
    CodeActionKind: {
        Empty: makeCodeActionKind(''),
        QuickFix: makeCodeActionKind('quickfix'),
        Refactor: makeCodeActionKind('refactor'),
        RefactorExtract: makeCodeActionKind('refactor.extract'),
        RefactorInline: makeCodeActionKind('refactor.inline'),
        RefactorRewrite: makeCodeActionKind('refactor.rewrite'),
        Source: makeCodeActionKind('source'),
        SourceOrganizeImports: makeCodeActionKind('source.organizeImports'),
        SourceFixAll: makeCodeActionKind('source.fixAll'),
    },
    CodeActionTriggerKind: {Invoke: 1, Automatic: 2},
    SymbolKind: {
        File: 0, Module: 1, Namespace: 2, Package: 3, Class: 4, Method: 5, Property: 6,
        Field: 7, Constructor: 8, Enum: 9, Interface: 10, Function: 11, Variable: 12,
        Constant: 13, String: 14, Number: 15, Boolean: 16, Array: 17, Object: 18, Key: 19,
        Null: 20, EnumMember: 21, Struct: 22, Event: 23, Operator: 24, TypeParameter: 25,
    },
    SymbolTag: {Deprecated: 1},
    InlayHintKind: {Type: 1, Parameter: 2},
    FoldingRangeKind: {Comment: 1, Imports: 2, Region: 3},
    SignatureHelpTriggerKind: {Invoke: 1, TriggerCharacter: 2, ContentChange: 3},
    ConfigurationTarget: {Global: 1, Workspace: 2, WorkspaceFolder: 3},
    ProgressLocation: {SourceControl: 1, Window: 10, Notification: 15},
    FileType: {Unknown: 0, File: 1, Directory: 2, SymbolicLink: 64},
    UIKind: {Desktop: 1, Web: 2},
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
    StatusBarAlignment: {Left: 1, Right: 2},
    OverviewRulerLane: {Left: 1, Center: 2, Right: 4, Full: 7},
    InputBoxValidationSeverity: {Info: 1, Warning: 2, Error: 3},
    InlineCompletionTriggerKind: {Invoke: 0, Automatic: 1},
    LanguageModelChatMessageRole: {User: 1, Assistant: 2},
    ViewColumn: {Active: -1, Beside: -2, One: 1, Two: 2, Three: 3},
    Disposable: DisposableClass,
};

// Broaden the namespaces that real extensions touch at load/activate time.
// These are stubs (no-ops, empty results, live events) so activation succeeds;
// behaviour is filled in slice by slice.
// A member of the API we do not implement yet. It must not throw - extensions
// register providers defensively while activating, and one throw would take
// the whole extension down - but it must not pass for the real thing either.
// Each one is reported once, so what an extension is actually missing shows up
// instead of being silently absent.
const reportedStubs = new Set();
// Which part of the configuration changed. Extensions lean on this: one that
// restarts a language server when its own settings change must not do so when
// another extension's do.
function configurationChangeEvent(keys) {
    const changed = keys && new Set(keys);
    return {
        affectsConfiguration: section => {
            if (!changed || !section)
                return true;
            for (const key of changed) {
                if (key === section || key.startsWith(section + '.'))
                    return true;
            }
            return false;
        },
    };
}

function changedConfigurationKeys(before, after) {
    const keys = new Set([...Object.keys(before || {}), ...Object.keys(after || {})]);
    const changed = [];
    for (const key of keys) {
        if (JSON.stringify(before[key]) !== JSON.stringify(after[key]))
            changed.push(key);
    }
    return changed;
}

function reportMissing(name) {
    if (reportedStubs.has(name))
        return;
    reportedStubs.add(name);
    notify('api/unimplemented', {member: name});
    logToStderr('not implemented:', name);
}

function stub(name, result) {
    return (...args) => {
        reportMissing(name);
        return typeof result === 'function' ? result(...args) : result;
    };
}

const noopDisposable = () => disposable(() => {});

// Being hidden means "the user gave up", so it has to come after whatever the
// extension does about the answer - which it does with an await. A timer runs
// after the promises the accept handler is waiting on.
function hideLater(onDidHide) {
    setTimeout(() => onDidHide.fire(), 0);
}
let nextDecorationKey = 1;

Object.assign(vscode.commands, {
    registerTextEditorCommand(command, callback, thisArg) {
        return vscode.commands.registerCommand(command, callback, thisArg);
    },
    // Extensions ask this to find out whether another extension's command is
    // there before using it; answering "none" makes every such check fail.
    // Everything that can be asked for, which is more than what is running:
    // a command an extension contributes starts that extension when it is
    // asked for, so leaving it out here says it cannot be had.
    getCommands: (filterInternal) => {
        const ids = new Set([...commandHandlers.keys(), ...Object.keys(builtinCommands)]);
        for (const entry of knownExtensions.values()) {
            const contributes = (entry.packageJSON || {}).contributes || {};
            for (const command of contributes.commands || []) {
                if (command && command.command)
                    ids.add(command.command);
            }
        }
        const all = [...ids];
        return Promise.resolve(filterInternal ? all.filter(id => !id.startsWith('_')) : all);
    },
});

Object.assign(vscode.workspace, {
    name: undefined,
    rootPath: undefined,
    isTrusted: true,
    onDidChangeWorkspaceFolders: eventEmitter(),
    onDidSaveTextDocument,
    onWillSaveTextDocument,
    onDidCreateFiles: eventEmitter(),
    onDidDeleteFiles: eventEmitter(),
    onDidRenameFiles: eventEmitter(),
    onWillCreateFiles: eventEmitter(),
    onWillDeleteFiles: eventEmitter(),
    onWillRenameFiles: eventEmitter(),
    onDidGrantWorkspaceTrust: eventEmitter(),
    // No .code-workspace file exists here, and undefined is the answer then -
    // but it has to be an answer, not an unknown member.
    workspaceFile: undefined,
    getWorkspaceFolder: uri => {
        const p = uriToString(uri);
        const folders = vscode.workspace.workspaceFolders || [];
        // The longest folder that really contains it: "/src/app" is not in
        // "/src/apple", and a folder contains itself.
        let found;
        for (const f of folders) {
            const base = f.uri.fsPath;
            if (p !== base && !p.startsWith(base.endsWith('/') ? base : base + '/'))
                continue;
            if (!found || base.length > found.uri.fsPath.length)
                found = f;
        }
        return found;
    },
    asRelativePath: (p, includeFolder) => {
        const full = uriToString(p);
        const folders = vscode.workspace.workspaceFolders || [];
        for (const f of folders) {
            if (!full.startsWith(f.uri.fsPath + '/'))
                continue;
            const relative = full.slice(f.uri.fsPath.length + 1);
            // Which folder it was in matters once there is more than one, and
            // the caller can ask for it either way.
            const named = includeFolder === undefined ? folders.length > 1 : !!includeFolder;
            return named ? f.name + '/' + relative : relative;
        }
        return full;
    },
    // Qt Creator does the searching: it owns the workspace, and its FilePath
    // handles a remote one the same way. A GlobPattern is either a plain
    // string, searched under every workspace folder, or a RelativePattern
    // carrying its own base.
    findFiles: (include, exclude, maxResults) => {
        const patternOf = glob => (glob && typeof glob === 'object' ? glob.pattern : glob) || '';
        const baseOf = glob => (glob && typeof glob === 'object'
            ? (glob.base || (glob.baseUri && glob.baseUri.fsPath)) : undefined);
        return request('workspace/findFiles', {
            include: patternOf(include),
            exclude: patternOf(exclude),
            base: baseOf(include),
            maxResults: maxResults || 0,
        }).then(paths => (paths || []).map(p => vscode.Uri.file(p)));
    },
    saveAll: () => request('workspace/saveAll', {}),
    applyEdit: edit => {
        const all = edit && typeof edit.operations === 'function' ? edit.operations() : [];
        const changes = all.map(operation => {
            if (operation.kind !== 'edit') {
                return {kind: operation.kind, path: uriToString(operation.uri),
                        newPath: operation.newUri ? uriToString(operation.newUri) : undefined,
                        overwrite: !!(operation.options && operation.options.overwrite),
                        ignoreIfExists: !!(operation.options && operation.options.ignoreIfExists),
                        ignoreIfNotExists: !!(operation.options
                                              && operation.options.ignoreIfNotExists),
                        recursive: !!(operation.options && operation.options.recursive)};
            }
            return {kind: 'edit', path: uriToString(operation.uri),
                    edits: operation.edits.map(
                        e => ({range: rangeToJson(e.range), newText: e.newText || ''}))};
        });
        if (!changes.length)
            return Promise.resolve(true);
        return request('workspace/applyEdit', {changes});
    },
    // Loading a document, not showing it. An untitled one lives in the host
    // alone; anything else is read through Qt Creator, so an editor's unsaved
    // text wins over what is on disk, and a remote file works at all.
    //
    // A URI in a scheme of the extension's own is answered by the extension.
    // Only a Uri can be one: a plain string is a file name, which is what
    // makes a Windows drive letter unambiguous.
    openTextDocument: arg => {
        if (arg && arg.scheme && arg.scheme !== 'file') {
            const provider = contentProviders.get(arg.scheme);
            if (!provider) {
                return Promise.reject(
                    new Error('No content provider is registered for "' + arg.scheme + ':".'));
            }
            const known = documents.get(arg.toString());
            if (known)
                return Promise.resolve(known);
            return Promise.resolve(
                provider.provideTextDocumentContent(arg, new CancellationTokenSource().token))
                .then(text => {
                    const document = makeTextDocument({uri: arg.toString(),
                                                       languageId: 'plaintext',
                                                       version: 1, text: text || ''});
                    document.uri = arg;
                    document.fileName = arg.path;
                    documents.set(arg.toString(), document);
                    vscode.workspace.textDocuments.push(document);
                    onDidOpenTextDocument.fire(document);
                    return document;
                });
        }
        if (arg && typeof arg === 'object' && !arg.fsPath && !arg.path) {
            const document = makeTextDocument({uri: '', languageId: arg.language || 'plaintext',
                                               version: 1, text: arg.content || ''});
            document.isUntitled = true;
            return Promise.resolve(document);
        }
        const path = uriToString(arg);
        const known = documents.get(path);
        if (known)
            return Promise.resolve(known);
        return request('workspace/openTextDocument', {path}).then(result => {
            // Filed under the path Qt Creator resolved it to, not the one it
            // was asked for: the two differ for anything an extension builds
            // out of its own directory, and a document filed under the other
            // spelling never hears about the file again.
            const resolved = result.path || path;
            const already = documents.get(resolved);
            if (already)
                return already;
            const document = makeTextDocument({uri: resolved, languageId: result.languageId,
                                               version: 1, text: result.text});
            documents.set(resolved, document);
            vscode.workspace.textDocuments.push(document);
            onDidOpenTextDocument.fire(document);
            return document;
        });
    },
    registerTextDocumentContentProvider(scheme, provider) {
        contentProviders.set(scheme, provider);
        // A provider announcing new content is asking for what it produced to
        // be shown again; without this the document stays as it first was.
        const listening = provider.onDidChange
            ? provider.onDidChange(async uri => {
                  const key = uri.toString();
                  const known = documents.get(key);
                  if (!known)
                      return;
                  const text = await provider.provideTextDocumentContent(
                      uri, cancellationToken);
                  known._text = text === undefined || text === null ? '' : String(text);
                  known.version += 1;
                  notify('document/replaceContents', {uri: key, text: known._text});
                  onDidChangeTextDocument.fire({document: known, contentChanges: [],
                                                reason: undefined});
              })
            : undefined;
        return disposable(() => {
            contentProviders.delete(scheme);
            if (listening)
                listening.dispose();
        });
    },
    registerFileSystemProvider: stub('workspace.registerFileSystemProvider', noopDisposable),
    registerTaskProvider: stub('workspace.registerTaskProvider', noopDisposable),
    registerRemoteAuthorityResolver: stub('workspace.registerRemoteAuthorityResolver',
                                          noopDisposable),
    // A real watcher. Extensions wait on these for a config file to appear or
    // their output to change; three emitters that never fire look exactly like
    // a project where nothing ever happens.
    createFileSystemWatcher: (pattern, ignoreCreate, ignoreChange, ignoreDelete) => {
        const id = 'watcher-' + (nextWatcherId++);
        const watcher = {
            onDidCreate: eventEmitter(),
            onDidChange: eventEmitter(),
            onDidDelete: eventEmitter(),
            ignoreCreateEvents: !!ignoreCreate,
            ignoreChangeEvents: !!ignoreChange,
            ignoreDeleteEvents: !!ignoreDelete,
            dispose() {
                fileWatchers.delete(id);
                notify('watch/remove', {id});
            },
        };
        fileWatchers.set(id, watcher);
        notify('watch/add', {
            id,
            pattern: (pattern && typeof pattern === 'object' ? pattern.pattern : pattern) || '',
            base: (pattern && typeof pattern === 'object'
                       ? (pattern.base || (pattern.baseUri && pattern.baseUri.fsPath)) : undefined),
        });
        return watcher;
    },
    // The host is a local node process, so this is the real file system - which
    // is what extensions assume (redhat.java reads its own package.json here).
    fs: {
        readFile: uri => fileSystemCall(uri, () => nodeFs.promises.readFile(uriToString(uri))),
        writeFile: (uri, content) => fileSystemCall(
            uri, () => nodeFs.promises.writeFile(uriToString(uri), Buffer.from(content))),
        // lstat, and the symbolic link bit alongside what it points at, which
        // is how a FileType is read: File | SymbolicLink, not either.
        stat: uri => fileSystemCall(uri, () => nodeFs.promises.lstat(uriToString(uri))
            .then(async stat => {
                let type = stat.isDirectory() ? 2 : 1;
                if (stat.isSymbolicLink()) {
                    const target = await nodeFs.promises.stat(uriToString(uri)).catch(() => null);
                    type = (target && target.isDirectory() ? 2 : 1) | 64;
                }
                return {type, ctime: stat.ctimeMs, mtime: stat.mtimeMs, size: stat.size};
            })),
        readDirectory: uri => fileSystemCall(
            uri, () => nodeFs.promises.readdir(uriToString(uri), {withFileTypes: true})
                .then(entries => entries.map(entry => {
                    const type = entry.isDirectory() ? 2 : 1;
                    return [entry.name, entry.isSymbolicLink() ? type | 64 : type];
                }))),
        createDirectory: uri => fileSystemCall(
            uri, () => nodeFs.promises.mkdir(uriToString(uri), {recursive: true})),
        // Asking for the trash is asking for the file back later; removing it
        // outright is a different thing and not what was asked for.
        delete: (uri, options) => (options && options.useTrash)
            ? fileSystemCall(uri, () => request('workspace/moveToTrash',
                                                {path: uriToString(uri)}))
            : fileSystemCall(uri, () => nodeFs.promises.rm(
                  uriToString(uri), {recursive: !!(options && options.recursive), force: true})),
        rename: (from, to) => fileSystemCall(
            from, () => nodeFs.promises.rename(uriToString(from), uriToString(to))),
        copy: (from, to) => fileSystemCall(
            from, () => nodeFs.promises.copyFile(uriToString(from), uriToString(to))),
    },
});

Object.assign(vscode.window, {
    state: {focused: true, active: true},
    terminals: [],
    activeTerminal: undefined,
    visibleTextEditors: [],
    activeColorTheme: {kind: 2},
    onDidChangeVisibleTextEditors: eventEmitter(),
    // Qt Creator has one editor area, so there is one group, and its tabs are
    // the documents that are open in it. Extensions walk these to find what
    // the user has in front of them - and a missing tabGroups is not an
    // absent feature to them, it is a crash.
    tabGroups: {
        get all() { return [tabGroup()]; },
        get activeTabGroup() { return tabGroup(); },
        onDidChangeTabs,
        onDidChangeTabGroups,
        close: () => Promise.resolve(false),
    },
    onDidChangeTextEditorSelection,
    onDidChangeTextEditorVisibleRanges: eventEmitter(),
    onDidChangeTextEditorOptions: eventEmitter(),
    onDidWriteTerminalData: eventEmitter(),
    onDidChangeWindowState: eventEmitter(),
    onDidChangeActiveColorTheme: eventEmitter(),
    onDidOpenTerminal: eventEmitter(),
    onDidCloseTerminal: eventEmitter(),
    // Opens the editor in Qt Creator. An untitled document has no path to open,
    // so it stays in the host - the editor object is still handed back, since
    // extensions go on to read from it.
    showTextDocument: (documentOrUri, options) => {
        const document = documentOrUri && documentOrUri.getText
            ? Promise.resolve(documentOrUri)
            : vscode.workspace.openTextDocument(documentOrUri);
        return document.then(opened => {
            // Nothing on disk to open: hand Qt Creator the text itself.
            if (opened.isUntitled || (opened.uri.scheme && opened.uri.scheme !== 'file')) {
                return request('window/showDocumentContents',
                               {title: nodePath.basename(opened.uri.path || 'untitled'),
                                text: opened.getText(),
                                uri: opened.uri.toString()})
                    .then(() => makeTextEditor(opened));
            }
            const selection = options && options.selection
                ? {line: options.selection.start.line,
                   character: options.selection.start.character}
                : undefined;
            return request('window/showTextDocument',
                           {path: uriToString(opened.uri), selection,
                            // Opening something to look at later is not the
                            // same as opening it to work in.
                            preserveFocus: !!(options && options.preserveFocus)})
                .then(() => makeTextEditor(opened));
        });
    },
    showWorkspaceFolderPick: options => {
        const folders = vscode.workspace.workspaceFolders || [];
        if (folders.length < 2)
            return Promise.resolve(folders[0]);
        return request('window/showQuickPick',
                       {items: folders.map(f => f.name),
                        placeholder: (options && options.placeHolder) || ''})
            .then(index => (index === null || index === undefined ? undefined : folders[index]));
    },
    // A dialog was never shown and the extension read the answer as a cancel.
    showOpenDialog: options => {
        const o = options || {};
        return request('window/showOpenDialog', {
            title: o.title || (o.openLabel || ''),
            defaultPath: o.defaultUri ? uriToString(o.defaultUri) : '',
            filters: o.filters || {},
            canSelectMany: !!o.canSelectMany,
            canSelectFolders: !!o.canSelectFolders,
        }).then(paths => (paths ? paths.map(p => vscode.Uri.file(p)) : undefined));
    },
    showSaveDialog: options => {
        const o = options || {};
        return request('window/showSaveDialog', {
            title: o.title || (o.saveLabel || ''),
            defaultPath: o.defaultUri ? uriToString(o.defaultUri) : '',
            filters: o.filters || {},
        }).then(path => (path ? vscode.Uri.file(path) : undefined));
    },
    registerWebviewViewProvider: stub('window.registerWebviewViewProvider', noopDisposable),
    registerWebviewPanelSerializer: stub('window.registerWebviewPanelSerializer', noopDisposable),
    registerCustomEditorProvider: stub('window.registerCustomEditorProvider', noopDisposable),
    registerUriHandler: stub('window.registerUriHandler', noopDisposable),
    registerTerminalLinkProvider: stub('window.registerTerminalLinkProvider', noopDisposable),
    // A terminal an extension opens is a shell in Qt Creator's terminal, so
    // what it sends is really run. The object is handed back at once, as the
    // API promises, and what is sent before the shell is there waits for it.
    createTerminal: nameOrOptions => {
        const options = typeof nameOrOptions === 'string' ? {name: nameOrOptions}
                                                         : (nameOrOptions || {});
        // A terminal the extension itself drives: no process, the text is what
        // it writes and what is typed goes back to it. cortex-debug's GDB
        // server console is one, and it holds the port its adapter needs, so
        // the session cannot start until this terminal has opened.
        if (options.pty)
            return createExtensionTerminal(options);
        const terminal = {
            name: options.name || 'Terminal',
            processId: Promise.resolve(undefined),
            creationOptions: options,
            exitStatus: undefined,
            state: {isInteractedWith: false},
            shellIntegration: undefined,
            sendText(text, addNewLine) {
                const line = text + (addNewLine === false ? '' : '\n');
                pending.push(line);
                flush();
            },
            show() { notify('terminal/show', {}); },
            hide() {},
            dispose() {
                if (id !== undefined)
                    notify('terminal/dispose', {id});
                const index = vscode.window.terminals.indexOf(terminal);
                if (index >= 0)
                    vscode.window.terminals.splice(index, 1);
                vscode.window.onDidCloseTerminal.fire(terminal);
            },
        };

        let id;
        const pending = [];
        const flush = () => {
            if (id === undefined)
                return;
            while (pending.length)
                notify('terminal/send', {id, text: pending.shift()});
        };
        request('terminal/create', {name: terminal.name,
                                    cwd: options.cwd ? uriToString(options.cwd) : '',
                                    env: options.env || {},
                                    // The shell it asked for, rather than the
                                    // one the system happens to default to.
                                    shellPath: options.shellPath || '',
                                    shellArgs: typeof options.shellArgs === 'string'
                                        ? [options.shellArgs] : (options.shellArgs || []),
                                    shellWanted: !!options._shellWanted})
            .then(created => {
                id = created;
                terminal._id = created;
                flush();
            })
            .catch(error => reportMissing('window.createTerminal (' + error.message + ')'));

        vscode.window.terminals.push(terminal);
        vscode.window.onDidOpenTerminal.fire(terminal);
        return terminal;
    },
    // An extension marks up text with these - a review comment, an analysis
    // finding - and hands the ranges back through editor.setDecorations().
    createTextEditorDecorationType: (options) => {
        const key = 'alien-decoration-' + nextDecorationKey++;
        notify('decoration/register', {key, options: options || {}});
        return {
            key,
            dispose: () => notify('decoration/unregister', {key}),
        };
    },
    // Real progress: an extension wraps minute-long work in this (a review, a
    // fetch), and a silent stub leaves the IDE looking idle throughout.
    withProgress: async (options, task) => {
        const id = 'progress-' + (nextProgressId++);
        const cancellable = !!(options && options.cancellable);
        // A task that says it can be stopped is given something that can stop
        // it; the button Qt Creator shows is what pulls the trigger.
        const source = new CancellationTokenSource();
        if (cancellable)
            progressCancellations.set(id, source);
        // Where it belongs: a notification is a task with a bar, a window
        // progress is a line in the status bar and nothing more.
        notify('progress/start', {id, title: (options && options.title) || '', cancellable,
                                  location: (options && options.location) || 15});
        try {
            return await task(
                {report: value => notify('progress/report',
                                         {id,
                                          message: (value && value.message) || '',
                                          increment: (value && value.increment) || 0})},
                source.token);
        } finally {
            progressCancellations.delete(id);
            notify('progress/end', {id});
        }
    },
});

// Some extensions gate features on the reported VS Code version.
vscode.version = '1.96.0';

vscode.env = {
    appName: 'Qt Creator',
    appHost: 'desktop',
    appRoot: __dirname,
    uriScheme: 'vscode',
    language: 'en',
    machineId: 'alien',
    sessionId: 'alien',
    isNewAppInstall: false,
    isTelemetryEnabled: false,
    remoteName: undefined,
    // Undefined the way a local window reports it, so an extension that can
    // work over a remote connection knows it is not on one.
    remoteAuthority: undefined,
    shell: '/bin/sh',
    uiKind: 1,
    onDidChangeTelemetryEnabled: eventEmitter(),
    // The real clipboard: extensions offer "copy the URL/id" commands, and a
    // no-op here means the command appears to work and pastes nothing.
    clipboard: {
        readText: () => request('env/clipboardRead', {}).then(text => text || ''),
        writeText: text => request('env/clipboardWrite', {text: String(text)}).then(() => undefined),
    },
    openExternal: async uri => {
        // A real hand-off: extensions use this to show a page in the browser
        // (gerrit-ai opens a change from its dashboard this way).
        await request('window/openExternal',
                      {uri: typeof uri === 'string' ? uri : String(uri)});
        return true;
    },
    asExternalUri: uri => Promise.resolve(uri),
    createTelemetryLogger: () => ({
        logUsage() {}, logError() {}, dispose() {},
        onDidChangeEnableStates: eventEmitter(),
    }),
};

// Extensions call l10n.t() for every user-visible string, often hundreds of
// times, and at module scope. With no translation bundle the message itself is
// the result; only the placeholders have to be filled in.
vscode.l10n = {
    bundle: undefined,
    uri: undefined,
    t(...args) {
        let message = args[0];
        let values = args.slice(1);
        if (message && typeof message === 'object') {
            // t({message, args, comment})
            values = message.args || [];
            message = message.message;
        }
        const named = values.length === 1 && values[0] && typeof values[0] === 'object'
            && !Array.isArray(values[0]);
        if (named) {
            const record = values[0];
            return String(message).replace(/\{(\w+)\}/g,
                (all, key) => (key in record ? String(record[key]) : all));
        }
        return String(message).replace(/\{(\d+)\}/g,
            (all, index) => (values[index] === undefined ? all : String(values[index])));
    },
};

// Every extension Qt Creator has and would run, whether or not it is running:
// asking for one that has not started yet is how an extension finds out that
// its neighbour is installed, and then starts it.
const knownExtensions = new Map(); // id -> {path, packageJSON}

function extensionApi(id) {
    const running = registeredExtensions.get(id);
    const entry = running || knownExtensions.get(id);
    if (!entry)
        return undefined;
    return {
        id,
        isActive: !!running,
        extensionPath: entry.path,
        extensionUri: vscode.Uri.file(entry.path),
        extensionKind: 1, // UI
        packageJSON: entry.packageJSON || {},
        get exports() { return (registeredExtensions.get(id) || {}).exports; },
        activate: async () => {
            if (!registeredExtensions.has(id))
                await request('extension/activate', {id});
            return (registeredExtensions.get(id) || {}).exports;
        },
    };
}

vscode.extensions = {
    get all() {
        const ids = new Set([...knownExtensions.keys(), ...registeredExtensions.keys()]);
        return [...ids].map(extensionApi);
    },
    // Proposed API, used by remote-explorer; there is only one host here.
    get allAcrossExtensionHosts() { return this.all; },
    getExtension: id => extensionApi(id),
    onDidChange: eventEmitter(),
};

vscode.lm = {
    tools: [],
    registerTool: noopDisposable,
    registerMcpServerDefinitionProvider: noopDisposable,
    selectChatModels: () => Promise.resolve([]),
    invokeTool: () => Promise.resolve({content: []}),
    onDidChangeChatModels: eventEmitter(),
};

const debugConfigurationProviders = new Map(); // debug type -> providers
const debugAdapterFactories = new Map(); // debug type -> factory
const debugSessions = new Map(); // id -> the session Qt Creator is running
const onDidStartDebugSession = eventEmitter();
const onDidTerminateDebugSession = eventEmitter();
const onDidChangeActiveDebugSession = eventEmitter();

// Qt Creator says when a session it started is over.
// A session can be over before startDebugging() has handed it out: the answer
// and the news can arrive together, and the answer is only unpacked once this
// has run. Remembering the id is what keeps the end from going unreported.
const terminatedEarly = new Set();

function endDebugSession(id) {
    const session = debugSessions.get(id);
    if (!session)
        return false;
    debugSessions.delete(id);
    if (vscode.debug.activeDebugSession === session) {
        vscode.debug.activeDebugSession = undefined;
        onDidChangeActiveDebugSession.fire(undefined);
    }
    onDidTerminateDebugSession.fire(session);
    return true;
}

onRequest('debug/terminated', params => {
    if (!endDebugSession(String(params.id)))
        terminatedEarly.add(String(params.id));
    return null;
});

vscode.debug = {
    activeDebugSession: undefined,
    activeDebugConsole: {append() {}, appendLine() {}},
    breakpoints: [],
    onDidStartDebugSession,
    onDidTerminateDebugSession,
    onDidChangeActiveDebugSession,
    onDidReceiveDebugSessionCustomEvent: eventEmitter(),
    onDidChangeBreakpoints: eventEmitter(),
    // Kept, not run: what an extension says about a debug type is the only
    // way to learn how its debugger is started - the manifest names a program
    // for two of the installed extensions, the rest answer at runtime.
    registerDebugConfigurationProvider(type, provider) {
        const providers = debugConfigurationProviders.get(type) || [];
        providers.push(provider);
        debugConfigurationProviders.set(type, providers);
        return disposable(() => {
            const rest = (debugConfigurationProviders.get(type) || [])
                             .filter(p => p !== provider);
            debugConfigurationProviders.set(type, rest);
        });
    },
    registerDebugAdapterDescriptorFactory(type, factory) {
        debugAdapterFactories.set(type, factory);
        return disposable(() => debugAdapterFactories.delete(type));
    },
    registerDebugAdapterTrackerFactory: noopDisposable,
    // Starting a session on what the extension asks for. It hands over a
    // configuration that is often only a type and a request - the rest is what
    // its own providers fill in, so they are asked first, exactly as they would
    // be before a session starts in VS Code.
    startDebugging: async (folder, nameOrConfiguration, options) => {
        if (typeof nameOrConfiguration === 'string') {
            // A name refers to an entry in the workspace's launch.json, which
            // Qt Creator does not keep.
            reportMissing('debug.startDebugging by configuration name');
            return false;
        }
        let configuration = Object.assign({request: 'launch'}, nameOrConfiguration || {});
        const workspaceFolder = folder
            || (vscode.workspace.workspaceFolders || [])[0];

        // Resolving can hand the session to another extension's debugger:
        // zephyr-ide asks for its own "zephyr-ide-cortex" and its provider
        // rewrites that to cortex-debug. The debugger it was handed to has to
        // resolve it in turn - cortex-debug puts the port of its server
        // console in there, and its adapter refuses the session without it.
        // Each type resolves once, so a provider that keeps its own type does
        // not spin here.
        const resolved = new Set();
        for (let type = configuration.type || ''; type && !resolved.has(type);
             type = configuration.type || type) {
            resolved.add(type);
            for (const provider of (debugConfigurationProviders.get(type) || [])
                     .concat(debugConfigurationProviders.get('*') || [])) {
                // A provider that answers with nothing is refusing the session
                // - cortex-debug does that while its console is not up yet -
                // and there is nothing to start. Taking the configuration it
                // was given instead starts a session it just said no to.
                for (const resolve of ['resolveDebugConfiguration',
                                       'resolveDebugConfigurationWithSubstitutedVariables']) {
                    if (typeof provider[resolve] !== 'function')
                        continue;
                    const answer = await provider[resolve](workspaceFolder, configuration);
                    if (!answer)
                        return false;
                    configuration = answer;
                }
            }
        }

        const adapter = await adapterDescriptorFor(configuration.type || '', configuration);
        const started = await request('debug/start', {configuration, adapter});
        if (!started)
            return false;
        const session = {
            id: String(started),
            type: configuration.type || type,
            name: configuration.name || type,
            workspaceFolder,
            configuration,
            customRequest: (command, args) => request(
                'debug/customRequest',
                {id: session.id, command, arguments: args || {}}),
            getDebugProtocolBreakpoint: () => Promise.resolve(undefined),
        };
        debugSessions.set(session.id, session);
        vscode.debug.activeDebugSession = session;
        onDidStartDebugSession.fire(session);
        // Over before it was handed out: the start is still what happened
        // first, and the end has to follow it rather than be lost.
        if (terminatedEarly.delete(session.id))
            endDebugSession(session.id);
        return true;
    },
    stopDebugging: session => {
        const ids = session ? [String(session.id)] : [...debugSessions.keys()];
        return Promise.all(ids.map(id => request('debug/stop', {id}))).then(() => undefined);
    },
    addBreakpoints() {},
    removeBreakpoints() {},
    asDebugSourceUri: uri => uri,
};

// The terminals an extension drives itself, by the id Qt Creator gave them.
const extensionTerminals = new Map(); // id -> {terminal, pty}

// Whatever is typed into one of them is input for the extension behind it.
onRequest('terminal/input', params => {
    const entry = extensionTerminals.get(String(params.id));
    if (entry && typeof entry.pty.handleInput === 'function')
        entry.pty.handleInput(String(params.text));
    return null;
});

onRequest('terminal/dimensions', params => {
    const entry = extensionTerminals.get(String(params.id));
    if (entry && typeof entry.pty.setDimensions === 'function')
        entry.pty.setDimensions({columns: params.columns, rows: params.rows});
    return null;
});

function createExtensionTerminal(options) {
    const pty = options.pty;
    let id;
    const pending = [];
    const terminal = {
        name: options.name || 'Terminal',
        processId: Promise.resolve(undefined),
        creationOptions: options,
        exitStatus: undefined,
        state: {isInteractedWith: false},
        shellIntegration: undefined,
        // Text sent to a terminal the extension owns is input for it, as it is
        // when someone types.
        sendText(text, addNewLine) {
            const line = text + (addNewLine === false ? '' : '\n');
            if (typeof pty.handleInput === 'function')
                pty.handleInput(line);
        },
        show() { notify('terminal/show', {id}); },
        hide() {},
        dispose() {
            if (typeof pty.close === 'function')
                pty.close();
            if (id !== undefined) {
                extensionTerminals.delete(id);
                notify('terminal/dispose', {id});
            }
            const index = vscode.window.terminals.indexOf(terminal);
            if (index >= 0)
                vscode.window.terminals.splice(index, 1);
            vscode.window.onDidCloseTerminal.fire(terminal);
        },
    };

    const write = text => {
        if (id === undefined)
            pending.push(text);
        else
            notify('terminal/write', {id, text: String(text)});
    };
    pty.onDidWrite(write);
    if (pty.onDidClose)
        pty.onDidClose(() => terminal.dispose());
    if (pty.onDidChangeName)
        pty.onDidChangeName(name => {
            terminal.name = name;
            if (id !== undefined)
                notify('terminal/setName', {id, name});
        });

    request('terminal/create', {name: terminal.name, pty: true,
                                cwd: options.cwd ? uriToString(options.cwd) : ''})
        .then(created => {
            id = String(created);
            terminal._id = created;
            extensionTerminals.set(id, {terminal, pty});
            while (pending.length)
                notify('terminal/write', {id, text: String(pending.shift())});
        })
        .catch(error => reportMissing('window.createTerminal (' + error.message + ')'));

    // The extension fills its terminal from here on: open() is where it starts
    // its own work, and cortex-debug's console only takes its port then.
    if (typeof pty.open === 'function')
        pty.open(undefined);

    vscode.window.terminals.push(terminal);
    vscode.window.onDidOpenTerminal.fire(terminal);
    return terminal;
}

const fileWatchers = new Map(); // id -> watcher
let nextWatcherId = 1;
const taskProviders = new Map(); // task type -> providers
const providedTasks = new Map(); // id -> task, for running one Qt Creator listed
const runningTasks = new Map(); // id -> execution, until it ends
let nextProvidedTaskId = 1;

// What Qt Creator needs to run a task: a command line, where to run it, and
// with what. A CustomExecution runs inside the extension and has no command,
// so it is not something that can be handed over.
function executionOf(task) {
    const execution = task && task.execution;
    if (!execution)
        return null;
    const options = execution.options || {};
    const common = {cwd: options.cwd || '', env: options.env || {}};
    if (execution.commandLine !== undefined)
        return Object.assign({kind: 'shell', commandLine: execution.commandLine}, common);
    if (execution.process !== undefined) {
        return Object.assign({kind: 'process', command: execution.process,
                              args: execution.args || []}, common);
    }
    if (execution.command !== undefined) {
        return Object.assign({kind: 'process', command: execution.command,
                              args: execution.args || []}, common);
    }
    return null;
}

vscode.tasks = {
    taskExecutions: [],
    registerTaskProvider(type, provider) {
        const providers = taskProviders.get(type) || [];
        providers.push(provider);
        taskProviders.set(type, providers);
        return disposable(() => {
            taskProviders.set(type, (taskProviders.get(type) || []).filter(p => p !== provider));
        });
    },
    // Qt Creator asks for these so a user can run them. The task objects stay
    // here - only what it takes to show one and to run it travels.
    _listForHost: async () => {
        providedTasks.clear();
        const listed = [];
        for (const [type, providers] of taskProviders) {
            for (const provider of providers) {
                const provided = await provider.provideTasks(
                    new CancellationTokenSource().token) || [];
                for (const task of provided) {
                    const id = 'task-' + (nextProvidedTaskId++);
                    providedTasks.set(id, task);
                    listed.push({id, type,
                                 name: task.name || '',
                                 source: task.source || '',
                                 detail: task.detail || '',
                                 // What kind of task it is, and who reads its
                                 // output: both decide where it belongs.
                                 group: (task.group && (task.group.id || task.group)) || '',
                                 problemMatchers: task.problemMatchers || []});
                }
            }
        }
        return listed;
    },
    fetchTasks: async filter => {
        const wanted = filter && filter.type;
        const tasks = [];
        for (const [type, providers] of taskProviders) {
            if (wanted && wanted !== type)
                continue;
            for (const provider of providers) {
                const provided = await provider.provideTasks(new CancellationTokenSource().token);
                for (const task of provided || [])
                    tasks.push(task);
            }
        }
        return tasks;
    },
    executeTask: async task => {
        const execution = executionOf(task);
        if (!execution) {
            reportMissing('tasks.executeTask(' + ((task && task.name) || '?') + ')');
            return {task, terminate() {}};
        }
        const id = await request('tasks/execute', {
            name: task.name || '', source: task.source || '', execution});
        const running = {task, terminate: () => notify('tasks/terminate', {id})};
        runningTasks.set(id, running);
        vscode.tasks.taskExecutions.push(running);
        vscode.tasks.onDidStartTask.fire({execution: running});
        vscode.tasks.onDidStartTaskProcess.fire({execution: running, processId: id});
        return running;
    },
    onDidStartTask: eventEmitter(),
    onDidEndTask: eventEmitter(),
    onDidStartTaskProcess: eventEmitter(),
    onDidEndTaskProcess: eventEmitter(),
};

// --- API call tracing --------------------------------------------------------
//
// Much of the surface above is a stub, so an extension activates happily and
// then quietly does nothing. With ALIEN_TRACE_API=1 every shim member an
// extension touches is logged once, which turns "what does this extension
// actually need?" into a measurement rather than a reading of this file.
if (process.env.ALIEN_TRACE_API) {
    const seen = new Set();
    const traceNamespace = (nsName, ns) => {
        if (!ns)
            return;
        for (const key of Object.keys(ns)) {
            // Accessors would run their getter just by being read here.
            const descriptor = Object.getOwnPropertyDescriptor(ns, key);
            if (!descriptor || !descriptor.writable)
                continue;
            const value = descriptor.value;
            // An event is a callable carrying .fire(); wrapping it would drop
            // that and silently break every listener.
            if (typeof value !== 'function' || typeof value.fire === 'function')
                continue;
            const traced = function (...args) {
                const name = nsName + '.' + key;
                if (!seen.has(name)) {
                    seen.add(name);
                    logToStderr('[api] ' + name);
                }
                return value.apply(this, args);
            };
            Object.assign(traced, value);
            ns[key] = traced;
        }
    };
    for (const nsName of ['commands', 'window', 'workspace', 'languages', 'env',
                          'extensions', 'debug', 'tasks'])
        traceNamespace(nsName, vscode[nsName]);
}

// The answer is which item was picked, and what an extension gets back is the
// item it passed - a string or its own object, as it handed it over.
function showMessage(level, message, items) {
    // The first argument after the message may be options rather than a
    // choice: a modal message is one the user has to answer, and its detail is
    // the second line of it.
    const options = (items && items.length && items[0] && typeof items[0] === 'object'
                     && items[0].title === undefined) ? items[0] : undefined;
    if (options)
        items = items.slice(1);
    const answers = (items || []).filter(i => i !== null && i !== undefined
                                              && (typeof i === 'string' || i.title));
    return request('window/showMessage', {level, message, items: flattenItems(items),
                                          modal: !!(options && options.modal),
                                          detail: (options && options.detail) || ''})
        .then(index => (typeof index === 'number' && index >= 0 ? answers[index] : undefined));
}

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

// A member the shim never heard of reads as undefined, and the extension dies
// later with "x is not a function", nowhere near the cause. Name it on the way
// out - but keep returning undefined, so that extensions probing for a newer
// API still get an honest answer.
// Not API: module interop and promise resolution probe for these.
const interopProbes = new Set(['then', 'default', '__esModule']);

function reportMissingMembers(name, namespace) {
    return new Proxy(namespace, {
        get(object, property, receiver) {
            if (typeof property === 'string' && !(property in object)
                && !interopProbes.has(property)) {
                reportMissing(name + '.' + property);
            }
            return Reflect.get(object, property, receiver);
        },
    });
}

for (const name of Object.keys(vscode)) {
    const namespace = vscode[name];
    if (namespace && typeof namespace === 'object' && !Array.isArray(namespace))
        vscode[name] = reportMissingMembers('vscode.' + name, namespace);
}

// Extensions see the reporting proxy, the host itself the plain object, so
// that only an extension's reach beyond the shim is reported. A whole missing
// namespace (vscode.chat) is reported the same way as a missing member.
const vscodeModule = reportMissingMembers('vscode', vscode);

// Make require('vscode') and require('vscode-languageclient') resolve to shims.
const originalLoad = Module._load;
Module._load = function (request, parent, isMain) {
    if (request === 'vscode')
        return vscodeModule;
    if (request === 'vscode-languageclient' || request === 'vscode-languageclient/node')
        return languageClientModule;
    return originalLoad.apply(this, arguments);
};

// --- extension lifecycle ----------------------------------------------------

const activated = new Map(); // extension id -> module exports

// Activations run one at a time so a dependency finishes (and registers its
// exports) before a dependent activates. Other messages - including responses
// awaited during an activation - keep flowing concurrently.
let activationChain = Promise.resolve();
onRequest('activate', params => {
    const result = activationChain.then(() => doActivate(params));
    activationChain = result.catch(() => {});
    return result;
});

async function doActivate(params) {
    const {id, main} = params;
    const langMap = new Map();
    for (const language of (params.languages || []))
        langMap.set(language.id, language.extensions || []);

    // Both of these have to be in place before the module runs, not after
    // activate() returns: extensions read their own declared defaults during
    // load, and look themselves up through vscode.extensions.getExtension()
    // while activating (cmake-tools does, and fails with "Extension is
    // undefined!"). The Extension object exists from load; only
    // exports fill in later, which the registration at the end of this
    // function then does.
    registerConfigurationDefaults(params.packageJSON);
    registeredExtensions.set(id, {exports: undefined, path: params.path,
                                  packageJSON: params.packageJSON});

    const exports = require(main);

    const os = require('os');
    const fs = require('fs');
    const path = require('path');
    // Qt Creator hands over a place that outlives the session; without one -
    // a host running on a device - fall back to a scratch directory.
    const storageRoot = params.storagePath
                        || path.join(os.tmpdir(), 'alien-host', id.replace(/[^\w.-]/g, '_'));
    const storageDir = path.join(storageRoot, 'storage');
    const globalStorageDir = path.join(storageRoot, 'globalStorage');
    const logDir = path.join(storageRoot, 'log');
    for (const dir of [storageDir, globalStorageDir, logDir]) {
        try { fs.mkdirSync(dir, {recursive: true}); } catch (e) { logToStderr('mkdir', e); }
    }

    // globalState/workspaceState are how an extension remembers anything at
    // all - a choice, a "do not ask again", a cache. Answering every get()
    // with the default made every extension forgetful, silently.
    const memento = (which) => {
        const file = path.join(storageRoot, which + '.json');
        let data = {};
        try {
            data = JSON.parse(fs.readFileSync(file, 'utf8'));
        } catch (e) { /* first run, or unreadable: start empty */ }
        return {
            get: (key, fallback) => (key in data ? data[key] : fallback),
            update: (key, value) => {
                if (value === undefined)
                    delete data[key];
                else
                    data[key] = value;
                try {
                    fs.writeFileSync(file, JSON.stringify(data, null, 1));
                } catch (e) {
                    logToStderr('cannot store ' + which, e);
                }
                return Promise.resolve();
            },
            keys: () => Object.keys(data),
            setKeysForSync() {},
        };
    };
    // Per workspace; the folders are known by now.
    const folders = vscode.workspace.workspaceFolders || [];
    const workspaceKey = folders.length
        ? 'workspaceState-' + require('crypto').createHash('sha1')
              .update(String(folders[0].uri ? folders[0].uri.path : folders[0])).digest('hex').slice(0, 12)
        : 'workspaceState';
    const context = {
        subscriptions: [],
        extensionPath: params.path,
        extensionUri: vscode.Uri.file(params.path),
        extensionMode: 1, // Production
        globalState: memento('globalState'),
        workspaceState: memento(workspaceKey),
        // A token, a password: kept where Qt Creator keeps its own, and not in
        // the settings an extension can read from another. A store that
        // forgets means being asked to log in again on every start.
        secrets: (() => {
            const onDidChange = eventEmitter();
            return {
                get: key => request('secrets/read', {extension: id, key})
                                .then(value => (value === null ? undefined : value)),
                store: (key, value) => request('secrets/write',
                                               {extension: id, key, value: String(value)})
                                           .then(() => { onDidChange.fire({key}); }),
                delete: key => request('secrets/write', {extension: id, key, value: null})
                                   .then(() => { onDidChange.fire({key}); }),
                onDidChange,
            };
        })(),
        storageUri: vscode.Uri.file(storageDir),
        storagePath: storageDir,
        globalStorageUri: vscode.Uri.file(globalStorageDir),
        globalStoragePath: globalStorageDir,
        logUri: vscode.Uri.file(logDir),
        logPath: logDir,
        environmentVariableCollection: {
            persistent: true,
            replace() {}, append() {}, prepend() {}, get() {}, forEach() {},
            delete() {}, clear() {}, getScoped() { return this; },
        },
        extension: {
            id,
            extensionPath: params.path,
            extensionUri: vscode.Uri.file(params.path),
            isActive: true,
            packageJSON: params.packageJSON || {},
        },
        asAbsolutePath: rel => path.join(params.path, rel),
    };
    activated.set(id, {exports, context});
    activating = {id, langMap};
    let api;
    try {
        if (typeof exports.activate === 'function')
            api = await exports.activate(context);
    } finally {
        activating = null;
    }
    // The value activate() returns is the extension's public API (its exports).
    registeredExtensions.set(id, {exports: api, path: params.path, packageJSON: params.packageJSON});
    updateContributedMenus(); // the manifest that says what may be offered is known now
    return {ok: true};
}

// What it takes to start an extension's debugger: the configuration its
// providers fill in, and the adapter its factory names. Both are answered by
// the extension, so both have to be asked for here.
// The configurations an extension offers when there is none yet, which is what
// fills a new launch configuration - asking only for a resolved one assumes the
// user already knows what to write.
onRequest('debug/configurations', async params => {
    const type = params.type || '';
    const folder = (vscode.workspace.workspaceFolders || [])[0];
    const offered = [];
    for (const provider of debugConfigurationProviders.get(type) || []) {
        if (typeof provider.provideDebugConfigurations !== 'function')
            continue;
        const provided = await provider.provideDebugConfigurations(folder, cancellationToken);
        for (const configuration of provided || [])
            offered.push(configuration);
    }
    return {configurations: offered};
});

// What the extension says its adapter is, for a type it has claimed. Only an
// extension can answer this: the descriptor comes from code it registered.
// Null means it did not register one, which is what most of them do - their
// adapter is named in the manifest, and Qt Creator reads that itself.
async function adapterDescriptorFor(type, configuration) {
    const factory = debugAdapterFactories.get(type);
    if (!factory || typeof factory.createDebugAdapterDescriptor !== 'function')
        return null;
    const session = {id: 'alien-' + type, type, name: configuration.name,
                     configuration,
                     workspaceFolder: (vscode.workspace.workspaceFolders || [])[0]};
    const descriptor = await factory.createDebugAdapterDescriptor(session, undefined);
    if (!descriptor)
        return null;
    if (descriptor instanceof DebugAdapterExecutable) {
        return {kind: 'executable', command: descriptor.command, args: descriptor.args,
                cwd: (descriptor.options || {}).cwd || '',
                env: (descriptor.options || {}).env || {}};
    }
    if (descriptor instanceof DebugAdapterServer)
        return {kind: 'server', host: descriptor.host || 'localhost', port: descriptor.port};
    if (descriptor instanceof DebugAdapterNamedPipeServer)
        return {kind: 'pipe', path: descriptor.path};
    // An adapter implemented in the extension itself: nothing to spawn, it
    // would have to be spoken to through this connection.
    return {kind: 'inline'};
}

onRequest('debug/resolve', async params => {
    const type = params.type || '';
    const folder = (vscode.workspace.workspaceFolders || [])[0];
    let config = Object.assign({type, request: 'launch', name: type},
                               params.configuration || {});

    for (const provider of debugConfigurationProviders.get(type) || []) {
        for (const resolve of ['resolveDebugConfiguration',
                               'resolveDebugConfigurationWithSubstitutedVariables']) {
            if (typeof provider[resolve] !== 'function')
                continue;
            const resolved = await provider[resolve](folder, config);
            // Refused: there is no configuration to hand out.
            if (!resolved)
                return {};
            config = resolved;
        }
    }

    return {configuration: config, adapter: await adapterDescriptorFor(type, config)};
});

onRequest('extensions/known', params => {
    knownExtensions.clear();
    for (const entry of params.extensions || []) {
        knownExtensions.set(entry.id, {path: entry.path,
                                       packageJSON: entry.packageJSON || {}});
    }
    vscode.extensions.onDidChange.fire();
});

onRequest('progress/cancel', params => {
    const source = progressCancellations.get(params.id);
    if (source)
        source.cancel();
});

onRequest('executeCommand', async params => {
    // A key bound only in a particular place does nothing anywhere else, which
    // is the whole point of the clause the manifest put on it.
    if (params.when && !evaluateWhen(params.when, contextValueOfKey))
        return null;
    // A command contributed for a file is called with that file, and what it
    // expects is a Uri rather than the path Qt Creator sent.
    const args = (params.args || []).map(
        a => (a && typeof a === 'object' && typeof a.$uri === 'string')
                 ? vscode.Uri.file(a.$uri) : a);
    return vscode.commands.executeCommand(params.command, ...args);
});

onRequest('deactivate', async params => {
    const entry = activated.get(params.id);
    if (!entry)
        return null;
    activated.delete(params.id);
    registeredExtensions.delete(params.id);
    // The extension's deactivate() runs first, then what it
    // registered in context.subscriptions (in reverse registration order).
    try {
        if (typeof entry.exports.deactivate === 'function')
            await entry.exports.deactivate();
    } catch (e) { logToStderr('deactivate error', e); }
    for (const d of entry.context.subscriptions.reverse()) {
        try { d && d.dispose && d.dispose(); } catch (e) { logToStderr('dispose error', e); }
    }
    return null;
});

onRequest('ping', async () => ({pong: true}));

onRequest('configuration/update', params => {
    const next = (params && params.config) || {};
    const changed = changedConfigurationKeys(configuration, next);
    configuration = next;
    if (changed.length)
        onDidChangeConfiguration.fire(configurationChangeEvent(changed));
});

onRequest('workspace/updateFolders', params => {
    const folders = ((params && params.folders) || []).map((f, index) => ({
        uri: vscode.Uri.file(f.path),
        name: f.name || f.path,
        index,
    }));
    vscode.workspace.workspaceFolders = folders.length ? folders : undefined;
    vscode.workspace.name = folders.length ? folders[0].name : undefined;
    vscode.workspace.onDidChangeWorkspaceFolders.fire({added: folders, removed: []});
    // Let config-driven consumers (e.g. qt-core) re-read per-folder settings.
    onDidChangeConfiguration.fire(configurationChangeEvent(undefined));
});

// --- document sync (Qt Creator -> vscode.workspace) -------------------------

onRequest('document/didOpen', params => {
    if (documents.has(params.uri))
        return;
    const document = makeTextDocument(params);
    documents.set(params.uri, document);
    vscode.workspace.textDocuments.push(document);
    onDidOpenTextDocument.fire(document);
    fireTabsChanged(tabGroup().tabs.filter(tab => tab.label === nodePath.basename(params.uri)));
});

const fileUris = params => ((params && params.files) || []).map(f => vscode.Uri.file(f));

onRequest('editor/didChangeVisible', params => {
    const editors = ((params && params.uris) || [])
        .map(uri => documents.get(uri))
        .filter(document => document)
        .map(document => makeTextEditor(document));
    vscode.window.visibleTextEditors = editors;
    vscode.window.onDidChangeVisibleTextEditors.fire(editors);
});

onRequest('files/didCreate', params => {
    vscode.workspace.onDidCreateFiles.fire({files: fileUris(params)});
});

onRequest('files/didDelete', params => {
    vscode.workspace.onDidDeleteFiles.fire({files: fileUris(params)});
});

onRequest('files/didRename', params => {
    vscode.workspace.onDidRenameFiles.fire({
        files: [{oldUri: vscode.Uri.file(params.from), newUri: vscode.Uri.file(params.to)}],
    });
});

onRequest('document/willSave', params => {
    const document = documents.get(params.uri);
    if (!document)
        return;
    // The reason VS Code gives is why the save started; ours are all the user
    // asking for it. waitUntil() is accepted and ignored: an edit handed back
    // here would have to reach the file before it is written, and nothing of
    // ours can hold the save open that long.
    onWillSaveTextDocument.fire({
        document,
        reason: 1, // TextDocumentSaveReason.Manual
        waitUntil: () => reportMissing('onWillSaveTextDocument.waitUntil'),
    });
});

onRequest('document/didSave', params => {
    const document = documents.get(params.uri);
    if (document)
        onDidSaveTextDocument.fire(document);
});

onRequest('document/didChange', params => {
    const document = documents.get(params.uri);
    if (!document)
        return;
    // The text is what the document now says; the offsets say what the user
    // did to get there. Expressed as the ranged edit an extension reads - and
    // vscode-languageclient forwards to servers that sync incrementally.
    const oldText = document._text;
    const newText = params.text || '';
    const known = typeof params.position === 'number' && params.charsRemoved !== undefined;
    const offset = known ? Math.max(0, Math.min(params.position, oldText.length)) : 0;
    const removed = known ? Math.max(0, Math.min(params.charsRemoved, oldText.length - offset))
                          : oldText.length;
    const added = known ? Math.max(0, params.charsAdded) : newText.length;
    // Positions in the document as it was, which is the one still in hand.
    const positionOf = text => {
        return at => {
            const before = text.slice(0, at);
            const line = (before.match(/\n/g) || []).length;
            return {line, character: at - before.lastIndexOf('\n') - 1};
        };
    };
    const at = positionOf(oldText);
    const change = {
        range: {start: at(offset), end: at(offset + removed)},
        rangeOffset: offset,
        rangeLength: removed,
        text: newText.slice(offset, offset + added),
    };
    document._text = newText;
    document.version = params.version;
    onDidChangeTextDocument.fire({document, contentChanges: [change],
                                  reason: params.reason || undefined});
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
    fireTabsChanged([], [{label: nodePath.basename(params.uri),
                          input: new TabInputText(document.uri),
                          isActive: false, isDirty: false,
                          isPinned: false, isPreview: false}]);
});

// The caret moved in Qt Creator. Extensions that follow it - a preview
// scrolling with the cursor - listen for this.
// Qt Creator asks when the user formats the document; the answer is the edits
// the extension would make, in the document's own coordinates.
// Formatting only what is selected. A different provider from the one that
// formats the whole file: an extension may offer either, or both.
onRequest('rangeFormatting/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return [];
    const options = {tabSize: params.tabSize || 4, insertSpaces: !!params.insertSpaces};
    const range = new Range(new Position(params.startLine, params.startCharacter),
                            new Position(params.endLine, params.endCharacter));
    for (const {selector, provider} of rangeFormattingProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        if (typeof provider.provideDocumentRangeFormattingEdits !== 'function')
            continue;
        const edits = await provider.provideDocumentRangeFormattingEdits(
            document, range, options,
            {isCancellationRequested: false, onCancellationRequested: () => disposable(() => {})});
        if (edits && edits.length) {
            return edits.map(edit => ({range: rangeToJson(edit.range),
                                       newText: edit.newText || ''}));
        }
    }
    return [];
});

onRequest('formatting/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return [];
    const options = {tabSize: params.tabSize || 4, insertSpaces: !!params.insertSpaces};
    for (const {selector, provider} of formattingProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        if (typeof provider.provideDocumentFormattingEdits !== 'function')
            continue;
        const edits = await provider.provideDocumentFormattingEdits(
            document, options, {isCancellationRequested: false,
                                onCancellationRequested: () => disposable(() => {})});
        if (edits && edits.length) {
            return edits.map(edit => ({range: rangeToJson(edit.range),
                                       newText: edit.newText || ''}));
        }
    }
    return [];
});

// What an extension offers to do about the text under the cursor. Only the
// actions carrying an edit are answered: one that runs a command of the
// extension's own would have to be run there, which is the next step.
onRequest('codeAction/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return [];
    const range = new Range(new Position(params.startLine, params.startCharacter),
                            new Position(params.endLine, params.endCharacter));
    const answers = [];
    for (const {selector, provider} of codeActionProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        if (typeof provider.provideCodeActions !== 'function')
            continue;
        const actions = await provider.provideCodeActions(
            document, range, {diagnostics: [], only: undefined, triggerKind: 1},
            {isCancellationRequested: false, onCancellationRequested: () => disposable(() => {})});
        for (let action of actions || []) {
            if (!action)
                continue;
            // An action the extension says cannot be applied here, and why.
            if (action.disabled)
                continue;
            // Its edit may be worked out only for the ones actually offered.
            if (!action.edit && !action.command
                && typeof provider.resolveCodeAction === 'function') {
                action = await provider.resolveCodeAction(action, cancellationToken) || action;
            }
            const edit = action.edit;
            const changes = (edit && typeof edit.operations === 'function')
                ? edit.operations().map(operation => (operation.kind !== 'edit' ? operation : {
                    kind: 'edit',
                    path: uriToString(operation.uri),
                    edits: operation.edits.map(e => ({range: rangeToJson(e.range),
                                                      newText: e.newText || ''})),
                }))
                : [];
            const command = action.command || (typeof action.command === 'string' ? action : null);
            // An action does something: it edits, or it runs a command.
            if (!changes.length && !command)
                continue;
            answers.push({
                title: action.title || (command && command.title) || '',
                changes: changes.filter(operation => operation.kind === 'edit'),
                command: command ? (command.command || '') : '',
                arguments: (command && command.arguments) || [],
                isPreferred: !!action.isPreferred,
            });
        }
    }
    // What the extension calls the obvious choice comes first.
    answers.sort((a, b) => (b.isPreferred ? 1 : 0) - (a.isPreferred ? 1 : 0));
    return answers;
});

// Renaming what is under the cursor, everywhere the extension knows of. The
// answer is the whole edit, which may well reach files that are not open.
onRequest('rename/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return [];
    const position = new Position(params.line, params.character);
    for (const {selector, provider} of renameProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        if (typeof provider.provideRenameEdits !== 'function')
            continue;
        const edit = await provider.provideRenameEdits(
            document, position, params.newName,
            {isCancellationRequested: false, onCancellationRequested: () => disposable(() => {})});
        if (edit && typeof edit.operations === 'function') {
            return edit.operations()
                .filter(operation => operation.kind === 'edit')
                .map(operation => ({
                    path: uriToString(operation.uri),
                    edits: operation.edits.map(e => ({range: rangeToJson(e.range),
                                                      newText: e.newText || ''})),
                }));
        }
    }
    return [];
});

// Everywhere the extension knows the thing under the cursor is used. Each one
// is a place in a file, which is what a search result is made of.
onRequest('references/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return [];
    const position = new Position(params.line, params.character);
    for (const {selector, provider} of referenceProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        if (typeof provider.provideReferences !== 'function')
            continue;
        const locations = await provider.provideReferences(
            document, position, {includeDeclaration: true},
            {isCancellationRequested: false, onCancellationRequested: () => disposable(() => {})});
        if (locations && locations.length) {
            return locations.map(location => ({path: uriToString(location.uri),
                                               range: rangeToJson(location.range)}));
        }
    }
    return [];
});

// What is in the document, as the extension sees it. Either shape is allowed:
// a flat list naming a container per symbol, or a tree of children.
function serializeSymbol(symbol) {
    const range = symbol.range || (symbol.location && symbol.location.range);
    return {
        name: symbol.name || '',
        detail: symbol.detail || symbol.containerName || '',
        kind: symbol.kind === undefined ? 0 : symbol.kind,
        range: rangeToJson(range),
        selectionRange: rangeToJson(symbol.selectionRange || range),
        tags: symbol.tags || [],
        children: (symbol.children || []).map(serializeSymbol),
    };
}

onRequest('symbols/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return [];
    for (const {selector, provider} of symbolProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        if (typeof provider.provideDocumentSymbols !== 'function')
            continue;
        const symbols = await provider.provideDocumentSymbols(
            document,
            {isCancellationRequested: false, onCancellationRequested: () => disposable(() => {})});
        if (symbols && symbols.length)
            return symbols.map(serializeSymbol);
    }
    return [];
});

// The other places the thing under the cursor appears, which the editor marks
// while the cursor sits on it.
onRequest('highlights/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return [];
    const position = new Position(params.line, params.character);
    for (const {selector, provider} of highlightProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        if (typeof provider.provideDocumentHighlights !== 'function')
            continue;
        const highlights = await provider.provideDocumentHighlights(
            document, position,
            {isCancellationRequested: false, onCancellationRequested: () => disposable(() => {})});
        if (highlights && highlights.length)
            return highlights.map(highlight => rangeToJson(highlight.range || highlight));
    }
    return [];
});

// The places in the document that point somewhere. A link may arrive without
// its target, to be filled in only if the user follows it.
onRequest('links/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return [];
    for (const {selector, provider} of linkProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        if (typeof provider.provideDocumentLinks !== 'function')
            continue;
        const links = await provider.provideDocumentLinks(
            document,
            {isCancellationRequested: false, onCancellationRequested: () => disposable(() => {})});
        if (!links || !links.length)
            continue;
        const answers = [];
        for (let link of links) {
            if (!link.target && typeof provider.resolveDocumentLink === 'function') {
                link = await provider.resolveDocumentLink(
                    link, {isCancellationRequested: false,
                           onCancellationRequested: () => disposable(() => {})}) || link;
            }
            if (!link.target)
                continue;
            answers.push({range: rangeToJson(link.range),
                          tooltip: link.tooltip || '',
                          target: String(link.target),
                          scheme: link.target.scheme || 'file',
                          path: link.target.path === undefined ? String(link.target)
                                                              : link.target.fsPath});
        }
        if (answers.length)
            return answers;
    }
    return [];
});

// What the call being typed expects. The label of each signature is what the
// editor shows; the active one and the argument the cursor is in say which part
// of it to point at.
// A parameter names itself either outright or as where it sits in the
// signature, which is the form a language server's answer takes.
function parameterLabel(signatureLabel, label) {
    if (typeof label === 'string')
        return label;
    if (Array.isArray(label) && label.length === 2)
        return String(signatureLabel).slice(label[0], label[1]);
    return '';
}

onRequest('signature/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return null;
    const position = new Position(params.line, params.character);
    for (const {selector, provider} of signatureProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        if (typeof provider.provideSignatureHelp !== 'function')
            continue;
        const help = await provider.provideSignatureHelp(
            document, position,
            {isCancellationRequested: false, onCancellationRequested: () => disposable(() => {})},
            {triggerKind: 1, triggerCharacter: params.triggerCharacter || undefined,
             isRetrigger: false, activeSignatureHelp: undefined});
        if (!help || !help.signatures || !help.signatures.length)
            continue;
        return {
            signatures: help.signatures.map(signature => ({
                label: signature.label || '',
                documentation: typeof signature.documentation === 'string'
                    ? signature.documentation
                    : ((signature.documentation && signature.documentation.value) || ''),
                parameters: (signature.parameters || []).map(parameter => ({
                    label: parameterLabel(signature.label || '', parameter.label),
                })),
            })),
            activeSignature: help.activeSignature || 0,
            activeParameter: help.activeParameter || 0,
        };
    }
    return null;
});

// Everything in the workspace whose name matches what was typed. Asked on
// every keystroke of a search, so an extension that answers slowly is one the
// user waits for - which is why the answer carries where it came from and
// nothing more.
onRequest('workspaceSymbols/provide', async params => {
    const query = params.query || '';
    const answers = [];
    for (const {provider} of workspaceSymbolProviders) {
        if (typeof provider.provideWorkspaceSymbols !== 'function')
            continue;
        const symbols = await provider.provideWorkspaceSymbols(
            query,
            {isCancellationRequested: false, onCancellationRequested: () => disposable(() => {})});
        for (const symbol of symbols || []) {
            const location = symbol.location || {};
            const range = location.range || symbol.range;
            if (!location.uri || !range)
                continue;
            answers.push({
                name: symbol.name || '',
                container: symbol.containerName || '',
                kind: symbol.kind === undefined ? 0 : symbol.kind,
                path: uriToString(location.uri),
                range: rangeToJson(range),
            });
        }
    }
    return answers;
});

// Qt Creator watches; this only has to hand the news to whoever asked for it.
// A task that was started here has finished. Extensions wait on this to know
// that a build is done - without it they wait for as long as they run.
// A terminal that has finished says so, with the code it finished on: an
// extension waiting for its process reads that and not a bare undefined.
onRequest('terminal/ended', params => {
    for (const terminal of vscode.window.terminals) {
        if (terminal._id !== params.id)
            continue;
        terminal.exitStatus = {code: params.exitCode, reason: 0};
        vscode.window.onDidCloseTerminal.fire(terminal);
    }
    return null;
});

onRequest('tasks/ended', params => {
    const running = runningTasks.get(params.id);
    if (!running)
        return null;
    runningTasks.delete(params.id);
    const index = vscode.tasks.taskExecutions.indexOf(running);
    if (index >= 0)
        vscode.tasks.taskExecutions.splice(index, 1);
    vscode.tasks.onDidEndTaskProcess.fire({execution: running, exitCode: params.exitCode});
    vscode.tasks.onDidEndTask.fire({execution: running});
    return null;
});

onRequest('editor/didChangeFocus', params => {
    editorFocusState.focused = !!params.focused;
    editorFocusState.readOnly = !!params.readOnly;
    updateContributedMenus();
    return null;
});

onRequest('watch/event', params => {
    const watcher = fileWatchers.get(params.id);
    if (!watcher)
        return null;
    const uri = vscode.Uri.file(params.path);
    if (params.kind === 'created') {
        if (!watcher.ignoreCreateEvents)
            watcher.onDidCreate.fire(uri);
    } else if (params.kind === 'deleted') {
        if (!watcher.ignoreDeleteEvents)
            watcher.onDidDelete.fire(uri);
    } else if (!watcher.ignoreChangeEvents) {
        watcher.onDidChange.fire(uri);
    }
    return null;
});

onRequest('tasks/list', async () => ({tasks: await vscode.tasks._listForHost()}));

onRequest('tasks/run', async params => {
    const task = providedTasks.get(params.id);
    if (!task)
        return {started: false};
    // Through the same path an extension uses to run one of its own, so a
    // provided task behaves the way that extension expects.
    const execution = await vscode.tasks.executeTask(task);
    return {started: !!execution};
});

onRequest('editor/didChangeSelection', params => {
    const editor = vscode.window.activeTextEditor;
    if (!editor || !editor.document)
        return;
    if (uriToString(editor.document.uri) !== params.uri)
        return; // a stale notification for an editor that is no longer active

    const selection = new Selection(
        new Position(params.anchorLine, params.anchorCharacter),
        new Position(params.activeLine, params.activeCharacter));
    editor.selection = selection;
    editor.selections = [selection];
    onDidChangeTextEditorSelection.fire({
        textEditor: editor,
        selections: [selection],
        kind: undefined,
    });
});

onRequest('editor/didChangeActive', params => {
    if (params.tabSize)
        editorOptions.tabSize = params.tabSize;
    editorOptions.insertSpaces = !!params.insertSpaces;
    const document = params.uri ? documents.get(params.uri) : undefined;
    vscode.window.activeTextEditor = document ? makeTextEditor(document) : undefined;
    activeTabUri = document ? params.uri : undefined;
    onDidChangeActiveTextEditor.fire(vscode.window.activeTextEditor);
    fireTabsChanged([], [], tabGroup().tabs.filter(tab => tab.isActive));
    // A "when" clause reads the file being edited, so what is offered for one
    // is not what is offered for the next.
    updateContributedMenus();
});

// --- language features (Qt Creator -> host providers) -----------------------

const cancellationToken = {
    isCancellationRequested: false,
    onCancellationRequested: () => disposable(() => {}),
};

// The items last offered, so that the one taken can be asked about. A server
// works out an item's documentation and its import line only for that one,
// which is why it is asked for then and not for the whole list.
const offeredCompletions = new Map(); // id -> {item, provider}
let previousCompletionIds = [];
let nextCompletionId = 1;

onRequest('completion/resolve', async params => {
    const offered = offeredCompletions.get(params.id);
    if (!offered)
        return null;
    const {item, provider} = offered;
    if (typeof provider.resolveCompletionItem !== 'function')
        return serializeCompletion(item);
    try {
        const resolved = await provider.resolveCompletionItem(item, cancellationToken);
        return serializeCompletion(resolved || item);
    } catch (e) {
        logToStderr('completion resolve error', e);
        return serializeCompletion(item);
    }
});

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
    const offered = [];
    for (const {selector, provider} of completionProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        try {
            const result = await provider.provideCompletionItems(
                document, position, cancellationToken, context);
            if (!result)
                continue;
            const list = Array.isArray(result) ? result : (result.items || []);
            for (const item of list) {
                const id = 'completion-' + (nextCompletionId++);
                offeredCompletions.set(id, {item, provider});
                offered.push(id);
                items.push({...serializeCompletion(item), id});
            }
        } catch (e) {
            logToStderr('completion provider error', e);
        }
    }
    // The last two answers stay askable: the editor re-asks as the user types,
    // and the item being taken came from one of them.
    for (const id of previousCompletionIds)
        offeredCompletions.delete(id);
    previousCompletionIds = offered;
    return {items};
});

onRequest('hover/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return {contents: ''};
    const parts = [];
    const position = new Position(params.position.line, params.position.character);
    for (const {selector, provider} of hoverProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        try {
            const hover = await provider.provideHover(document, position, cancellationToken);
            if (hover && hover.contents !== null && hover.contents !== undefined)
                parts.push(hoverContentsToString(hover.contents));
        } catch (e) {
            logToStderr('hover provider error', e);
        }
    }
    // Every provider that has something to say about this word says it, which
    // is one tooltip with all of it rather than whichever registered first.
    return {contents: parts.filter(part => part).join('\n\n')};
});

// Where the type of the thing under the cursor is declared, which is a
// different question from where the thing itself is.
// What an extension has to say about particular lines - how many uses a symbol
// has, that a test can be run from here - and the command behind each.
// A colour an extension found in the text. Qt Creator draws nothing inside a
// line, so it goes where the editor already says things about one.
// What an extension offers for a file in the project tree ("explorer/context").
// The clause is about the file that was picked, not the one being edited, so it
// is read against that one; keys that are not about a resource still answer as
// they always do.
onRequest('menus/forResource', params => {
    const path = params.path || '';
    const name = nodePath.basename(path);
    const lookup = key => {
        switch (key) {
        case 'resourcePath': return path;
        case 'resourceDirname': return nodePath.dirname(path);
        case 'resourceFilename': return name;
        case 'resourceExtname': return nodePath.extname(name);
        case 'resourceLangId': return params.languageId || '';
        case 'resourceScheme': return 'file';
        case 'explorerResourceIsFolder': return !!params.isFolder;
        }
        return contextValueOfKey(key);
    };
    const shown = [];
    for (const registered of registeredExtensions.values()) {
        const contributes = (registered.packageJSON || {}).contributes || {};
        const titles = new Map();
        for (const command of contributes.commands || [])
            titles.set(command.command, command.title || command.command);
        for (const entry of (contributes.menus || {})[params.location] || []) {
            const id = entry.command;
            if (!id || !commandHandlers.has(id))
                continue;
            if (entry.when !== undefined && !evaluateWhen(entry.when, lookup))
                continue;
            shown.push({command: id, title: titles.get(id) || id});
        }
    }
    return {items: shown};
});

// Where a server says the text folds. Without it Qt Creator folds by indent,
// which is a guess about a language it does not know.
// What the server calls each piece of the text. The tokens arrive packed as
// five numbers each, relative to the one before, and the legend says which
// name a number stands for - so they are unpacked here, where the legend is.
// What the extension would write next, shown ahead of the caret rather than
// in a list. An item carries the text and, when it replaces something, where.
onRequest('inlineCompletion/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return [];
    const position = new Position(params.line, params.character);
    const context = {triggerKind: 0, selectedCompletionInfo: undefined};
    for (const {selector, provider} of inlineCompletionProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        if (typeof provider.provideInlineCompletionItems !== 'function')
            continue;
        const answer = await provider.provideInlineCompletionItems(
            document, position, context, cancellationToken);
        const items = Array.isArray(answer) ? answer : ((answer && answer.items) || []);
        const suggestions = [];
        for (const item of items) {
            const text = item && (item.insertText !== undefined ? item.insertText : item.text);
            if (typeof text !== 'string' || !text)
                continue;
            const range = item.range || new Range(position, position);
            suggestions.push({text, range: rangeToJson(range),
                              line: position.line, character: position.character});
        }
        if (suggestions.length)
            return suggestions;
    }
    return [];
});

// What the extension would rewrite now that this character has been typed -
// closing a brace, ending a statement. The edits come back and are applied as
// any other edit is.
onRequest('onTypeFormatting/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return [];
    const position = new Position(params.line, params.character);
    const options = {tabSize: params.tabSize || 4, insertSpaces: params.insertSpaces !== false};
    for (const {selector, provider, triggers} of onTypeProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        if (!triggers.includes(params.ch))
            continue;
        if (typeof provider.provideOnTypeFormattingEdits !== 'function')
            continue;
        const edits = await provider.provideOnTypeFormattingEdits(
            document, position, params.ch, options, cancellationToken) || [];
        const answers = [];
        for (const edit of edits) {
            if (edit && edit.range)
                answers.push({range: rangeToJson(edit.range), newText: edit.newText || ''});
        }
        if (answers.length)
            return answers;
    }
    return [];
});

onRequest('semanticTokens/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return [];
    for (const {selector, provider, types, names} of semanticProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        if (typeof provider.provideDocumentSemanticTokens !== 'function')
            continue;
        const tokens = await provider.provideDocumentSemanticTokens(document, cancellationToken);
        const data = tokens && tokens.data ? Array.from(tokens.data) : [];
        const answers = [];
        let line = 0;
        let character = 0;
        for (let i = 0; i + 4 < data.length; i += 5) {
            const deltaLine = data[i];
            line += deltaLine;
            character = deltaLine === 0 ? character + data[i + 1] : data[i + 1];
            const modifiers = [];
            for (let bit = 0; bit < names.length; ++bit) {
                if (data[i + 4] & (1 << bit))
                    modifiers.push(names[bit]);
            }
            answers.push({line, character, length: data[i + 2],
                          type: types[data[i + 3]] || '', modifiers});
        }
        if (answers.length)
            return answers;
    }
    return [];
});

onRequest('folding/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return [];
    const answers = [];
    for (const {selector, provider} of foldingProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        if (typeof provider.provideFoldingRanges !== 'function')
            continue;
        const found = await provider.provideFoldingRanges(document, {}, cancellationToken) || [];
        for (const range of found) {
            if (!range || range.start === undefined || range.end === undefined)
                continue;
            answers.push({start: range.start, end: range.end});
        }
    }
    return answers;
});

onRequest('color/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return [];
    const answers = [];
    for (const {selector, provider} of colorProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        if (typeof provider.provideDocumentColors !== 'function')
            continue;
        const found = await provider.provideDocumentColors(document, cancellationToken) || [];
        for (const item of found) {
            if (!item || !item.range || !item.color)
                continue;
            const color = item.color;
            answers.push({
                line: item.range.start.line,
                red: color.red, green: color.green, blue: color.blue,
                alpha: color.alpha === undefined ? 1 : color.alpha,
            });
        }
    }
    return answers;
});

onRequest('codeLens/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return [];
    const answers = [];
    for (const {selector, provider} of codeLensProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        if (typeof provider.provideCodeLenses !== 'function')
            continue;
        const lenses = await provider.provideCodeLenses(document, cancellationToken) || [];
        for (let lens of lenses) {
            // A lens may arrive without its command, to be filled in only for
            // the ones actually shown.
            if (!lens.command && typeof provider.resolveCodeLens === 'function')
                lens = await provider.resolveCodeLens(lens, cancellationToken) || lens;
            if (!lens.command || !lens.range)
                continue;
            answers.push({
                line: lens.range.start.line,
                title: lens.command.title || '',
                tooltip: lens.command.tooltip || '',
                command: lens.command.command || '',
                arguments: lens.command.arguments || [],
            });
        }
    }
    return answers;
});

onRequest('typeDefinition/provide', async params => {
    const document = documents.get(params.uri);
    if (!document)
        return {locations: []};
    const position = new Position(params.position.line, params.position.character);
    const locations = [];
    for (const {selector, provider} of typeDefinitionProviders) {
        if (!matchDocumentSelector(document, selector))
            continue;
        try {
            const result = await provider.provideTypeDefinition(document, position,
                                                                cancellationToken);
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
            logToStderr('type definition provider error', e);
        }
    }
    return {locations};
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
        } catch (e) {
            logToStderr('definition provider error', e);
        }
    }
    return {locations};
});

// The interesting commands of a tree-view extension hang off the item context
// menu ("view/item/context"), filled by matching each entry's
// "when" against the view and the item's contextValue. Only that subset is
// evaluated here - "view == x", "viewItem == y", "viewItem =~ /re/", and && -
// which is what these contributions actually use.
async function contextValueOf(entry, element) {
    try {
        const item = await entry.provider.getTreeItem(element);
        return (item && item.contextValue) || '';
    } catch (e) {
        logToStderr('getTreeItem for the context menu failed', e);
        return '';
    }
}

// A "when" clause: context keys combined with !, &&, ||,
// parentheses, comparisons and a regular expression match. An unknown key is
// false rather than an error, which is what makes "!myExt.busy" show an entry
// instead of hiding it.
function evaluateWhen(clause, lookup) {
    // A regular expression may hold a "/" inside a character class, which is
    // where "src[/|\\]main" puts one, and a bare word may hold a "+", which is
    // where "maven:profile+checked" puts one. Reading either the short way
    // stops mid-clause and answers no to something that was true.
    const tokens = String(clause).match(
        /=~|==|!=|>=|<=|&&|\|\||[()!<>]|\/(?:\[(?:[^\]\\]|\\.)*\]|[^/\\[]|\\.)*\/[a-z]*|'[^']*'|"[^"]*"|[\w.:$+-]+/g)
        || [];
    let pos = 0;
    const peek = () => tokens[pos];
    const take = () => tokens[pos++];
    const constantOf = token => {
        if (token === undefined)
            return undefined;
        if (/^['"]/.test(token))
            return token.slice(1, -1);
        if (/^-?\d+(\.\d+)?$/.test(token))
            return Number(token);
        if (token === 'true' || token === 'false')
            return token === 'true';
        return token;
    };
    const valueOf = token => {
        const constant = constantOf(token);
        return typeof constant === 'string' && constant === token ? lookup(token) : constant;
    };

    const primary = () => {
        if (peek() === '(') {
            take();
            const value = or();
            if (peek() === ')')
                take();
            return value;
        }
        if (peek() === '!') {
            take();
            return !primary();
        }
        const left = take();
        const operator = peek();
        if (operator === '=~') {
            take();
            const pattern = /^\/(.*)\/([a-z]*)$/.exec(take() || '');
            if (!pattern)
                return false;
            try {
                return new RegExp(pattern[1], pattern[2]).test(String(valueOf(left) ?? ''));
            } catch (error) {
                return false;
            }
        }
        if (['==', '!=', '>', '<', '>=', '<='].includes(operator)) {
            take();
            // The right side is a constant, quoted or not: "viewItem == patch"
            // compares against the word, not against a key of that name.
            const left_ = valueOf(left);
            const right = constantOf(take());
            switch (operator) {
            case '==': return String(left_) === String(right);
            case '!=': return String(left_) !== String(right);
            case '>': return Number(left_) > Number(right);
            case '<': return Number(left_) < Number(right);
            case '>=': return Number(left_) >= Number(right);
            default: return Number(left_) <= Number(right);
            }
        }
        return !!valueOf(left);
    };
    const and = () => {
        let value = primary();
        while (peek() === '&&') {
            take();
            const right = primary(); // parse it either way, then combine
            value = value && right;
        }
        return value;
    };
    const or = () => {
        let value = and();
        while (peek() === '||') {
            take();
            const right = and();
            value = value || right;
        }
        return value;
    };
    return !!or();
}

function whenMatches(when, viewId, contextValue) {
    if (!when)
        return true;
    return evaluateWhen(when, key => {
        if (key === 'view')
            return viewId;
        if (key === 'viewItem')
            return contextValue;
        return contextValueOfKey(key);
    });
}

// kind is "view/item/context" for a row's menu, "view/title" for the buttons
// and overflow menu of the view itself.
onRequest('treeview/menu', async params => {
    const kind = params.kind || 'view/item/context';
    const entry = treeDataProviders.get(params.viewId);
    const element = entry && params.id ? entry.elements.get(params.id) : undefined;
    const contextValue = element ? await contextValueOf(entry, element) : '';
    const items = [];
    const seen = new Set(); // a manifest may list the same command twice
    for (const registered of registeredExtensions.values()) {
        const contributes = (registered.packageJSON || {}).contributes || {};
        const commands = new Map();
        for (const command of contributes.commands || [])
            commands.set(command.command, command);
        const submenuTitles = new Map();
        for (const submenu of contributes.submenus || [])
            submenuTitles.set(submenu.id, submenu.label || submenu.id);

        // An entry names a command or a submenu whose own entries are listed
        // under its id.
        const collect = (entries, into, depth) => {
            for (const menu of entries || []) {
                if (!whenMatches(menu.when, params.viewId, contextValue))
                    continue;
                if (menu.submenu) {
                    if (depth > 3)
                        continue;
                    const nested = [];
                    collect((contributes.menus || {})[menu.submenu], nested, depth + 1);
                    if (nested.length) {
                        into.push({submenu: submenuTitles.get(menu.submenu) || menu.submenu,
                                   items: nested, group: menu.group || ''});
                    }
                    continue;
                }
                if (seen.has(menu.command))
                    continue;
                seen.add(menu.command);
                const command = commands.get(menu.command) || {};
                into.push({command: menu.command,
                           title: command.title || menu.command,
                           icon: typeof command.icon === 'string' ? command.icon : '',
                           group: menu.group || ''});
            }
        };
        collect((contributes.menus || {})[kind], items, 0);
    }
    return {items};
});

// A context menu command is handed the element itself, and it
// never leaves the host - the main side only knows the node id.
onRequest('treeview/executeItemCommand', async params => {
    const entry = treeDataProviders.get(params.viewId);
    const element = entry ? entry.elements.get(params.id) : undefined;
    return vscode.commands.executeCommand(params.command, element);
});

// Which rows are picked in the sidebar. An extension that watches its own view
// hears it here; one that only registered a data provider has nothing to tell.
onRequest('treeview/selectionChanged', params => {
    const entry = treeDataProviders.get(params.viewId);
    if (!entry || !entry.view)
        return;
    const selection = (params.ids || [])
        .map(id => entry.elements.get(id))
        .filter(element => element !== undefined);
    entry.view.selection = selection;
    entry.view.onDidChangeSelection.fire({selection});
});

onRequest('treeview/getChildren', async params => {
    return {nodes: await treeChildren(params.viewId, params.id)};
});

// Webview bridge (Qt Creator -> host).
onRequest('webview/onMessage', params => {
    const entry = webviews.get(params.id);
    if (entry)
        entry.onMessage.fire(params.message);
});

onRequest('webview/onDidDispose', params => {
    const entry = webviews.get(params.id);
    if (!entry)
        return;
    entry.onDispose.fire();
    webviews.delete(params.id);
});

notify('host/ready', {pid: process.pid, node: process.version});
