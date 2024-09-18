---@meta Project
local project = {}

---@enum RunMode
project.RunMode {
    Normal = "RunConfiguration.NormalRunMode",
    Debug = "RunConfiguration.DebugRunMode",
}

---@class Project
---@field directory FilePath The directory of the project.
project.Project = {}

---Test whether the current startup project can be started using the specified run mode.
---@param runMode RunMode The run mode to test.
---@return boolean True if the current startup project can be started using the specified run mode; otherwise, false.
---@return string|nil If the project cannot be started, a message explaining why; otherwise, nil.
function project.canRunStartupProject(runMode) end

return project
