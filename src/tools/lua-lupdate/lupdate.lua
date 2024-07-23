--- luarocks install luafilesystem
local lfs = require "lfs"

function string:endswith(suffix)
    return self:sub(- #suffix) == suffix
end

LUpdatePath = "/Users/mtillmanns/Qt/6.7.2/macos/bin/lupdate"
TmpFiles = {}


local curdir, err = lfs.currentdir()
if not curdir then
    print("Error: " .. err)
    return
end

local folderName = curdir:match("([^/]+)$")
local pluginSpecName = folderName .. ".lua"

--- Noop tr function
function tr(str) return str end

local spec, err = loadfile(pluginSpecName)()

if not spec then
    print("Error: " .. err)
    return
end

if not spec.languages then
    print("Error: No languages specified in plugin spec.")
    return
end

TrContext = spec.Name:gsub("[^a-zA-Z]", "_")

for file in lfs.dir(".") do
    if file ~= "." and file ~= ".." and file:endswith(".lua") and file ~= "lupdate.lua" then
        local f = io.open(file, "r")
        if f then
            local contents = f:read("a")
            local tmpname = os.tmpname()
            local tf = io.open(tmpname, "w")
            if tf then
                tf:write("--- class " .. TrContext .. " { Q_OBJECT; \n")
                tf:write(contents)
                tf:write("--- }\n")
                tf:close()
                table.insert(TmpFiles, tmpname)
            end
        end
    end
end

AllFiles = table.concat(TmpFiles, "\n")
LstFileName = os.tmpname()
local lstFile = io.open(LstFileName, "w")

if lstFile then
    lstFile:write(AllFiles)
    lstFile:close()

    local allLangs = ""
    for _, lang in ipairs(spec.languages) do
        local name = string.lower(folderName) .. "_" .. lang .. ".ts"
        allLangs = allLangs .. name .. " "
    end

    os.execute(LUpdatePath .. " @" .. LstFileName .. " -ts " .. allLangs)

    --- Cleanup
    os.remove(LstFileName)
    for _, file in ipairs(TmpFiles) do
        os.remove(file)
    end
end
