# HarmonyOS Plugin for Qt Creator

The plugin builds, deploys, runs and debugs Qt applications on a HarmonyOS
device. It drives the HarmonyOS command-line tools and talks to the device with
`hdc`. The SDK detection also looks where DevEco Studio keeps its copy, but
DevEco Studio is not available for Linux, so none of this was tried with it.

All of it is cross-development: Qt Creator runs on a desktop host, a Linux one
in what follows, and the HarmonyOS machine is the target it builds for and
deploys to. Running Qt Creator on HarmonyOS itself, and developing there for
the machine it runs on, is what this is meant to lead to; nothing below covers
that yet, so do not read a statement about the cross case as one about the
native one.

Everything below was measured on a consumer HAD-W32 (OpenHarmony 6.1.0.115,
API 23) with the command-line tools 6.0.2 (API 22) and Qt 6.12 for HarmonyOS,
from an x86_64 Linux host.

The three lists that follow are deliberately kept apart: what was watched
happening, what is in place but was never watched, and what cannot be done.
Reading the second list as the first is how one ends up debugging the wrong
thing.

## Seen working

- **SDK, toolchain, Qt versions, kits.** With the SDK location set in
  Preferences > SDKs > HarmonyOS, Clang toolchains appear for aarch64, armv7
  and x86_64, a Qt for HarmonyOS is recognized by its `ohos` mkspec, and a kit
  for it comes out valid and without complaints.
- **Devices.** `hdc list targets` finds the connected device, the device page
  reports it as ready, and its device test finishes successfully.
- **Deploying and installing.** A deploy produced a signed package and put it
  on the device. That the debug session below then ran is also what says the
  debug plugin and the `lldb-server` really were in that package.
- **Debugging.** The application starts the debug server itself, Qt Creator
  attaches, the breakpoints are resolved and the debugger stops.
- **Symbolised frames.** A stop showed every frame of the interrupted thread
  with its function name, demangled, across the musl loader and the OHOS
  framework libraries. The libraries of a HarmonyOS application are loaded at
  an address that changes with every launch, which used to leave the frames
  nameless; what resolves them is the `remote-ohos` platform the debug worker
  selects, which fetches the libraries off the device into the debugger's
  module cache and matches them by build id. Nothing has to report those
  addresses for this to work.

## Implemented but not confirmed

Written and built, with the pieces they rest on checked by hand against the
device, but never yet watched doing their job from within Qt Creator:

- **Running.** The ability is started with `aa start`, the run is meant to stay
  alive for as long as the application does by following its `hilog` output,
  and stopping it force-stops the bundle. Each of those commands was tried on
  the device by hand; the run worker driving them was not.
- **Building on the device.** The device is offered as a build device and its
  build tools are detected, and the command bridge is signed on its way there
  because the device refuses an unsigned binary. Signing a binary for this
  device was tried by hand; this path through Qt Creator was not.
- **File access.** Reading and writing files on the device goes through `hdc`,
  with the transfer standing in where `hdc shell` cannot help. There is a test
  for it that needs an attached device.

## What it needs

- A provisioning profile for the application, set as "Provisioning profile" in
  Preferences > SDKs > HarmonyOS. The device installs a package only under the
  bundle name the profile is issued for, and only such a package may be
  debugged, so the deploy step writes that bundle name into the package.
- A Java runtime, which the signing tool of the SDK needs.

## Not possible

- **Launching under the debugger.** The device allows `PTRACE_ATTACH` from
  inside an installed application, and refuses `PTRACE_TRACEME` everywhere. So
  the application starts the server and the debugger attaches, rather than the
  server starting the application. This is a property of the device, not
  something left to do.

## What the SDK makes awkward

Worked around here, but worth knowing when reading the deploy step:

- `hvigor` 6.0.2 deletes the generated project directly after its `PackageHap`
  task, and would take the finished package with it, so the package is copied
  out before the next run replaces the project. The 6.1 tools leave the project
  alone; copying the package out costs nothing there and keeps both usable.
- `hvigor` signs only with material that DevEco Studio manages, so the package
  is signed here instead, with the `hap-sign-tool` of the SDK.
- `harmonydeployqt` stages neither the third-party runtime dependencies nor the
  debug server, so both are put into the package afterwards.
- The `ohos` mkspec stops with an error unless `NATIVE_OHOS_SDK` is set in the
  environment, which Qt Creator does not set, so the mkspec cannot be evaluated
  and says nothing about the platform. A Qt version is therefore recognized by
  the name of its mkspec. One that was registered earlier, as a desktop Qt,
  stays that way, and its kit keeps warning that the Qt version does not
  support the device type even though building and running work.
