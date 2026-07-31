Experimental infrastructure for running Visual Studio Code extensions inside
Qt Creator.

This prototype reads VS Code extension manifests (`package.json`) from a
configured extensions directory and can surface an extension's language server
into Qt Creator's editor through the existing Language Client infrastructure.

The full extension host (running arbitrary extension JavaScript against the
`vscode` API) is not yet implemented; see the plugin README for the phased
plan.
