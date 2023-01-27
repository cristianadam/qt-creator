Qt Creator 10
=============

Qt Creator version 10 contains bug fixes and new features.

The most important changes are listed in this document. For a complete list of
changes, see the Git log for the Qt Creator sources that you can check out from
the public Git repository. For example:

    git clone git://code.qt.io/qt-creator/qt-creator.git
    git log --cherry-pick --pretty=oneline origin/9.0..v10.0.0

General
-------

Help
----

Editing
-------

* Fixed editor scrolling when pressing backspace (QTCREATORBUG-28316)

### C++

* Added renaming of includes when renaming `.ui` files (QTCREATORBUG-14259)
* Built-in
  * Fixed handling of `= default` (QTCREATORBUG-28102)

### Language Server Protocol

### QML

* Added experimental support for QML language server

### Image Viewer

### Diff Viewer

Projects
--------

### CMake

### Qbs

### Qmake

### Python

Debugging
---------

### C++

### QML

Analyzer
--------

### Clang

### Perf

Version Control Systems
-----------------------

* Removed settings for prompting to submit (QTCREATORBUG-22233)
* Added links to file names in diff output (QTCREATORBUG-27309)
* Fixed blame on symbolic links (QTCREATORBUG-20792)
* Fixed saving of files before applying action on chunks (QTCREATORBUG-22506)

### Git

* Improved tracking of external changes (QTCREATORBUG-21089)

Test Integration
----------------

Platforms
---------

### Windows

### macOS

### Android

### iOS

### Remote Linux

### Boot to Qt

### Docker

Credits for these changes go to:
--------------------------------
