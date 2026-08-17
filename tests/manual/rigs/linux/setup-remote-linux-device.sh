#!/bin/bash
# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
#
# Scenario: build-local-linux-run-linux (see ../README.md) - build locally,
# run/debug on a remote Linux device of the same ABI.
#
# Seed a throwaway settings directory with a GenericLinux (remote-linux SSH)
# device and a kit bound to it, so a headless Qt Creator can debug on / attach
# to that device. Everything comes from the environment - no host, user or key
# is hard-coded:
#
#   QTC_SETTINGS         -settingspath dir to seed              (required)
#   QTC_REMOTE_HOST      device hostname or IP                 (required)
#   QTC_REMOTE_USER      ssh user on the device                (required)
#   QTC_REMOTE_PORT      ssh port                              (default 22)
#   QTC_REMOTE_GDBSERVER gdbserver path on the device          (default /usr/bin/gdbserver)
#   QTC_FREE_PORTS       ports gdbserver may use on the device (default 30000-31000)
#   QTC_DEVICE_ID        internal device id                    (default remote-linux)
#   QTC_DEVICE_NAME      display name                          (default = id)
#   QTC_REMOTE_SSH       ssh executable Creator should use      (default: ssh in PATH)
#   QTC_SDKTOOL          sdktool path        (default: <repo>/libexec/qtcreator/sdktool)
#
# Prerequisite: Qt Creator must have been launched once against QTC_SETTINGS so
# it auto-detected the host toolchains, debugger and Desktop kit - this script
# clones that kit's C/C++ toolchains and debugger into the remote kit. (sdktool
# CAN create the kit too, but Creator prunes sdktool-origin kits on startup, so
# a hand-written user kit is used instead.)
set -u

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$here/../../../.." && pwd)

: "${QTC_SETTINGS:?set QTC_SETTINGS to the -settingspath dir}"
: "${QTC_REMOTE_HOST:?set QTC_REMOTE_HOST}"
: "${QTC_REMOTE_USER:?set QTC_REMOTE_USER}"
QTC_REMOTE_PORT=${QTC_REMOTE_PORT:-22}
QTC_REMOTE_GDBSERVER=${QTC_REMOTE_GDBSERVER:-/usr/bin/gdbserver}
QTC_FREE_PORTS=${QTC_FREE_PORTS:-30000-31000}
QTC_DEVICE_ID=${QTC_DEVICE_ID:-remote-linux}
QTC_DEVICE_NAME=${QTC_DEVICE_NAME:-$QTC_DEVICE_ID}
QTC_SDKTOOL=${QTC_SDKTOOL:-$repo/libexec/qtcreator/sdktool}

cfg=$QTC_SETTINGS/QtProject/qtcreator
[ -f "$cfg/profiles.xml" ] || {
    echo "ERROR: $cfg/profiles.xml not found." >&2
    echo "       Launch Qt Creator once with -settingspath $QTC_SETTINGS first" >&2
    echo "       so the host toolchains/debugger/Desktop kit get auto-detected." >&2
    exit 1
}

# 1) The SSH username is stored in the device's "Uname" key; FreePortsSpec is
#    mandatory or the debug-channel port allocation fails and attach stalls.
#    FreePortsSpec/DebugServer are standard device fields, so use the dedicated
#    flags (passing them as extra key/value pairs conflicts with the defaults).
"$QTC_SDKTOOL" -s "$cfg" addDev \
    --id "$QTC_DEVICE_ID" --name "$QTC_DEVICE_NAME" --type 0 \
    --osType GenericLinuxOsType --host "$QTC_REMOTE_HOST" --uname "$QTC_REMOTE_USER" \
    --sshPort "$QTC_REMOTE_PORT" --authentication 0 \
    --freePorts "$QTC_FREE_PORTS" --debugServerKey "$QTC_REMOTE_GDBSERVER" \
    || { echo "ERROR: sdktool addDev failed" >&2; exit 1; }

# 2) Also set the GLOBAL ssh executable: device access (env probe, process
#    list) uses SshSettings/SshFilePath, not the per-device SshExecutable.
if [ -n "${QTC_REMOTE_SSH:-}" ]; then
    ini=$QTC_SETTINGS/QtProject/QtCreator.ini
    grep -q '^\[SshSettings\]' "$ini" 2>/dev/null || printf '\n[SshSettings]\n' >>"$ini"
    grep -q '^SshFilePath=' "$ini" 2>/dev/null || \
        printf 'SshFilePath=%s\n' "$QTC_REMOTE_SSH" >>"$ini"
