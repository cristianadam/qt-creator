# Empty "Executable on device" reproducer

Reproduces the case where a remote run configuration has no deployed
executable, exercising `RunConfiguration::createNoRemoteExecutableIssue()`.

## Setup

- A kit with a *local* build device and a *remote* run device (Remote Linux
  or QNX), i.e. build device != run device.

## Steps

1. Open `remotelinux-noexe.qbs` with the cross-device kit and build it.
2. Open Projects mode > Run Settings.

## Expected

- The run configuration `remotelinux-noexe (on <device>)` is selected.
- "Executable on device" is empty: the application is not installed, so
  deployment maps nothing to the device.
- A warning is shown explaining that no remote executable was deployed, and
  the run is not blocked (it may be started and then fails with the underlying
  error).

Adding an install rule for the application (see the companion commit) deploys
the executable, populates "Executable on device", and clears the warning.
