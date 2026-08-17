# Scenario catalog

How common build/run targets map onto the two axes (see README.md: *where you
build* x *what you run on*), what each needs on the Qt Creator side, and how the
rig drives it. This is reference guidance; only some cells ship a script today.
Supply every machine coordinate (host/user/key/port) via environment variables
- there is no machine registry in the tree.

| run target | device / transport            | debugger engine | typical build | driver          | script    |
|------------|-------------------------------|-----------------|---------------|-----------------|-----------|
| linux      | GenericLinux over SSH         | gdb + gdbserver | local (same ABI) or on-device | MCP (port)      | `linux/`  |
| windows    | Remote Windows over SSH, or headless on the box | CDB (engine on host) | MSVC on box, or mingw cross | MCP over ssh tunnel | pattern |
| macos      | local, or SSH (headless only) | lldb            | local (Xcode)  | headless lldb (GUI walled by SIP/TCC) | pattern |
| freebsd    | local, or a QEMU VM over SSH  | gdb             | local / on-VM  | GUI + Xvfb (packaged Creator lacks McpServer) | pattern |
| qnx        | QNX device over SSH (SDP)     | gdb (QNX)       | cross (SDP)    | MCP (port)      | pattern   |
| bare-metal | gdb stub over TCP or pipe (simavr / OpenOCD) | gdb (cross) | cross toolchain | MCP `start_debug remote_channel` | pattern |
| harmonyos  | device over hdc (TCP)         | lldb (OHOS)     | cross (OHOS SDK)| MCP (port)      | pattern   |

## Notes per target

- **linux** - the only cell with a script (`linux/setup-remote-linux-device.sh`).
  `build-local` uses the host toolchain when the device shares the host ABI;
  `build-remote` makes the device the kit's build device and uses an on-device
  toolchain; a cross run (`...-run-<other>`) swaps in a cross-compiler.

- **windows** - two shapes. As a *remote Windows device* a local Creator drives
  it over SSH with the debugger engine on the host talking to `cdb.exe` on the
  device. Alternatively run the built Creator *on the box* with
  `-platform offscreen -load McpServer -mcp-port <P>` and drive MCP through an
  `ssh -L` tunnel. Launch detached (Windows sshd kills the process tree when the
  ssh session ends) and force software rendering in a non-GUI session.

- **macos** - a plain SSH session has no window server, and SIP/TCC block
  synthesizing input, so full IDE-GUI automation is not achievable without
  someone at the console. Verify the underlying mechanism headlessly (drive
  `lldb` directly); first debug run may need "Developer Tools Access" authorized
  on the console beforehand.

- **freebsd** - a persistent QEMU VM is the practical target. A *packaged*
  Creator there usually lacks the `McpServer` plugin, so drive the GUI under an
  in-guest `Xvfb` and screenshot with `import`, rather than over MCP. gdb may
  need pre-seeding as the kit debugger (auto-detect can pick lldb).

- **qnx / harmonyos** - cross-compiled with the vendor SDK; the device is added
  over its native transport (SSH for QNX, `hdc` for HarmonyOS) and then driven
  like the linux cell. These need their own `<platform>/` setup script (device +
  cross toolchain seeding) when someone has a target to verify against.

- **bare-metal** - no real hardware needed: a simulator (e.g. `simavr -g`) or
  OpenOCD acts as a gdb stub, and Creator attaches via `start_debug` with
  `remote_channel`. Exercising the BareMetal plugin's own provider path
  additionally needs a debug-server-provider + a BareMetalOsType device seeded.

When you have a real target for one of the `pattern` cells, add a
`<platform>/setup-*.sh` beside `linux/` and verify it live the same way.
