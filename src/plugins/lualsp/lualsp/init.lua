local inspect = require('inspect')

local function setupClient()
  Client = qtc.createLanguageClient({
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
    qtc.writeFlashing(params.params.text .. ": " .. params.params.tooltip);
  end)
  qtc.writeFlashing("Client: ", Client)
end

local function installServer()
  print("Lua Language Server not found, installing ...")
  local cmds = {
    mac = "brew install lua-language-server",
    win = "winget install lua-language-server",
    linux = "sudo apt install lua-language-server"
  }
  if qtc.runInTerminal(cmds[HostOsInfo.os]) == 0 then
    print("Lua Language Server installed!")
    setupClient()
  else
    print("Lua Language Server installation failed!")
  end
end

local function layoutSettings()
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
  Settings = AspectContainer.create({
    autoApply = false,
    layouter = layoutSettings,
  });

  Settings.enableCopilot = BoolAspect.create({
    settingsKey = "LuaCopilot.EnableCopilot",
    displayName = "Enable Copilot",
    labelText = "Enable Copilot",
    toolTip = "Enables the Copilot integration.",
    defaultValue = false,
    onVolatileValueChanged = function() print("Volatile Value Changed") end,
  })

  Settings.autoComplete = BoolAspect.create({
    settingsKey = "LuaCopilot.AutoComplete",
    displayName = "Auto Complete",
    labelText = "Auto Complete",
    toolTip = "Automatically request suggestions for the current text cursor position after changes to the document.",
    defaultValue = true,
  })

  OptionsPage = qtc.createOptionsPage({
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
  local serverPath = FilePath.fromUserInput("lua-language-server")
  local absolute = serverPath:searchInPath()
  if absolute:exists() == true then
    print("Lua Language Server found!")
    setupClient()
  else
    coroutine.resume(coroutine.create(installServer))
  end
end

return {
  setup = setup,
}
