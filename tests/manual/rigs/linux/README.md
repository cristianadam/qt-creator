# Remote-linux (GenericLinux SSH) rig

Set up a throwaway settings directory with a remote Linux device and a kit
bound to it, then debug on or attach to a process on that device from a
headless Qt Creator. No host, user, or key is hard-coded - pass your own device
via environment variables.

**Scenario:** `build-local-linux-run-linux` (see the top-level README) - the
kit builds locally (Desktop build device, host toolchain) and runs/debugs on a
remote Linux device of the same ABI. Variations are just different env values:

- **build-remote** (compile on the device): additionally make the device the
  kit's build device and use an on-device toolchain. `setup-remote-linux-device.sh`
  seeds the run device + host toolchain; a build-on-device kit also needs the
  device's own toolchain (auto-detected once the device is reachable).
- **cross run** (`...-run-<other>`): point the run device at that platform's
  device and the kit's toolchain at a cross-compiler for it; the device seeding
  and driving here are otherwise the same. Give such a target its own
  `<platform>/` directory when it needs different device/toolchain setup.

## Configure your device (env only)

    export QTC_SETTINGS=$PWD/.rig-run/settings
    export QTC_REMOTE_HOST=<ip-or-hostname>
    export QTC_REMOTE_USER=<ssh-user>
    # optional:
    export QTC_REMOTE_PORT=22
    export QTC_REMOTE_GDBSERVER=/usr/bin/gdbserver
    export QTC_FREE_PORTS=30000-31000

## Steps

1. Launch once so the host toolchains/debugger/Desktop kit get auto-detected:

       tests/manual/rigs/common/launch-creator.sh
       tests/manual/rigs/common/stop-creator.sh

2. Seed the device + a kit that targets it:

       tests/manual/rigs/linux/setup-remote-linux-device.sh

3. Launch again and drive it (e.g. attach to a running process):

       tests/manual/rigs/common/launch-creator.sh
       # ... mcpcli.py calls ...
       tests/manual/rigs/common/stop-creator.sh

## Non-obvious things this encodes

- **SSH username** lives in the device's `Uname` key (not a separate field).
- **`FreePortsSpec` is mandatory.** Without a ports range, the debug-channel
  port allocation fails (a `port.cpp` `isValid()` soft-assert) and an attach
  stalls forever at `DebuggerNotReady`.
- **The kit must have a toolchain.** `KitChooser` hides invalid kits, so a kit
  with only a device+debugger never appears in the attach dialog. The setup
  script clones the auto-detected Desktop kit's C/C++ toolchains.
- **sdktool-created kits get pruned** by Creator on startup, so the kit is
  hand-written as a user kit (`PE.Profile.AutoDetected=false`); only the device
  is created with sdktool.
- **Global vs per-device ssh.** Device access (env probe, process list) uses
  the global `SshSettings/SshFilePath`, not the per-device `SshExecutable`. If
  the system ssh config is unusable (see `common/ssh-wrap.sh`), set
  `QTC_REMOTE_SSH` to the wrapper - the setup script points the global
  `SshSettings/SshFilePath` at it.

## Attaching to a running process

"Attach to Running Application" (`call_action Debugger.AttachToRemoteProcess`)
opens a modal dialog "List of Processes": select your kit in the combo, filter
by PID in the `Utils::FancyLineEdit` (mcpcli `type_text` uses the `input`
argument for the text to type), select the row in the process QTreeView, then
"Attach to Process". Creator starts gdbserver on the device automatically.

**Target must be ptrace-able.** If the device has
`kernel.yama.ptrace_scope=1`, attaching to a non-descendant process fails with
"Operation not permitted" (this defeats any attacher, not just Creator). Either
lower `ptrace_scope` (root) or have the target call
`prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY)`. Symptom when hit: the attach
starts, then the session tears straight back down to state "none".
