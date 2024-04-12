local inspect = require('inspect')

local function traceback()
    local result = ""
    local level = 1
    while true do
        local info = debug.getinfo(level, "Sl")
        if not info then break end
        if info.what ~= "C" then
            ---Get the last part of the path in info.source
            local fileName = info.source:match("^.+/(.+)$")
            result = result .. (string.format("  %s:%d\n", fileName, info.currentline))
        end
        level = level + 1
    end
    return result
end

local function compare(actual, expected)
    if (actual == expected) then
        return true
    end

    error("Compared values were not the same.\n  Actual: " ..
        inspect(actual) .. "\n  Expected: " .. inspect(expected) .. "\nTrace:\n" .. traceback())
end

local function sync(func)
    return { func }
end

return {
    compare = compare,
    sync = sync,
}
