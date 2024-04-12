local Utils = require("Utils")
local Action = require("Action")
local a = require("async")


local function script_path()
    local str = debug.getinfo(2, "S").source:sub(2)
    return str
end

local function printResults(results)
    print("Passed:", results.passed)
    print("Failed:", results.failed)
    for index, value in ipairs(results.failedTests) do
        print("Failed test:", value.name, value.error)
    end
end

function RunAsyncTests(asyncTests, results)
    for k, v in pairs(asyncTests) do
        print("* " .. k)
        local status, err = pcall(v)

        if status then
            results.passed = results.passed + 1
        else
            results.failed = results.failed + 1
            table.insert(results.failedTests, { name = k, error = err })
        end
    end

    printResults(results)
end
local function runTests()
    local results = {
        passed = 0,
        failed = 0,
        failedTests = {}
    }

    local tests = Utils.FilePath.fromUserInput(script_path()):parentDir():dirEntries({ nameFilters = { "tst_*.lua" } })

    local asyncTests = {}
    for index, testFile in ipairs(tests) do
        local test, err = loadfile(testFile:nativePath())
        if (test == nil) then
            print("Failed to load test:", testFile, err)
        else
            for k, v in pairs(test()) do
                if type(v) == "table" then
                    asyncTests[testFile:fileName() .. " : " .. k] = v[1]
                else
                    print("* " .. testFile:fileName() .. " : " .. k)
                    local status, err = pcall(v)

                    if status then
                        results.passed = results.passed + 1
                    else
                        results.failed = results.failed + 1
                        table.insert(results.failedTests, { name = testFile:fileName() .. ":" .. k, error = err })
                    end
                end
            end
        end
    end
    
    if next(asyncTests) == nil then
        printResults(results)
        return
    end
    print("Running async tests...")
    a.sync(function() RunAsyncTests(asyncTests, results) end)()
end

local function setup()
    Action.create("LuaTests.run", {
        text = "Run lua tests",
        onTrigger = function() runTests() end,
    })
end

return { setup = setup }
