# HarmonyOS Plugin for Qt Creator

The plugin builds, deploys, runs and debugs Qt applications on a HarmonyOS
device. It drives the HarmonyOS SDK, either from DevEco Studio or from the
command-line tools, and talks to the device with `hdc`.

All of it is cross-development: Qt Creator runs on a desktop host, a Linux one
in what follows, and the HarmonyOS machine is the target it builds for and
deploys to. Running Qt Creator on HarmonyOS itself, and developing there for
the machine it runs on, is what this is meant to lead to; nothing below covers
that yet, so do not read a statement about the cross case as one about the
native one.

Everything below was measured on a consumer HAD-W32 (OpenHarmony 6.1.0.115,
API 23) with the command-line tools 6.0.2 (API 22) and Qt 6.12 for HarmonyOS,
from an x86_64 Linux host.

## What works

- **SDK, toolchain, Qt versions, kits.** The SDK location is auto-detected or
  configured in Preferences > SDKs > HarmonyOS. Clang toolchains are registered
  for aarch64, armv7 and x86_64, a Qt for HarmonyOS is recognized by its `ohos`
  mkspec, and a kit is created per Qt version and architecture.
- **Devices.** `hdc list targets` finds the connected devices, the device page
  reports the connection state, and the device test reports it in words.
- **Building on the device.** The device is also offered as a build device. The
  build tools it carries are detected, and the command bridge is signed on its
  way there, because the device refuses an unsigned binary.
- **File access.** Reading and writing files on the device goes through `hdc`,
  with the transfer standing in where `hdc shell` cannot help.
- **Deploying.** The generated HarmonyOS project is created next to the build,
  the Qt libraries and the application are staged into it, the debug plugin is
  compiled against the target Qt, and the SDK's `lldb-server` is packaged.
- **Running.** The ability is started with `aa start`. The run stays alive for
  as long as the application does, its `hilog` output arrives in the
  application output, and stopping the run force-stops the bundle.
- **Debugging.** The application starts the debug server itself and Qt Creator
  attaches to it. Breakpoints are resolved and the debugger stops on them.

## What it needs

- A provisioning profile for the application, set as "Provisioning profile" in
  Preferences > SDKs > HarmonyOS. The device installs a package only under the
  bundle name the profile is issued for, and only such a package may be
  debugged, so the deploy step writes that bundle name into the package.
- A Java runtime, which the signing tool of the SDK needs.

## What is not finished

- **Symbols in a stopped backtrace are not confirmed.** A library of a
  HarmonyOS application is loaded at an address that changes with every launch,
  which left the frames showing addresses. The debug plugin reports what the
  process mapped and the addresses are now passed to the debugger, but that
  this is all it takes has not been seen on a device yet.
- **Launching under the debugger is not possible, and does not need to be.**
  The device allows `PTRACE_ATTACH` from inside an installed application, and
  refuses `PTRACE_TRACEME` everywhere. That is why the application starts the
  server and the debugger attaches, instead of the server starting the
  application.

## What the SDK makes awkward

Worked around here, but worth knowing when reading the deploy step:

- `hvigor` deletes the generated project directly after its `PackageHap` task,
  and would take the finished package with it, so the package is copied out
  before the next run replaces the project.
- `hvigor` signs only with material that DevEco Studio manages, so the package
  is signed here instead, with the `hap-sign-tool` of the SDK.
- `harmonydeployqt` stages neither the third-party runtime dependencies nor the
  debug server, so both are put into the package afterwards.
