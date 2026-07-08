Qt Creator 21
=============

Qt Creator version 21 contains bug fixes and new features.
It is a free upgrade for all users.

The most important changes are listed in this document. For a complete list of
changes, see the Git log for the Qt Creator sources that you can check out from
the public Git repository or view online at

<https://code.qt.io/cgit/qt-creator/qt-creator.git/log/?id=20.0..v21.0.0>

New plugins
-----------

### Zephyr

Adds support for [Zephyr RTOS](https://www.zephyrproject.org/) and West.

### Plugin 1

Description of the plugin.

([Documentation](<URL>))

General
-------

Added

Changed

* Improved the file dialog for remote paths

Fixed

* Performance issues with the `File System` view
  (QTCREATORBUG-33785)

### Agent Client Protocol (ACP)

Added

* The option to create a new session for a workspace from the history
  (QTCREATORBUG-34619)

### Model Context Protocol

Added

* The option to enable and disable tools in
  `Preferences > AI > Qt Creator MCP Server`
  (QTCREATORBUG-34617)
* The `list_tests`, `reconfigure_cmake`, and `open_project` tools
* The option to provide a line and column to the `open_file` tool
* The option to provide a starting line and ending line to the `file_plain_text`
  tool

Changed

* Improved `list_projects` and `set_active_project`

Fixed

* That modified documents could be overwritten with `set_file_plain_text`

Help
----

Added

Changed

Fixed

* That registered Qt Creator documentation could accumulate over version updates

Editing
-------

Added

* The option to open a project file as a project in Qt Creator

Changed

Fixed

### C++

Added

* The `Wrap in std::as_const()` quick fix

Fixed

* That the `Generate Missing Q_PROPERTY Members` quick fix did not consider the
  `BINDABLE` attribute
  (QTCREATORBUG-34561)

### QML

Fixed

* That qmlls was not enabled for Python projects
  (QTCREATORBUG-34467)

### Python

### Language Server Protocol

Fixed

* That the tool button in the editor could vanish if the language server failed

### Diff Viewer

### Widget Designer

### Copilot

### Compiler Explorer

### TODO

### Markdown

### Images

### Models

### SCXML

### FakeVim

### GLSL

### Binary Files

Projects
--------

Added

* The option to create and open a Workspace project for executable files that
  are opened in an editor
  (QTCREATORBUG-30837)
* The option to filter for run configurations in the target selector
  (QTCREATORBUG-34608)

Changed

Fixed

* The handling of ANSI escape sequences in incomplete lines
  (QTCREATORBUG-33704)
* That switching application output tabs temporarily disabled the search tool
  bar
  (QTCREATORBUG-32444)

### CMake

Changed

* Updated to the CMake parser version 4.2

* vcpkg

### qmake

### Qbs

### Python

### Workspace

### Compilation Database

### Autotools

### Meson

### Qt Safe Renderer

Debugging
---------

Added

* The experimental `Use native combined debugging` option that enables combined
  stack traces when debugging C++ and QML

Changed

Fixed

### C++

### QML

### Debug Adapter Protocol

Analyzer
--------

Added

Changed

Fixed

### Clang

### Profiler

Added

* Trace Viewer
    * Support for the [Common Trace Format version 2](https://diamon.org/ctf/) and
      [version 1.8](https://diamon.org/ctf/v1.8.3/) (in contrast to the already
      supported Chrome Trace Event Format)
      (QTCREATORBUG-29909)
    * That `.qtd`, `.qzt`, and `.ptq` files can be opened in the profiler by opening
      the file (for example on the command line or with `File > Open File`)

### Axivion

### Coco

### Valgrind

### Perf

### Cppcheck

Terminal
--------

Added

Changed

Fixed

Version Control Systems
-----------------------

Added

Changed

Fixed

### Git

Added

* The `Merge`, `Rebase`, and `Stop tracking` actions to the `Branches` view
* The `Tools > Git > Current File > Delete` action

Changed

* Increased the width of command output in the output view
  (QTCREATORBUG-27374)

### CVS

Test Integration
----------------

Added

Changed

Fixed

### Qt Test

### Boost

### Catch2

### GoogleTest

### CTest

Platforms
---------

Added

Changed

Fixed

### Windows

### Linux

### macOS

### Android

### iOS

### Remote Linux

### Development Container

### Docker

### Boot to Qt

### MCU

### Qt Application Manager

### QNX

### Bare Metal

### WebAssembly

### VxWorks

Credits for these changes go to:
--------------------------------
