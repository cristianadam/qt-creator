# VS Code extension infrastructure (prototype)

This plugin is the seed for running VS Code extensions inside Qt Creator.

## What a VS Code extension is

An extension is a directory with a `package.json` manifest plus, usually, a
JavaScript `main` entry point. The manifest declares *contribution points*
(languages, grammars, commands, debuggers, configuration, ...) and
*activation events*. When an activation event fires, VS Code loads `main`
into a Node.js *extension host* process and calls its `activate()` function.
The extension code talks to the editor through the `vscode` module API.

Extensions fall into four classes of increasing difficulty:

- **A. Declarative** - themes, snippets, TextMate grammars, language configs,
  keybindings. Only the manifest matters; no JS runs.
- **B. Server-backed** - most language and debug extensions. The JS is a thin
  launcher; the real work is an LSP/DAP server spoken over stdio. Qt Creator
  already has full LSP (`LanguageClient`) and DAP (`debugger/dap*`) clients.
- **C. API-driven** - commands, tree views, status bar items, quick pick,
  decorations, code actions implemented in JS against the `vscode` API. Needs
  a Node extension host plus a C++ implementation of the `vscode` API surface,
  bridged over JSON-RPC.
- **D. Webview** - custom HTML/JS panels. Class C plus an HTML/JS renderer
  (QWebEngine).

## Target architecture (Theia-style)

The proven way to run VS Code extensions unmodified (Eclipse Theia, Gitpod,
code-server) is an *extension host*: a Node process that loads the extension
and provides the `vscode` module as a set of JSON-RPC stubs. Each `vscode`
call is reflected to the "main side" (here: Qt Creator C++), which maps it
onto `EditorManager`, `IDocument`, `ActionManager`, etc. Qt Creator already
owns most of the transport plumbing this needs:

- `languageserverprotocol` lib: generic JSON-RPC (`JsonRpcMessage`),
  `StdIOClientInterface`, request/response/notification machinery.
- `LanguageClient` + `debugger/dap*`: ready consumers for class B.
- `acp`/`acpclient`: a second JSON-RPC-over-stdio subsystem (transport,
  permission handler, filesystem handler, inspector) - a structural template
  for the host bridge.
- `extensionmanager`: marketplace-style browse/install UI, a natural home for
  a `.vsix` installer and an Open VSX browser.
- `lua`: precedent for a non-C++ extension runtime embedded in Creator.

## Known gaps

- No bundled Node runtime (only a configurable path today, as in `copilot`).
- No `vscode` API implementation.
- Highlighting is KSyntaxHighlighting (Kate XML), *not* TextMate; VS Code
  grammars need a TextMate engine or conversion.
- Licensing: target **Open VSX**, not the Microsoft marketplace.

## Phasing

- **Phase 0 (this prototype):** manifest model + registry that scans an
  extensions directory, and a `LanguageClient` subclass that can surface an
  extension's LSP server into Creator. Proves the manifest -> LSP path.
- **Phase 1:** Node extension host + core `vscode` API (`commands`, `window`,
  `workspace`, `languages`) over JSON-RPC. Also `.vsix` install + Open VSX.
- **Phase 2:** webviews via QWebEngine.
- **Phase 3:** TextMate grammar engine, settings/marketplace lifecycle.

## Guinea pig: qt-labs/vscodeext (Qt extension for VS Code)

The chosen target is the Qt extension monorepo `qt-labs/vscodeext` (PR #535
adds a "Qt Bridge for C#" extension; the `qt-qml` extension is the most
relevant piece). It exercises every class:

- **B (LSP):** `qt-qml` runs `qmlls`. But the server command (qmlls path +
  args) is computed in the extension's TypeScript `activate()` and handed to
  `vscode-languageclient`; it is not a declared standalone server. So even
  this "easy" path ultimately needs the host to observe the `ServerOptions`.
  The Phase 0 stopgap (`assumeMainIsStdioServer`) does *not* cover it.
- **C (commands/wizards):** "Qt: ..." commands, project/kit discovery,
  templates - pure `vscode` API, needs the host.
- **D (webview):** the QML live preview panel.

So the guinea pig is what forces Phase 1. Phase 0 here proves discovery and
the LSP wiring seam against it; "make it work" end to end is the Phase 1
milestone.

## What is real in this prototype

- `VscodeManifest` parses a real `package.json` (identity, `main`,
  `activationEvents`, `contributes.languages/commands/debuggers/grammars`).
- `ExtensionRegistry` discovers installed extensions under the configured
  directory (default `~/.qtcreator-vscode-extensions`, mirroring VS Code's
  `~/.vscode/extensions` layout).
- `AlienClient` reuses `LanguageClient::Client` to run a resolved
  stdio server and wire it to matching documents.
