---@meta Utils

local utils = {}

---Suspends the current coroutine for the given amount of milliseconds. Call `a.wait` on the returned value to get the result.
---@param ms number The amount of milliseconds to wait
function utils.waitms(ms) end

---Calls the callback after the given amount of milliseconds
---@param ms number The amount of milliseconds to wait
---@param callback function The callback to call
function utils.waitms_cb(ms, callback) end

---@class FilePath
---@field exists boolean True if the path exists
utils.FilePath = {}

---@param path string The path to convert
---@return FilePath The converted path
---Convert and clean a path, returning a FilePath object
function utils.FilePath.fromUserInput(path) end

---@return FilePath The new absolute path
---Searches for the path inside the PATH environment variable
function utils.FilePath:searchInPath() end

---@class (exact) DirEntriesOptions
---@field nameFilters? string[] The name filters to use (e.g. "*.lua"), defaults to all files
---@field fileFilters? integer The filters to use (combination of QDir.Filters.*), defaults to QDir.Filters.NoFilter
---@field flags? integer The iterator flags (combination of QDirIterator.Flags.*), defaults to QDirIterator.Flags.NoIteratorFlags

---Returns all entries in the directory
---@param options DirEntriesOptions
---@return FilePath[]
function utils.FilePath:dirEntries(options) end

---Returns the FilePath as it should be displayed to the user
---@return string
function utils.FilePath:toUserOutput() end

---Returns the path portion of FilePath as a string in the hosts native format
---@return string
function utils.FilePath:nativePath() end

---Returns the last part of the path
---@return string
function utils.FilePath:fileName() end

---Returns the current working path of Qt Creator
---@return FilePath
function utils.FilePath.currentWorkingPath() end

---Returns a new FilePath with the given tail appended
---@param tail string|FilePath The tail to append
---@return FilePath
function utils.FilePath:resolvePath(tail) end

---Returns the parent directory of the path
---@return FilePath
function utils.FilePath:parentDir() end

return utils
