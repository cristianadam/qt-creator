Experimental infrastructure for running VSIX extensions inside Qt Creator.
A VSIX extension is written against the extension API of Visual Studio Code,
which several editors implement independently - Eclipse Theia and VSCodium
among them.

Extensions are read from a configured extensions directory (each in its own
subfolder with a `package.json`, as installing a `.vsix` leaves them) and run
in a Node.js extension host, which surfaces their commands, views, language
servers and debug adapters in Qt Creator.

Visual Studio Code is a trademark of Microsoft Corporation. This plugin is not
affiliated with, sponsored by or endorsed by Microsoft, and extensions
licensed for use only with Microsoft products are outside its scope.
