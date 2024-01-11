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

local function setup(parameters)
  print("Setting up Lua Language Server ...")
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
