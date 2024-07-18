---@meta Project

local project = {}

---Returns the project for the given file path or nil.
---@param path FilePath The file path.
function project.forFile(path) end

---@class Project
local Project = {}

---Returns the display name of the project.
function Project:displayName() end

return project
