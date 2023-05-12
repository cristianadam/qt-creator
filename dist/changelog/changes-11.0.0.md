Qt Creator 11
=============

Qt Creator version 11 contains bug fixes and new features.

The most important changes are listed in this document. For a complete list of
changes, see the Git log for the Qt Creator sources that you can check out from
the public Git repository. For example:

    git clone git://code.qt.io/qt-creator/qt-creator.git
    git log --cherry-pick --pretty=oneline origin/10.0..v11.0.0

General
-------

* Added a `Terminal` view (QTCREATORBUG-8511)
* Improved the `Issues` view (QTCREATORBUG-26128, QTCREATORBUG-27006,
  QTCREATORBUG-27506)
* Locator
    * Added the creation of directories to the `Files in File System` filter

Editing
-------

* Improved the performance of the multi-cursor support

### C++

* Improved the style of forward declarations in the outline (QTCREATORBUG-312)
* Fixed that locator showed both the declaration and the definition of symbols
  (QTCREATORBUG-13894)
* Fixed the handling of C++20 keywords and concepts

### Language Server Protocol

* Added experimental support for GitHub Copilot
  ([GitHub documentation](https://github.com/features/copilot))

### QML

### Python

### Markdown

* Added a Markdown editor with preview (QTCREATORBUG-27883)

Projects
--------

### CMake

### Qbs

### Python

* Added an option for the interpreter to the wizards

### vcpkg

* Added experimental support for `vcpkg`
  ([vcpgk documentation](https://vcpkg.io/en/))
* Added an option for the `vcpkg` installation location
* Added a search dialog for packages
* Added a wizard and an editor for `vcpkg.json` files

Debugging
---------

### C++

* Added an option for the default number of array elements to show
  (Preferences > Debugger > Locals & Expressions > Default array size)

### Python

Analyzer
--------

### Clang

Version Control Systems
-----------------------

### Git

Test Integration
----------------

Platforms
---------

### macOS

### Android

### Boot to Qt

### Docker

### QNX

* Added `slog2info` as a requirement for devices

Credits for these changes go to:
--------------------------------
