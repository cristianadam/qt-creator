Qt Creator 13.0.2
=================

Qt Creator version 13.0.2 contains bug fixes.

The most important changes are listed in this document. For a complete list of
changes, see the Git log for the Qt Creator sources that you can check out from
the public Git repository. For example:

    git clone git://code.qt.io/qt-creator/qt-creator.git
    git log --cherry-pick --pretty=oneline v13.0.1..v13.0.2

General
-------

* Fixed that the `-client` option could start a new Qt Creator instance instead
  of using a running one (which affects for example version control operations)
  (QTCREATORBUG-30624)

Help
----

Editing
-------

* Fixed that closing files with the tool button didn't add an entry to the
  navigation history

### C++

### QML

### Python

### Language Server Protocol

### Widget Designer

* Fixed that `Use Qt module name in #include-directive` used Qt 4 module names
  (QTCREATORBUG-30751)


### Copilot

### Compiler Explorer

### TODO

### Markdown

### Images

### Models

### Binary Files

Projects
--------

### CMake

### qmake

### Qbs

### Python

### vcpkg

### Qt Safe Renderer

Debugging
---------

### C++

### QML

### Debug Adapter Protocol

Analyzer
--------

### Clang

### Axivion

### CTF Visualizer

Terminal
--------

* Fixed the handling of environment variables with an equal sign `=` in the
  value
  (QTCREATORBUG-30844)

Version Control Systems
-----------------------

### Git

* Fixed a crash in `Instant Blame` when reloading externally modified files
  (QTCREATORBUG-30824)

### CVS

Test Integration
----------------

### Qt Test

### Catch2

### CTest

Platforms
---------

### Windows

* Fixed `Add build library search path to PATH` for MinGW-based builds
  (QTCREATORBUG-30556)

### Linux

### macOS

### Android

* Fixed a crash when re-connecting devices
  (QTCREATORBUG-30645,
   QTCREATORBUG-30770)

### iOS

### Remote Linux

* Fixed passing more than one argument to `rsync`
  (QTCREATORBUG-30795)

### Docker

### Boot to Qt

### MCU

### QNX

Credits for these changes go to:
--------------------------------
