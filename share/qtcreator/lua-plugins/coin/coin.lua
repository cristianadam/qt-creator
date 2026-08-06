-- Copyright (C) 2026 The Qt Company Ltd.
-- SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

-- Coin: internal/experimental helper to stand up a Coin debug VM (a QNX SDP
-- host) and wire it into Qt Creator. Real-world exercise of the Lua API; see
-- /opt/qnx_workspace/coin-vm/COIN-PLUGIN-DESIGN.md for the design + API gaps.
return {
    Id = "coin",
    Name = "Coin",
    DisplayName = "Coin Debug VM",
    Version = "0.1.0",
    CompatVersion = "0.1.0",
    VendorId = "theqtcompany",
    Vendor = "The Qt Company",
    Category = "Utilities",
    Experimental = true,
    DisabledByDefault = false,
    Description = "Schedule and connect to Coin debug VMs from the IDE.",
    LongDescription = [[
Internal/experimental. Schedules a Coin debug VM (default: the QNX SDP host),
waits until it is reachable, and helps connect and register it as a device.
    ]],
    Dependencies = {
        { Id = "lua", Version = "15.0.0" },
    },
    Type = "Script",
    printToOutputPane = true,
    setup = function() require("init").setup() end,
} --[[@as QtcPlugin]]