- `ExtensionHost` + `host/host.js` are a working first slice of the Node
  extension host: a shared Node process loads an extension, `require('vscode')`
  resolves to a shim, and `activate()` runs. The `vscode` surface implemented
  so far is `commands.registerCommand`/`executeCommand`, `window.show*Message`,
  and `window.createOutputChannel`; those calls reflect back over
  newline-delimited JSON-RPC (`HostConnection`) onto Qt Creator (messages go to
  the General Messages pane, registered commands become invokable). A bundled
  test extension (`host/testextension`) plus a plugin test exercise the whole
  round trip.

## vscode API coverage (host)

Implemented: `commands.registerCommand`, `commands.executeCommand`,
`window.showInformationMessage/showWarningMessage/showErrorMessage`,
`window.createOutputChannel`, minimal `workspace.getConfiguration` and `Uri`,
and an `ExtensionContext` with `subscriptions`.

Document sync (Qt Creator -> host): `workspace.textDocuments`,
`workspace.onDidOpenTextDocument`/`onDidChangeTextDocument`/`onDidCloseText`
`Document`, `window.activeTextEditor` and `onDidChangeActiveTextEditor`. The
`ExtensionHost` mirrors Creator's open editors into the host's `vscode.workspace`
so an in-host extension (and the language client it runs) sees the documents,
tagged with a `languageId` derived from the activated extensions' contributed
languages. Full-text sync for now (each edit sends the whole document).

Diagnostics (host -> Qt Creator): `languages.createDiagnosticCollection` plus
the `Diagnostic`/`Range`/`Position`/`DiagnosticSeverity` types. When an in-host
extension sets diagnostics, the host sends `diagnostics/publish` and the
`ExtensionHost` renders them as `TextEditor::TextMark`s (colored by severity,
with the message as tooltip and line annotation) on the matching document.

Completion (Qt Creator -> host): `languages.registerCompletionItemProvider`
plus `CompletionItem`/`CompletionItemKind`/`SnippetString`/`MarkdownString`.
When a provider registers for a language, `ExtensionHost` attaches an
`AlienCompletionAssistProvider` to matching documents. On a completion request
its async processor sends `completion/provide`; the host calls the in-host
providers whose documentSelector matches and returns the items, which become a
`GenericProposal` in Creator's completion popup.

Hover + go-to-definition (Qt Creator -> host): `languages.registerHoverProvider`
and `registerDefinitionProvider` plus the `Hover` type. When these register,
`ExtensionHost` attaches an `AlienHoverHandler` to matching editors and enables
Follow Symbol, wiring `TextEditorWidget::requestLinkAt`. Hovering sends
`hover/provide` (shown as a tooltip); following a symbol sends
`definition/provide` and jumps to the returned `Utils::Link`. Other
`languages.register*Provider` calls are still accepted as no-ops.

## Running unmodified bundled extensions (the chosen direction)

The `qt-qml` guinea pig is esbuild-bundled, so it never `require()`s
vscode-languageclient at runtime and the interception below cannot catch it.
Rather than modify the extension, the plan is the Theia-style host: run the
extension (and its inlined language client) in Node and implement the `vscode`
API it uses, bridging editor state to Creator. Document sync, diagnostics,
completion and hover/definition above are the first slices of that. Remaining
slices: commands and UI (quick pick, tree views), then webviews.

Remote-readiness: the host's runtime directory is created on the same device as
`node` (via `FilePath::tmpDir()`), and the transport is a device-transparent
`Utils::Process`. Launching the host on a remote device (like VS Code's remote
extension host, needed so the extension's raw `fs`/`child_process` see the
project's files) is then mostly a matter of passing a device `node` path.

## vscode-languageclient interception

Language extensions start their server through `vscode-languageclient` rather
than talking LSP themselves. The host intercepts `require('vscode-languageclient')`
(and `.../node`) with a shim `LanguageClient`. On `.start()` it resolves the
`ServerOptions` to a concrete command (the `Executable` and `NodeModule` shapes;
function-valued options are not supported yet) and the `clientOptions.document`
`Selector` to file patterns - mapping language ids through the activating
extension's contributed languages - and sends `languageclient/start` to Qt
Creator. The C++ side then runs the server with a real `LanguageClient::Client`
(`AlienClient`), so document sync, diagnostics, completion, etc. all go through
Creator's mature LSP stack instead of a second one in Node.

A bundled LSP test extension (`host/lsptestextension`) starts a minimal mock
server (`host/mockserver/server.js`) this way; a plugin test asserts the
resulting client reaches `reachable()` (i.e. completes `initialize`).

Caveat: interception fires only when the extension `require()`s
vscode-languageclient at runtime. Extensions bundled with esbuild/webpack
(including the `qt-qml` guinea pig) inline it, so this hook does not catch them
yet; that needs either building them with vscode-languageclient marked external,
or implementing the vscode API surface vscode-languageclient consumes so it can
run in the host (the Theia approach).

Next, toward the guinea pig: `workspace` documents/fs/events, `languages`
(diagnostics, code actions), text editor edits/decorations, and handling the
bundled-extension case above. Then webviews (Phase 2).
