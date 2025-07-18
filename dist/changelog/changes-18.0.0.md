Qt Creator 18
=============

Qt Creator version 18 contains bug fixes and new features.
It is a free upgrade for commercial license holders.

The most important changes are listed in this document. For a complete list of
changes, see the Git log for the Qt Creator sources that you can check out from
the public Git repository or view online at

<https://code.qt.io/cgit/qt-creator/qt-creator.git/log/?id=17.0..v18.0.0>

New plugins
-----------

General
-------

* Added the option `Prefer banner style info bars over pop-ups`
  TODO: Do we need this? What did now really become a pop-up?

Help
----

Editing
-------

### C++

* Added automatic insertion of the closing part of a raw string literal prefix
  (QTCREATORBUG-31901)
* Fixed that trailing white space was removed from raw string literals
  (QTCREATORBUG-30003)

### QML

* Added the option to install Qt Design Studio via the Qt Online Installer
  (QTCREATORBUG-30787)

### Python

### Language Server Protocol

### Widget Designer

### Copilot

### Compiler Explorer

### TODO

### Markdown

### Images

### Models

### SCXML

* Fixed the positioning of the transition arrow
  (QTCREATORBUG-32654)

### FakeVim

### Binary Files

Projects
--------

* Changed the `Build` and `Run` subitems to tabs in `Projects` mode
* Changed the `Current Project` advanced search to `Single Project` with
  an explicit choice of the project to search
  (QTCREATORBUG-29790)
* Removed the `Code Snippet` wizard, use `Plain C++` instead
* Added tool button `Create Issues From External Build Output` to `Issues` view
  (QTCREATORBUG-30776)
* Added the `Default working directory` setting for run configurations
* Added keyboard shortcuts for editing the active build and run configurations
  (QTCREATORBUG-27887)
* Added the option to add a file to a project directly from the
  `This file is not part of any project` warning
  (QTCREATORBUG-25834)
* Added a Qt Interface Framework project wizard
  (QTBUG-99070)

### CMake

* vcpkg

### qmake

* Fixed opening remote projects
  TODO: what state is that exactly in now?

### Qbs

### Python

* Removed PySide2 from the project wizard options
  (QTCREATORBUG-33030)

### Workspace

### Autotools

### Meson

### Qt Safe Renderer

Debugging
---------

### C++

### QML

### Debug Adapter Protocol

Analyzer
--------

### Clang

* Added Clang-Tidy and Clazy issues from the current document to the `Issues`
  view
  (QTCREATORBUG-29789)
* Improved the performance of loading diagnostics from a file
* Fixed freezes when applying multiple fix-its
  (QTCREATORBUG-25394)

### QML Profiler

### Axivion

### Coco

### CTF Visualizer

### Valgrind

### Perf

### Cppcheck

Terminal
--------

Version Control Systems
-----------------------

### Git

* Added `Git > Local Repository > Patch > Apply from Clipboard`
* Added `Git > Local Repository > Patch > Create from Commits`
* Added `Recover File`, `Revert All Changes to File`, and
  `Revert Unstaged Changes to File` to the context menu on files in the commit
  editor
* Added an error indicator and error messages to the `Add Branch` dialog
* Added `Diff & Cancel` to the `Checkout Branch` dialog
* Improved performance of file modification status updates
  (QTCREATORBUG-32002)

### CVS

Test Integration
----------------

### Qt Test

### Boost

### Catch2

### GoogleTest

### CTest

Platforms
---------

### Windows

### Linux

### macOS

### Android

### iOS

### Remote Linux

### Docker

* Added the option `Mount Command Bridge` to the docker device configuration
  (QTCREATORBUG-33006)

### Boot to Qt

### MCU

### Qt Application Manager

### QNX

### Bare Metal

### WebAssembly

### VxWorks

Credits for these changes go to:
--------------------------------
