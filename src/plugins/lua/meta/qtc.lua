---@meta

---The global qtc object defined in the Lua plugin.
---@class qtc
qtc = {}

---@class (exact) QtcPlugin
---@field Name string The name of the plugin.
---@field Version string The version of the plugin. (`major.minor.patch`)
---@field CompatVersion string The lowest previous version of the plugin that this one is compatible to. (`major.minor.patch`)
---@field Vendor string The vendor of the plugin.
---@field Category string The category of the plugin.
---@field Dependencies? QtcPluginDependency[] The dependencies of the plugin.
---@field Description? string A short one line description of the plugin.
---@field LongDescription? string A long description of the plugin. Can contain newlines.
---@field Url? string The url of the plugin.
---@field License? string The license text of the plugin.
---@field Revision? string The revision of the plugin.
---@field Copyright? string The copyright of the plugin.
---@field Experimental? boolean Whether the plugin is experimental or not. ( Default: true )
---@field DisabledByDefault? boolean Whether the plugin is disabled by default or not. ( Default: true )
---@field setup function The setup function of the plugin.
QtcPlugin = {}

---@class QtcPluginDependency
---@field Name string The name of the dependency.
---@field Version string The version of the dependency. (`major.minor.patch`)
---@field Required boolean Whether the dependency is required or not.
QtcPluginDependency = {}
---Writes a message to the output pane and flashes it.
function qtc.writeFlashing(...) end

---Creates a new Language Client
---@param options ClientOptions
---@return LanguageClient
function qtc.createLanguageClient(options) end

---Runs a command in a terminal, has to be called from a coroutine!
---@param cmd string The command to run
---@return number The exit code of the command
function qtc.runInTerminal(cmd) end


---A network reply from fetch
---@class QNetworkReply
---@field error int The error code of the reply or 0 if no error
QNetworkReply = {}

---Returns the data of the reply
---@return string
function QNetworkReply:readAll() end

---Fetches a url, has to be called from a coroutine!
---@param options FetchOptions
---@return table|QNetworkReply|string
function qtc.fetch(options) end

---@class FetchOptions
---@field url string The url to fetch
---@field method? string The method to use (GET, POST, ...), default is GET
---@field headers? table The headers to send
---@field body? string The body to send
---@field convertToTable? boolean If true, the resulting data will expect JSON and converted it to a table
FetchOptions = {}


---@enum CommandAttributes
qtc.CommandAttribute = {
    ---Hide the command from the menu
    CA_Hide = 1,
    ---Update the text of the command
    CA_UpdateText = 2,
    ---Update the icon of the command
    CA_UpdateIcon = 4,
    ---The command cannot be configured
    CA_NonConfigurable = 8,
}

---@class ActionOptions
---@field context? string The context in which the action is available
---@field text? string The text to display for the action
---@field iconText? string The icon text to display for the action
---@field toolTip? string The tooltip to display for the action
---@field onTrigger? function The callback to call when the action is triggered
---@field commandAttributes? CommandAttributes The attributes of the action
---@field commandDescription? string The description of the command
---@field defaultKeySequence? string The default key sequence for the action
---@field defaultKeySequences? string[] The default key sequences for the action
ActionOptions = {}

---Creates a new Action
---@param id string The id of the action
---@param options ActionOptions
function qtc.createAction(id, options) end

---Suspends the current coroutine for the given amount of milliseconds
---@param ms number The amount of milliseconds to wait
function qtc.waitms(ms) end

---@class ClientOptions
---@field name string The name under which to register the language server.
---@field cmd string[] The command to start the language server
---@field transport? "stdio"|"localsocket" Defaults to stdio
---@field languageFilter LanguageFilter The language filter deciding which files to open with the language server
---@field startBehavior? "AlwaysOn"|"RequiresFile"|"RequiresProject"
ClientOptions = {}

---@class LanguageFilter
---@field patterns? string[] The file patterns supported by the language server
---@field mimeTypes? string[] The mime types supported by the language server
LanguageFilter = {}

---@class LanguageClient
---@field on_instance_start function The callback to call when a language client starts
LanguageClient = {}

---@param msg string The name of the message to handle
---@param callback function The callback to call when the message is received
---Registers a message handler for the message named 'msg'
function LanguageClient:registerMessage(msg, callback) end

---@class HostOsInfo
---@field isMacHost function Returns true if the host is a Mac
---@field isWindowsHost function Returns true if the host is Windows
---@field isLinuxHost function Returns true if the host is Linux
---@field os string The name of the host OS (mac, win, linux)
HostOsInfo = {}

---@class FilePath
---@field exists boolean True if the path exists
FilePath = {}

---@param path string The path to convert
---@return FilePath The converted path
---Convert and clean a path, returning a FilePath object
function FilePath.fromUserInput(path) end

---@return FilePath The new absolute path
---Searches for the path inside the PATH environment variable
function FilePath:searchInPath() end
