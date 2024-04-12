local T = require("qtctest")
local fetch = require("Fetch").fetch
local inspect = require('inspect')
local a = require("async")

local function testFetch()
    local r = a.wait(fetch({ url = "https://dummyjson.com/products", convertToTable = true }))
    local y = a.wait(fetch({ url = "https://dummyjson.com/products", convertToTable = true }))
    T.compare(type(r), "table")
    T.compare(type(y), "table")
end

local function testGoogle()
    local r = a.wait(fetch({ url = "https://www.google.com" }))
    T.compare(r.error, 0)
    print(r)
end

local function fetchTwo()
    local r = a.wait_all {
        fetch({ url = "https://dummyjson.com/products", convertToTable = true }),
        fetch({ url = "https://www.google.com" }),
    }

    T.compare(type(r), "table")
    T.compare(#r, 2)
    T.compare(type(r[1]), "table")
    T.compare(r[2].error, 0)
end

return {
    testFetch = T.sync(testFetch),
    testGoogle = T.sync(testGoogle),
    testFetchTwo = T.sync(fetchTwo)
}
