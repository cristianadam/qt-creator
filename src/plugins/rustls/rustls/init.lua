-- Copyright (C) 2024 The Qt Company Ltd.
-- SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
local LSP = require('LSP')
local mm = require('MessageManager')
local Utils = require('Utils')
local Process = require('Process')
local S = require('Settings')
local Layout = require('Layout')
local a = require('async')
local fetch = require('Fetch').fetch

Settings = {}

local function createCommand()
  local cmd = { Settings.binary.expandedValue:nativePath() }
  return cmd
end

local function filter(tbl, callback)
  for i = #tbl, 1, -1 do
    if not callback(tbl[i]) then
      table.remove(tbl, i)
    end
  end
end

local function installOrUpdateServer()
  local data = a.wait(fetch({
    url = "https://api.github.com/repos/rust-lang/rust-analyzer/releases?per_page=2",
    convertToTable = true,
    headers = {
      Accept = "application/vnd.github.v3+json",
      ["X-GitHub-Api-Version"] = "2022-11-28"
    }
  }))

  if type(data) == "table" and #data > 1 then
    local r = data[1]
    if r.prerelease then
      r = data[2]
    end
    Install = require('Install')
    local lspPkgInfo = Install.packageInfo("rust-analyzer")
    if not lspPkgInfo or lspPkgInfo.version ~= r.tag_name then
      local osTr = { mac = "apple-darwin", windows = "pc-windows-msvc", linux = "unknown-linux-gnu" }
      local archTr = { unknown = "", x86 = "", x86_64 = "x86_64", itanium = "", arm = "", arm64 = "aarch64" }
      local extTr = { mac = "gz", windows = "zip", linux = "gz" }
      local os = osTr[Utils.HostOsInfo.os]
      local arch = archTr[Utils.HostOsInfo.architecture]
      local ext = extTr[Utils.HostOsInfo.os]

      local expectedFileName = "rust-analyzer-" .. arch .. "-" .. os .. "." .. ext

      filter(r.assets, function(asset)
        return string.find(asset.name, expectedFileName, 1, true) == 1
      end)

      if #r.assets == 0 then
        print("No assets found for this platform")
        return
      end
      local res, err = a.wait(Install.install(
        "Do you want to install the rust-analyzer?", {
        name = "rust-analyzer",
        url = r.assets[1].browser_download_url,
        version = r.tag_name
      }))

      if not res then
        mm.writeFlashing("Failed to install rust-analyzer: " .. err)
        return
      end

      lspPkgInfo = Install.packageInfo("rust-analyzer")
      print("Installed:", lspPkgInfo.name, "version:", lspPkgInfo.version, "at", lspPkgInfo.path)
    end

    local binary = "rust-analyzer"
    if Utils.HostOsInfo.isWindowsHost() then
      binary = "rust-analyzer.exe"
    end

    Settings.binary.defaultPath = lspPkgInfo.path:resolvePath(binary)
    Settings:apply()
    return
  end

  if type(data) == "string" then
    print("Failed to fetch:", data)
  else
    print("No rust-analyzer release found.")
  end
end
IsTryingToInstall = false

local function setupClient()
  Client = LSP.Client.create({
    name = 'Rust Language Server',
    cmd = createCommand,
    transport = 'stdio',
    languageFilter = {
      patterns = { '*.rs' },
      mimeTypes = { 'text/rust' }
    },
    settings = Settings,
    startBehavior = "RequiresProject",
    onStartFailed = function()
      a.sync(function()
        if IsTryingToInstall == true then
          return
        end
        IsTryingToInstall = true
        installOrUpdateServer()
        IsTryingToInstall = false
      end)()
    end
  })
end

local function using(tbl)
  local result = _G
  for k, v in pairs(tbl) do result[k] = v end
  return result
end
local function layoutSettings()
  --- "using namespace Layout"
  local _ENV = using(Layout)

  local layout = Form {
    Settings.binary, br,
    Row {
      PushButton {
        text("Try to install Rust language server"),
        onClicked(function() a.sync(installOrUpdateServer)() end),
        br,
      },
      st
    }
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
    settingsKey = "Rustls.Binary",
    displayName = "Binary",
    labelText = "Binary:",
    toolTip = "The path to the rust analyzer binary.",
    expectedKind = S.Kind.ExistingCommand,
    defaultPath = Utils.FilePath.fromUserInput("rust-analyzer"),
  })
  -- Search for the binary in the PATH
  local serverPath = Settings.binary.defaultPath
  local absolute = a.wait(serverPath:searchInPath()):resolveSymlinks()
  if absolute:isExecutableFile() == true then
    Settings.binary.defaultPath = absolute
  end

  return Settings
end

local function setup(parameters)
  setupAspect()
  setupClient()
end

return {
  setup = function() a.sync(setup)() end,
}