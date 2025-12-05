Qt Creator 19
=============

Qt Creator version 19 contains bug fixes and new features.
It is a free upgrade for all users.

The most important changes are listed in this document. For a complete list of
changes, see the Git log for the Qt Creator sources that you can check out from
the public Git repository or view online at

<https://code.qt.io/cgit/qt-creator/qt-creator.git/log/?id=18.0..v19.0.0>

New plugins
-----------

General
-------

* Added the root folders of connected devices to the `File System` view
  (QTCREATORBUG-33577)
* Added the option to explicitly remove categories for external tools
  (QTCREATORBUG-33316)

Help
----

Editing
-------

### C++

* Added a quick fix for adding a class or struct definition from a forward
  declaration
  (QTCREATORBUG-19929)
* Fixed issues with parameter packs when refactoring
  (QTCREATORBUG-32597)
* Fixed that `Add Definition` was not available for `friend` functions
  (QTCREATORBUG-31048)
* Fixed indenting with the `TAB` key after braced initializer
  (QTCREATORBUG-31759)

### QML

* Fixed a wrong warning `Duplicate Id. (M15)`
  (QTCREATORBUG-32418)
* Fixed that `Split Initializer` did not remove unneeded semicolons
  (QTCREATORBUG-16207)
* qmlls
    * Added the option `Enable qmlls's CMake integration`

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

* Fixed the handling of the implicit initial state
  (QTCREATORBUG-32603)

### FakeVim

### GLSL

### Binary Files

Projects
--------

* Improved performance when opening projects (finding extra compilers for CMake
  projects)
* Added the option to run applications as a different user
  (QTCREATORBUG-33655)
* Added the option to search in generated files with the project related
  `Advanced Find` filters
  (QTCREATORBUG-33579)

### CMake

* vcpkg

### qmake

### Qbs

### Python

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

### QML Profiler

* Added the option to resize the category column in the timeline view
  (QTCREATORBUG-33045)

### Axivion

### Coco

* Fixed the recognition of Coco installations on macOS
  (QTCREATORBUG-33476)

### CTF Visualizer

### Valgrind

### Perf

### Cppcheck

Terminal
--------

Version Control Systems
-----------------------

### Git

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

* Fixed the insert location of the `target_properties` call when creating
  template files
  (QTCREATORBUG-33360)

### iOS

### Remote Linux

* Added support for `Run as root`

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
