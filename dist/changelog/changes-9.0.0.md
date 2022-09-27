Qt Creator 9
============

Qt Creator version 9 contains bug fixes and new features.

The most important changes are listed in this document. For a complete list of
changes, see the Git log for the Qt Creator sources that you can check out from
the public Git repository. For example:

    git clone git://code.qt.io/qt-creator/qt-creator.git
    git log --cherry-pick --pretty=oneline origin/8.0..v9.0.0

General
-------

* Added change log browser `Help > Change Log` (`Qt Creator > Change Log` on
  macOS)
* Added option for showing locator as a centered popup
* Added non-menu actions to locator `t` filter

Editing
-------

* Added option for visualizing indentation (QTCREATORBUG-22756)
* Added option for `Tint whole margin area`
* Added option for line spacing (QTCREATORBUG-13727)
* Added `Create Cursors at Selected Line Ends`
* Improved UI for multiple markers on the same line (QTCREATORBUG-27415)
* Fixed performance issue with large selections
* Fixed `Rewrap Paragraph` for Doxygen comments (QTCREATORBUG-9739)
* Fixed MIME type matching for generic highlighting with MIME type aliases

### C++

* Moved code style editor from dialog directly into the preferences page
* Added `Show Preprocessed Source`
* Added `Follow Symbol` for QRC files in string literals (QTCREATORBUG-28087)
* Added option for returning only non-value types by const reference
  (QTCREATORBUG-25790)
* Fixed that selection was not considered for refactoring actions
  (QTCREATORBUG-27886)
* Clangd
    * Added option for using single Clangd instance for the whole session
      (QTCREATORBUG-26526)
    * Added option for maximum number of completion results (default 100)
      (QTCREATORBUG-27152)
    * Added option for document specific preprocessor directives
      (QTCREATORBUG-20423)
    * Fixed semantic highlighting for `__func__`
    * Fixed double items in outline after switching Clangd off and on
      (QTCREATORBUG-27594)
* Built-in
    * Added support for structured bindings (QTCREATORBUG-27975)
* ClangFormat
    * Moved settings back to top level preferences page

### Language Server Protocol

* Improved performance for large documents
* Fixed that server was not restarted after 5 times, even if a long time passed
  after the last time

### Binaries

* Added support for character encoding

### Image Viewer

* Made `Fit to Screen` sticky and added option for the default
  (QTCREATORBUG-27816)

Projects
--------

* Added option for hiding build system output with `Show Right Sidebar`
  (QTCREATORBUG-26069)
* Fixed that opening terminal from build environment settings did not change
  directory to build directory

### CMake

* Moved settings from `Kits` and `Build & Run` into their own `CMake` category
* Turned `Package manager auto setup` off by default
* Added support for CMake configure presets (QTCREATORBUG-24555)
* Added option for changing environment for configure step
* Added option for hiding subfolders in source groups (QTCREATORBUG-27432)
* Fixed that `PATH` environment variable was not completely set up during first
  CMake run

### Qbs

* Fixed that `qbs.sysroot` was not considered

Debugging
---------

* Added warning for missing QML debugging functionality for mobile and embedded
  devices
* Fixed display of strings with characters more than 2 bytes long

Version Control Systems
-----------------------

### Git

* Added support for user-configured comment character (QTCREATORBUG-28042)

Test Integration
----------------

* Added support for Squish
* Catch 2
    * Fixed handling of exceptions (QTCREATORBUG-28131)

Platforms
---------

### Windows

* Improved detection of MinGW and LLVM ABI (QTCREATORBUG-26247)
* Fixed wrong debugger when importing build (QTCREATORBUG-27758)
* Fixed issues when drives are mapped (QTCREATORBUG-27869, QTCREATORBUG-28031)
* Fixed that output could be missing for Qt based external tools
  (QTCREATORBUG-27828)

### macOS

* Added auto-detection of `ccache` compilers from Homebrew (QTCREATORBUG-27792)
* Fixed that theme partially switched between dark and light when system theme
  changed during runtime (QTCREATORBUG-28066)

### iOS

* Fixed determination of Qt version when debugging

### Remote Linux

* Added option for SSH port to wizard
* Added fallback for devices without `base64`

### Docker

* Added option for `docker` command
* Added detection of Python
* Improved device wizard

Credits for these changes go to:
--------------------------------
Aaron Barany  
Alessandro Portale  
Alexander Akulich  
Alexander Drozdov  
André Pönitz  
Artem Sokolovskii  
Assam Boudjelthia  
Björn Schäpers  
Christiaan Janssen  
Christian Kandeler  
Christian Stenger  
Cristian Adam  
Daniele Bortolotti  
David Schulz  
Eike Ziller  
Fawzi Mohamed  
Florian Koch  
Henning Gruendl  
Jaroslaw Kobus  
Lucie Gérard  
Marco Bubke  
Marcus Tillmanns  
Miikka Heikkinen  
Orgad Shaneh  
Piotr Mućko  
Robert Löhning  
Sergey Levin  
Sivert Krøvel  
Tasuku Suzuki  
Thiago Macieira  
Thomas Hartmann  
Tim Jenssen  
Ulf Hermann  
Yasser Grimes  