fi

# 3) Clone the first Desktop kit's toolchains + debugger into a user kit that
#    targets the remote device. KitChooser hides invalid kits, so the toolchain
#    is required for the kit to appear in the attach dialog.
QTC_DEVICE_ID="$QTC_DEVICE_ID" QTC_DEVICE_NAME="$QTC_DEVICE_NAME" python3 - "$cfg/profiles.xml" <<'PY'
import os, sys, re
path = sys.argv[1]
xml = open(path, encoding="utf-8").read()
dev = os.environ["QTC_DEVICE_ID"]
name = os.environ["QTC_DEVICE_NAME"]

if 'PE.Profile.Device">%s<' % dev in xml:
    print("kit for device '%s' already present, leaving profiles.xml alone" % dev)
    sys.exit(0)

# Pull toolchains + debugger from the first profile that has a ToolChainsV3 map.
def grab(block, key):
    m = re.search(r'key="%s">([^<]*)<' % re.escape(key), block)
    return m.group(1) if m else ""

profiles = re.findall(r'<data>\s*<variable>Profile\.\d+</variable>.*?</data>', xml, re.S)
tc_c = tc_cxx = dbg = ""
for p in profiles:
    if "PE.Profile.ToolChainsV3" in p and "GenericLinuxOsType" not in p:
        tcmap = re.search(r'ToolChainsV3">(.*?)</valuemap>', p, re.S).group(1)
        tc_c = grab(tcmap, "C"); tc_cxx = grab(tcmap, "Cxx")
        dbg = grab(p, "Debugger.Information")
        break
if not tc_c or not tc_cxx:
    sys.exit("ERROR: no Desktop kit with toolchains found; launch Creator once first")

count = int(re.search(r'<variable>Profile\.Count</variable>\s*<value type="int">(\d+)</value>', xml).group(1))
new_id = "{a1b2c3d4-0000-0000-0000-%012d}" % count
dbgline = ('    <value type="QString" key="Debugger.Information">%s</value>\n' % dbg) if dbg else ""
block = '''\
 <data>
  <variable>Profile.%d</variable>
  <valuemap type="QVariantMap">
   <value type="bool" key="PE.Profile.AutoDetected">false</value>
   <value type="QString" key="PE.Profile.AutoDetectionSource"></value>
   <valuemap type="QVariantMap" key="PE.Profile.Data">
%s    <value type="QByteArray" key="PE.Profile.BuildDeviceType">Desktop</value>
    <value type="QString" key="PE.Profile.Device">%s</value>
    <value type="QString" key="PE.Profile.DeviceType">GenericLinuxOsType</value>
    <valuemap type="QVariantMap" key="PE.Profile.ToolChainsV3">
     <value type="QString" key="C">%s</value>
     <value type="QString" key="Cxx">%s</value>
    </valuemap>
   </valuemap>
   <value type="UnknownType" key="PE.Profile.DeviceTypeForIcon"></value>
   <value type="QString" key="PE.Profile.Icon"></value>
   <value type="QString" key="PE.Profile.Id">%s</value>
   <valuelist type="QVariantList" key="PE.Profile.MutableInfo"/>
   <value type="QString" key="PE.Profile.Name">%s</value>
   <value type="bool" key="PE.Profile.SDK">false</value>
   <valuelist type="QVariantList" key="PE.Profile.StickyInfo"/>
  </valuemap>
 </data>
''' % (count, dbgline, dev, tc_c, tc_cxx, new_id, name)

xml = xml.replace(' <data>\n  <variable>Profile.Count</variable>',
                  block + ' <data>\n  <variable>Profile.Count</variable>', 1)
xml = xml.replace('<variable>Profile.Count</variable>\n  <value type="int">%d</value>' % count,
                  '<variable>Profile.Count</variable>\n  <value type="int">%d</value>' % (count + 1), 1)
open(path, "w", encoding="utf-8").write(xml)
print("added kit '%s' (id %s) for device '%s'" % (name, new_id, dev))
PY

echo "done. Device '$QTC_DEVICE_ID' and a kit for it are in $cfg."
echo "Restart Qt Creator against $QTC_SETTINGS to pick them up."
