Qt Creator 11.0.1
=================

Qt Creator version 11.0.1 contains bug fixes.

The most important changes are listed in this document. For a complete list of
changes, see the Git log for the Qt Creator sources that you can check out from
the public Git repository. For example:

    git clone git://code.qt.io/qt-creator/qt-creator.git
    git log --cherry-pick --pretty=oneline origin/v11.0.0..v11.0.1

General
-------

* Fixed writing configuration files with `sdktool`
* Fixed exporting keyboard shortcut schemes
  (QTCREATORBUG-29431)

Editing
-------

### Beautifier

* Fixed setting a customized Clang Format style
  (QTCREATORBUG-28525)

Projects
--------

* Fixed a crash when manually re-detecting toolchains
  (QTCREATORBUG-29430)
* Fixed the pasting of large texts in integrated terminal
* Incredibuild
    * Fixed missing UI in the build steps

Debugging
---------

* Fixed the button state in the dialog for loading core files
* Fixed debugging with debuggers that still use Python 2.7
  (QTCREATORBUG-29440)
* GDB
    * Fixed `Use common locations for debug information`

Version Control Systems
-----------------------

### Git

* Fixed a crash when tools are not found in `PATH`

Credits for these changes go to:
--------------------------------
Aleksei German  
André Pönitz  
Christian Kandeler  
Christian Stenger  
Cristian Adam  
Eike Ziller  
Leena Miettinen  
Marcus Tillmanns  
Robert Löhning  
