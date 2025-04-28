Qt Creator 17
=============

Qt Creator version 17 contains bug fixes and new features.
It is a free upgrade for commercial license holders.

The most important changes are listed in this document. For a complete list of
changes, see the Git log for the Qt Creator sources that you can check out from
the public Git repository or view online at

<https://code.qt.io/cgit/qt-creator/qt-creator.git/log/?id=16.0..v17.0.0>

New plugins
-----------

TODO: what about the "learning" plugin?

General
-------

* Made the "2024" theme variants the default
  (QTCREATORBUG-32400)
* Refreshed icons
* Improved support for extracting archives
  (QTAIASSIST-169)
* Added tab completion to the Locator
* Extensions
    * Added the dependencies and supported platforms of not installed plugins to
      its details
    * Added support for dropping plugin archives onto `Extensions` mode

Help
----

Editing
-------

### C++

### QML

### Python

### Language Server Protocol

* Fixed that the `detail` field of `Document Symbols` was ignored
  (QTCREATORBUG-31766)

### Widget Designer

### Copilot

### Compiler Explorer

### TODO

### Markdown

### Images

### Models

### SCXML

* Improved adaptation to Qt Creator theme
  (QTCREATORBUG-29701)

### FakeVim

### Binary Files

Projects
--------

* Removed the explicit Haskell project support (use a Workspace project instead)
* Changed run configurations to be configured per build configuration
  (QTCREATORBUG-20986,
   QTCREATORBUG-32380)
* Changed the project configuration page to only select `Debug` configurations
  by default
* Improved the behavior of `Next Item` and `Previous Item` in the `Issues` view
  (QTCREATORBUG-32503)
* Added the option to use custom output parsers for all build or run
  configurations by default
  (QTCREATORBUG-32342)
* Added to option to select `qtpaths` instead of `qmake` when registering
  Qt versions
  (QTCREATORBUG-32213)

### CMake

### qmake

* Fixed that `QMAKE_PROJECT_NAME` was not used for run configuration names
  (QTCREATORBUG-32465)

### Qbs

### Python

* Added support for `pyproject.toml` projects
  (QTCREATORBUG-22492,
   PYSIDE-2714)

### Workspace

### Autotools

### Meson

### vcpkg

### Qt Safe Renderer

Debugging
---------

### C++

* LLDB
    * Fixed the pretty printer for `QMap` on ARM Macs
      (QTCREATORBUG-32309)
    * Fixed the pretty printer for `QImage`
      (QTCREATORBUG-32390)

### QML

### Debug Adapter Protocol

Analyzer
--------

### Clang

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

* Added `Log Directory` to directories in the `File System` view
  (QTCREATORBUG-32540)

### Git

* Added the `%{Git:Config:<key>}` Qt Creator variable for Git configuration
  values
* Added actions for staged changes
  (QTCREATORBUG-32361)
* Added `Revert` to the actions in the `Instant Blame` tooltip
* Added the option to create annotated tags to the `Create Branch` dialog
* Added a `Diff & Cancel` option to the `Uncommitted Changes Found` dialog
  (QTCREATORBUG-25795)
* Added a `.gitignore` file when creating a repository in an existing directory
  (QTCREATORBUG-29776)

### CVS

Test Integration
----------------

### Qt Test

### Boost

### Catch2

### CTest

Platforms
---------

### Windows

### Linux

### macOS

### Android

* Dropped support for GDB (LLDB is available for Qt 5.15.9 and later and Qt 6.2
  and later)
* Fixed that Valgrind actions were enabled
  (QTCREATORBUG-32336)

### iOS

### Remote Linux

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
