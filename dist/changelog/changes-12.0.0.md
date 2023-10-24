Qt Creator 12
=============

Qt Creator version 12 contains bug fixes and new features.

The most important changes are listed in this document. For a complete list of
changes, see the Git log for the Qt Creator sources that you can check out from
the public Git repository. For example:

    git clone git://code.qt.io/qt-creator/qt-creator.git
    git log --cherry-pick --pretty=oneline origin/11.0..v12.0.0

What's new?
-----------

* Integrated [Compiler Explorer (https://godbolt.org)](https://godbolt.org)
* CMake debugging and the [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/)
* Screen recording

### Compiler Explorer

Use [Compiler Explorer (https://godbolt.org)](https://godbolt.org) in Qt Creator
and enter example code to explore the capabilities of your compilers and
interpreters.

To enable the CompilerExplorer plugin, select
`Help > About Plugins > Utilities > CompilerExplorer`. Then select
`Restart Now` to restart Qt Creator and load the plugin.

Select `File > New File` and select one of the new `Compiler Explorer` templates
to get started.

Alternatively, you can open a new `Compiler Explorer` editor via
`Tools > Compiler Explorer > Open Compiler Explorer`.

Every language, compiler, and library that is supported at <https://godbolt.org>
is also supported in Qt Creator. You can save your Compiler Explorer session as
a `.qtce` file (JSON-based).

([Documentation](https://doc-snapshots.qt.io/qtcreator-12.0/creator-how-to-explore-compiler-code.html))

### CMake Debugging and the Debug Adapter Protocol

Set breakpoints in a CMake file and select
`Debug > Start Debugging > Start CMake Debugging` to start debugging.

([Documentation](https://doc-snapshots.qt.io/qtcreator-12.0/creator-how-to-debug-cmake-files.html))


### Screen Recording

Use `Tools > Record Screen` to record a part of your screen. Requires an
installation of [FFmpeg](https://ffmpeg.org).

To enable the ScreenRecorder plugin, select
`Help > About Plugins > Utilities > ScreenRecorder`. Then select
`Restart Now` to restart Qt Creator and load the plugin.

([Documentation](https://doc-snapshots.qt.io/qtcreator-12.0/creator-how-to-record-screens.html))

General
-------

* Improved the startup performance significantly on various setups
* Added the `Sort results` check box for configuring the `md` locator filter in
  `Edit > Preferences > Environment > Locator` to keep the sorting from the tool
  used for the file system index locator filter
  ([QTCREATORBUG-27789](https://bugreports.qt.io/browse/QTCREATORBUG-27789))
  ([Documentation](https://doc-snapshots.qt.io/qtcreator-12.0/creator-editor-locator.html#locating-files-from-global-file-system-index))
* Added the `View > Show Menu Bar` option to hide the menu bar on platforms
  without a unified menu bar
  ([QTCREATORBUG-29498](https://bugreports.qt.io/browse/QTCREATORBUG-29498))
  ([Documentation](https://doc-snapshots.qt.io/qtcreator-12.0/creator-how-to-show-and-hide-main-menu.html))
* Fixed an issue with growing session files

Help
----

* Added the `Edit > Preferences > Help > General > Antialias` check box for
  setting the anti-aliasing of text
  ([QTCREATORBUG-12177](https://bugreports.qt.io/browse/QTCREATORBUG-12177))
  ([Documentation](https://doc-snapshots.qt.io/qtcreator-12.0/creator-how-to-get-help.html#change-the-font))

Editing
-------

* Added the count of selected characters to line and column information
  on the `Edit` mode toolbar
  ([QTCREATORBUG-29381](https://bugreports.qt.io/browse/QTCREATORBUG-29381))
  ([Documentation](https://doc-snapshots.qt.io/qtcreator-12.0/creator-coding-navigating.html#navigating-between-open-files-and-symbols))
* Added an indenter, auto-brace and auto-quote for JSON
  ([Documentation](https://doc-snapshots.qt.io/qtcreator-12.0/creator-enclose-code-in-characters.html))
* Fixed that the historical order of open documents was not restored
* Fixed that suggestions were rendered with the wrong tab size
  ([QTCREATORBUG-29483](https://bugreports.qt.io/browse/QTCREATORBUG-29483))

### C++

* Updated to LLVM 17.0.1
* Added `Tools > C++ > Fold All Comment Blocks` and `Unfold All Comment Blocks`
  ([QTCREATORBUG-2449](https://bugreports.qt.io/browse/QTCREATORBUG-2449))
  ([Documentation](https://doc-snapshots.qt.io/qtcreator-12.0/creator-highlighting.html#folding-blocks))
* Added the `Convert Comment to C Style` and `Convert Comment to C++ Style`
  refactoring actions for converting comments between C++-style and
  C-style
  ([QTCREATORBUG-27501](https://bugreports.qt.io/browse/QTCREATORBUG-27501))
  ([Documentation](https://doc-snapshots.qt.io/qtcreator-12.0/creator-editor-quick-fixes.html#refactoring-c-code))
* Added the `Move Function Documentation to Declaration` and
  `Move Function Documentation to Definition` refactoring actions for moving
  documentation between function declaration and definition
  ([QTCREATORBUG-13877](https://bugreports.qt.io/browse/QTCREATORBUG-13877))
* Extended the application of renaming operations to documentation comments
  ([QTCREATORBUG-12051](https://bugreports.qt.io/browse/QTCREATORBUG-12051),
   [QTCREATORBUG-15425](https://bugreports.qt.io/browse/QTCREATORBUG-15425))
* Fixed that code inserted by refactoring actions was not formatted according
  to the Clang Format settings
  ([QTCREATORBUG-10807](https://bugreports.qt.io/browse/QTCREATORBUG-10807),
   [QTCREATORBUG-19158](https://bugreports.qt.io/browse/QTCREATORBUG-19158))
* Fixed that automatically created functions could be added between another
  function and its documentation
  ([QTCREATORBUG-6934](https://bugreports.qt.io/browse/QTCREATORBUG-6934))
* Fixed that the refactoring actions from Clangd were not available in the
  context menu
* Clang Format
    * Fixed the style settings for Clang Format 16 and later
      ([QTCREATORBUG-29434](https://bugreports.qt.io/browse/QTCREATORBUG-29434))

### QML

* Fixed invalid `M325` warnings
  ([QTCREATORBUG-29601](https://bugreports.qt.io/browse/QTCREATORBUG-29601))
* Language Server
    * Fixed the shortcut for applying refactoring actions
      ([QTCREATORBUG-29557](https://bugreports.qt.io/browse/QTCREATORBUG-29557))

### Python

* Fixed duplicate code when renaming
  ([QTCREATORBUG-29389](https://bugreports.qt.io/browse/QTCREATORBUG-29389))

### Language Server Protocol

* Added support for Language servers that request creating, renaming, or deleting
  of files
  ([QTCREATORBUG-29542](https://bugreports.qt.io/browse/QTCREATORBUG-29542))

### Copilot

* Added support for proxies
  ([QTCREATORBUG-29485](https://bugreports.qt.io/browse/QTCREATORBUG-29485))
  ([Documentation](https://doc-snapshots.qt.io/qtcreator-12.0/creator-copilot.html))

### TODO

* Added the `\todo` keyword to the default

### Markdown

* Added buttons and configurable shortcuts for text styles
  ([Documentation](https://doc-snapshots.qt.io/qtcreator-12.0/creator-markdown-editor.html))

### Images

* Fixed that animations could not be restarted
  ([QTCREATORBUG-29606](https://bugreports.qt.io/browse/QTCREATORBUG-29606))
* Fixed that looping animations did not loop
  ([QTCREATORBUG-29606](https://bugreports.qt.io/browse/QTCREATORBUG-29606))

Projects
--------

* Project specific settings
    * Added C++ file naming settings
      ([QTCREATORBUG-22033](https://bugreports.qt.io/browse/QTCREATORBUG-22033))
      ([Documentation](https://doc-snapshots.qt.io/qtcreator-12.0/creator-how-to-set-cpp-file-naming.html))
    * Added documentation comment settings
      ([Documentation](https://doc-snapshots.qt.io/qtcreator-12.0/creator-how-to-document-code.html))
* Added an option for the Doxygen command prefix
  ([QTCREATORBUG-8096](https://bugreports.qt.io/browse/QTCREATORBUG-8096))
* Improved performance of filtering the target setup page
  ([QTCREATORBUG-29494](https://bugreports.qt.io/browse/QTCREATORBUG-29494))
* Fixed that run configurations were removed when the corresponding target
  vanishes (even temporarily)
  ([QTCREATORBUG-23163](https://bugreports.qt.io/browse/QTCREATORBUG-23163),
   [QTCREATORBUG-28273](https://bugreports.qt.io/browse/QTCREATORBUG-28273))
* Fixed issues with recursive symbolic links
  ([QTCREATORBUG-29663](https://bugreports.qt.io/browse/QTCREATORBUG-29663))

### CMake

* Removed support for extra generators
* Added `Follow Symbol Under Cursor` for functions, macros, targets and packages
  ([QTCREATORBUG-25523](https://bugreports.qt.io/browse/QTCREATORBUG-25523),
   [QTCREATORBUG-25524](https://bugreports.qt.io/browse/QTCREATORBUG-25524))
* Added support for `CMAKE_SOURCE_DIR` and similar variables for
  `Jump to File Under Cursor`
  ([QTCREATORBUG-29467](https://bugreports.qt.io/browse/QTCREATORBUG-29467))
* Added code completion for various aspects of CMake (local functions and
  variables, cache variables, `ENV`, targets, packages, variables added by
  `find_package`)
* Added support for `CMAKE_UNITY_BUILD`
  ([QTCREATORBUG-23635](https://bugreports.qt.io/browse/QTCREATORBUG-23635),
   [QTCREATORBUG-26822](https://bugreports.qt.io/browse/QTCREATORBUG-26822),
   [QTCREATORBUG-29080](https://bugreports.qt.io/browse/QTCREATORBUG-29080))
* Added support for `cmake-format` configuration files
  ([QTCREATORBUG-28969](https://bugreports.qt.io/browse/QTCREATORBUG-28969))
* Added help tooltips
  ([QTCREATORBUG-25780](https://bugreports.qt.io/browse/QTCREATORBUG-25780))
* Extended context help for variables, properties and modules

### Python

* Added auto-detection of PySide from the installer
  ([PYSIDE-2153](https://bugreports.qt.io/browse/PYSIDE-2153))
  ([Documentation](https://doc-snapshots.qt.io/qtcreator-12.0/creator-python-development.html#set-up-pyside6))
* Added the option to forward the display for remote Linux
* Fixed PySide wheels installation on macOS

### vcpkg

* Added the generation of code for `CMakeLists.txt`
* Added parsing the dependencies from `vcpkg.json` manifest files
* Improved the addition of dependencies to `vcpkg.json`

### Qt Safe Renderer

* Added a wizard for Qt Safe Renderer 2.1 and later
  ([Documentation](https://doc.qt.io/QtSafeRenderer/qtsr-safety-project.html#using-qt-safe-renderer-project-template-in-qt-creator))

Debugging
---------

### C++

* Added support for remote Linux debugging with LLDB
* Fixed warnings about index cache permissions
  ([QTCREATORBUG-29556](https://bugreports.qt.io/browse/QTCREATORBUG-29556))

Analyzer
--------

### Clang

* Fixed that error messages were not shown
  ([QTCREATORBUG-29298](https://bugreports.qt.io/browse/QTCREATORBUG-29298))

### CTF Visualizer

* Fixed that process and thread IDs could not be strings
* Fixed the computation of nesting levels
* Fixed a crash when zooming with a touch pad

Terminal
--------

* Added mouse support
* Added support for Windows Terminal schemes
* Fixed `Ctrl+C/V` on Windows

Version Control Systems
-----------------------

### Git

* Added option for ignoring whitespace changes and line moves in `Instant Blame`
  ([QTCREATORBUG-29378](https://bugreports.qt.io/browse/QTCREATORBUG-29378))

### CVS

* Disabled by default

Test Integration
----------------

* Added an option for the number of threads used for scanning
  ([QTCREATORBUG-29301](https://bugreports.qt.io/browse/QTCREATORBUG-29301))

Credits for these changes go to:
--------------------------------
Aleksei German  
Alessandro Portale  
Ali Kianian  
Amr Essam  
Andre Hartmann  
André Pönitz  
Andreas Loth  
Artem Sokolovskii  
Brook Cronin  
Burak Hancerli  
Christian Kandeler  
Christian Stenger  
Cristian Adam  
David Schulz  
Dominic Ernst  
Eike Ziller  
Esa Törmänen  
Friedemann Kleint  
Henning Gruendl  
Jaroslaw Kobus  
Johanna Vanhatapio  
Johnny Jazeix  
Jonas Karlsson  
Jussi Witick  
Knud Dollereder  
Leena Miettinen  
Mahmoud Badri  
Marco Bubke  
Marcus Tillmanns  
Mats Honkamaa  
Mehdi Salem  
Miikka Heikkinen  
Olivier De Cannière  
Olivier Delaune  
Orgad Shaneh  
Pranta Dastider  
Robert Löhning  
Sami Shalayel  
Samuel Ghinet  
Semih Yavuz  
Tasuku Suzuki  
Thiago Macieira  
Thomas Hartmann  
Tim Jenssen  
Tim Jenßen  
Tor Arne Vestbø  
Vikas Pachdha  
Xavier Besson  
Yasser Grimes  
