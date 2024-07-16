---@meta Documents

local documents = {}

---@class TextDocument
local TextDocument = {}

---Set a callback that is called when the document is changed.
---@param callback function The callback function. The callback function should not take any arguments.
function TextDocument:setChangedCallback(callback) end

---Returns the file path of the document.
---@return FilePath filePath The file path of the document.
function TextDocument:file() end
return documents
