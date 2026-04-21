Qt Creator 19.0.1
=================

Qt Creator version 19.0.1 contains bug fixes.
It is a free upgrade for all users.

The most important changes are listed in this document. For a complete list of
changes, see the Git log for the Qt Creator sources that you can check out from
the public Git repository or view online at

<https://code.qt.io/cgit/qt-creator/qt-creator.git/log/?id=v19.0.0..v19.0.1>

General
-------

Fixed

* That preferences for newly enabled plugins were only available after restart
  ([QTCREATORBUG-34266](https://bugreports.qt.io/browse/QTCREATORBUG-34266))
* Various issues with marking the `Preferences` as dirty
* A possible crash when opening the `About Qt Creator` dialog multiple times
* That using the keyboard shortcut for `Advanced Find` did not raise the search
  widget
  ([QTCREATORBUG-34306](https://bugreports.qt.io/browse/QTCREATORBUG-34306))
* Model Context Protocol
    * A crash when using the `quit` action
      ([QTCREATORBUG-34256](https://bugreports.qt.io/browse/QTCREATORBUG-34256))

Editing
-------

Fixed

* That the minimap was shown in text views in the preferences which lead to a
  crash
  ([QTCREATORBUG-34228](https://bugreports.qt.io/browse/QTCREATORBUG-34228))

### QML

Fixed

* That issues from the built-in model were still shown when `qmlls` is used
  ([QTCREATORBUG-33861](https://bugreports.qt.io/browse/QTCREATORBUG-33861))

### Diff Viewer

Fixed

* That `Diff Against Current File` was not available for non-text editors like
  the image viewer
  ([QTCREATORBUG-32619](https://bugreports.qt.io/browse/QTCREATORBUG-32619))

Projects
--------

Fixed

* That the build directory was expanded to unexpected paths
  ([QTCREATORBUG-34222](https://bugreports.qt.io/browse/QTCREATORBUG-34222))
* That `%{BuildConfig:Name}` did not return the name verbatim
  ([QTCREATORBUG-34296](https://bugreports.qt.io/browse/QTCREATORBUG-34296))

### CMake

Changed

* The default build directories back to simpler paths
  ([QTCREATORBUG-34217](https://bugreports.qt.io/browse/QTCREATORBUG-34217))

Fixed

* That the `ctest` Locator filter built too much
  ([QTCREATORBUG-34225](https://bugreports.qt.io/browse/QTCREATORBUG-34225))
* Presets
    * A crash when reloading build presets
      ([QTCREATORBUG-34210](https://bugreports.qt.io/browse/QTCREATORBUG-34210))
    * The inheritance of `graphviz` and `trace` values
      ([QTCREATORBUG-34310](https://bugreports.qt.io/browse/QTCREATORBUG-34310))

### Workspace

Fixed

* That debugging was not possible

### Generic Projects

Fixed

* The handling of absolute file paths
  ([QTCREATORBUG-34223](https://bugreports.qt.io/browse/QTCREATORBUG-34223))

Analyzer
--------

### Axivion

Fixed

* The handling of local anonymous dashboards
  ([QTCREATORBUG-34282](https://bugreports.qt.io/browse/QTCREATORBUG-34282))
* Issues when stopping local dashboards
  ([QTCREATORBUG-34290](https://bugreports.qt.io/browse/QTCREATORBUG-34290))
* That `Remove All Finished` did not remove finished single file analyses

### Coco

Fixed

* Issues with initially setting up the Coco build step
  ([QTCREATORBUG-34150](https://bugreports.qt.io/browse/QTCREATORBUG-34150))

### Perf

Fixed

* A crash in the `CPU Usage` preferences

Version Control Systems
-----------------------

Fixed

* The live version control state display after turning the feature off
  ([QTCREATORBUG-34312](https://bugreports.qt.io/browse/QTCREATORBUG-34312))

Test Integration
----------------

### CTest

Fixed

* The parsing of the test duration
  ([QTCREATORBUG-34231](https://bugreports.qt.io/browse/QTCREATORBUG-34231))

Platforms
---------

### Android

Fixed

* A crash when setting a non-Android Qt version for a kit with an Android
  target device

### Remote Linux

Fixed

* That remote toolchains were removed when the device is disconnected
* That detected CMake tools could not be removed

### Development Container

Fixed

* The event monitoring for Docker >= 29.0
* The error reporting if a command fails
* The detection of the `docker` command on macOS

### Boot to Qt

Fixed

* A crash when reconnecting physical devices via USB
  ([QTCREATORBUG-34246](https://bugreports.qt.io/browse/QTCREATORBUG-34246))
* That the `Custom Command` deploy step was no longer available

### Bare Metal

Fixed

* The device state
  ([QTCREATORBUG-34221](https://bugreports.qt.io/browse/QTCREATORBUG-34221))
* Wrong errors reported for the executable of run configurations
  ([QTCREATORBUG-34268](https://bugreports.qt.io/browse/QTCREATORBUG-34268))

Credits for these changes go to:
--------------------------------
Alessandro Portale  
Alexander Akulich  
Alexandru Croitor  
André Hartmann  
André Pönitz  
Bernhard Beschow  
Björn Schäpers  
Christian Kandeler  
Christian Stenger  
Cristian Adam  
Eike Ziller  
Jaroslaw Kobus  
Leena Miettinen  
Marcus Tillmanns  
Markus Redeker  
Sami Shalayel  
Teea Poldsam  
