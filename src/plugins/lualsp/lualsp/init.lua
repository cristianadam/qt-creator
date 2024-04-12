local inspect = require('inspect')
local LSP = require('LSP')
local mm = require('MessageManager')
local Utils = require('Utils')
local Process = require('Process')
local S = require('Settings')
local Layout = require('Layout')
local a = require('async')

Settings = {}

local function setupClient()
  Client = LSP.Client.create({
    name = 'lua-lsp',
    cmd = { 'lua-language-server' }, -- Expects it in PATH
    transport = 'stdio',
    languageFilter = {
      patterns = { '*.lua' },
      mimeTypes = { 'text/x-lua' }
    }
  })

  Client.on_instance_start = function()
    print("ON_INSTANCE_START!")
  end

  Client:registerMessage("$/status/report", function(params)
    mm.writeFlashing(params.params.text .. ": " .. params.params.tooltip);
  end)
  mm.writeFlashing("Client: ", Client)
end

local function installServer()
  print("Lua Language Server not found, installing ...")
  local cmds = {
    mac = "brew install lua-language-server",
    win = "winget install lua-language-server",
    linux = "sudo apt install lua-language-server"
  }
  if a.wait(Process.runInTerminal(cmds[Utils.HostOsInfo.os])) == 0 then
    print("Lua Language Server installed!")
    setupClient()
  else
    print("Lua Language Server installation failed!")
  end
end

local function using(tbl)
  local result = _G
  for k, v in pairs(tbl) do result[k] = v end
  return result
end
local function layoutSettings()
  --- "using namespace Layout"
  local _ENV = using(Layout)

  
  local layout = Column {
    Group {
      title("Note"),
      Column {
        "This is a warning!", br,
        "Please take it seriously!"
      }
    },
    Form {
      PushButton {
        text("Hallo Welt!"),
        onClicked(function() print("Hallo Welt!") end),
      }, br,
      Settings.enableCopilot, br,
      Settings.autoComplete, br,
    },
  }

  --local dlg = QDialog()
  --layout:attachTo(dlg)
  --
  --print("LayoutDir:", dlg.layoutDirection, dlg.autoFillBackground);
  --local g = dlg.geometry
  --print("Geo:", inspect(g))
  --g.width = 400
  --g.height = 300
  --dlg.geometry = g
  --print("Geo:", inspect(dlg.geometry))
  --dlg:show()
  return layout
end

local function setupAspect()
  Settings = S.AspectContainer.create({
    autoApply = false,
    layouter = layoutSettings,
  });

  Settings.enableCopilot = S.BoolAspect.create({
    settingsKey = "LuaCopilot.EnableCopilot",
    displayName = "Enable Copilot",
    labelText = "Enable Copilot",
    toolTip = "Enables the Copilot integration.",
    defaultValue = false,
    onVolatileValueChanged = function() print("Volatile Value Changed") end,
  })

  Settings.autoComplete = S.BoolAspect.create({
    settingsKey = "LuaCopilot.AutoComplete",
    displayName = "Auto Complete",
    labelText = "Auto Complete",
    toolTip = "Automatically request suggestions for the current text cursor position after changes to the document.",
    defaultValue = true,
  })

  OptionsPage = S.OptionsPage.create({
    id = "LanguageClient.Lua",
    displayName = "Lua",
    categoryId = "ZY.LanguageClient",
    displayCategory = "Language Client",
    categoryIconPath = ":/languageclient/images/settingscategory_languageclient.png",
    aspectContainer = Settings,
  })
  layoutSettings()

end
local function setup(parameters)
  print("Setting up Lua Language Server ...")
  setupAspect()
  local serverPath = Utils.FilePath.fromUserInput("lua-language-server")
  local absolute = serverPath:searchInPath()
  if absolute:exists() == true then
    print("Lua Language Server found!")
    setupClient()
  else
    a.sync(installServer)()
  end
end

return {
  setup = setup,
}
