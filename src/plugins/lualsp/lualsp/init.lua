-- Copyright (C) 2024 The Qt Company Ltd.
-- SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
local LSP = require('LSP')
local mm = require('MessageManager')
local Utils = require('Utils')
local Process = require('Process')
local S = require('Settings')
local Layout = require('Layout')
local a = require('async')

Settings = {}

local function createCommand()
  local cmd = { Settings.binary.expandedValue:nativePath() }
  if Settings.showNode.value then
    table.insert(cmd, '--shownode=true')
  end
  if Settings.showSource.value then
    table.insert(cmd, '--showsource=true')
  end
  if Settings.developMode.value then
    table.insert(cmd, '--develop=true')
  end

  return cmd
end
local function installServer()
  print("Lua Language Server not found, installing ...")
  local cmds = {
    mac = "brew install lua-language-server",
    windows = "winget install lua-language-server",
    linux = "sudo apt install lua-language-server"
  }

  if a.wait(Process.runInTerminal(cmds[Utils.HostOsInfo.os])) == 0 then
    print("Lua Language Server installed!")
    local binary = a.wait(Utils.FilePath.fromUserInput("lua-language-server"):searchInPath())
    if binary:isExecutableFile() == false then
      mm.writeFlashing("Lua Language Server was installed, but I could not find it in your PATH")
      return false
    end

    Settings.binary.value = binary:toUserOutput()
    Settings:apply()
    return true
  end

  mm.writeFlashing("Lua Language Server installation failed!")
  return false
end
local function setupClient()
  Client = LSP.Client.create({
    name = 'Lua Language Server',
    cmd = createCommand,
    transport = 'stdio',
    languageFilter = {
      patterns = { '*.lua' },
      mimeTypes = { 'text/x-lua' }
    },
    settings = Settings,
    startBehavior = "RequiresFile",
    onStartFailed = function(ask)
      a.sync(function()
        local prereqs = {
          mac = "brew",
          windows = "winget",
          linux = "" -- TODO: Add linux package manager
        }

        if prereqs[Utils.HostOsInfo.os] == "" then
          return
        end

        if a.wait(Utils.FilePath.fromUserInput(prereqs[Utils.HostOsInfo.os]):searchInPath()):isExecutableFile() == false then
          return
        end

        if a.wait(ask("Try to install the Lua Language Server?")) then
          installServer()
        end
      end)()
    end
  })

  Client.on_instance_start = function()
    print("Instance has started")
  end

  Client:registerMessage("$/status/report", function(params)
    mm.writeFlashing(params.params.text .. ": " .. params.params.tooltip);
  end)
end

local function using(tbl)
  local result = _G
  for k, v in pairs(tbl) do result[k] = v end
  return result
end
local function layoutSettings()
  --- "using namespace Layout"
  local _ENV = using(Layout)

  local installButton = {}

  if Settings.binary.expandedValue:isExecutableFile() == false then
    installButton = {
      "Language server not found:",
      Row {
        PushButton {
          text("Try to install lua language server"),
          onClicked(function() a.sync(installServer)() end),
          br,
        },
        st
      }
    }
  end
  local layout = Form {
    Settings.binary, br,
    Settings.developMode, br,
    Settings.showSource, br,
    Settings.showNode, br,
    table.unpack(installButton)
  }

  return layout
end

local function setupAspect()
  ---@class Settings: AspectContainer
  Settings = S.AspectContainer.create({
    autoApply = false,
    layouter = layoutSettings,
  });

  Settings.binary = S.FilePathAspect.create({
    settingsKey = "LuaCopilot.Binary",
    displayName = "Binary",
    labelText = "Binary:",
    toolTip = "The path to the lua-language-server binary.",
    expectedKind = S.Kind.ExistingCommand,
    defaultPath = Utils.FilePath.fromUserInput("lua-language-server"),
  })
  -- Search for the binary in the PATH
  local serverPath = Settings.binary.defaultPath
  local absolute = a.wait(serverPath:searchInPath()):resolveSymlinks()
  if absolute:isExecutableFile() == true then
    Settings.binary.defaultPath = absolute
  end
  Settings.developMode = S.BoolAspect.create({
    settingsKey = "LuaCopilot.DevelopMode",
    displayName = "Enable Develop Mode",
    labelText = "Enable Develop Mode:",
    toolTip = "Turns on the develop mode of the language server.",
    defaultValue = false,
    labelPlacement = S.LabelPlacement.InExtraLabel,
  })

  Settings.showSource = S.BoolAspect.create({
    settingsKey = "LuaCopilot.ShowSource",
    displayName = "Show Source",
    labelText = "Show Source:",
    toolTip = "Display the internal data of the hovering token.",
    defaultValue = false,
    labelPlacement = S.LabelPlacement.InExtraLabel,
  })

  Settings.showNode = S.BoolAspect.create({
    settingsKey = "LuaCopilot.ShowNode",
    displayName = "Show Node",
    labelText = "Show Node:",
    toolTip = "Display the internal data of the hovering token.",
    defaultValue = false,
    labelPlacement = S.LabelPlacement.InExtraLabel,
  })
  return Settings
end
local function setup(parameters)
  setupAspect()
  setupClient()
end

return {
  setup = function() a.sync(setup)() end,
}
