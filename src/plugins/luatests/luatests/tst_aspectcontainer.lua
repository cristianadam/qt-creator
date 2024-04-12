T = require("qtctest")
local S = require('Settings')

local function testAutoApply()
    local container = S.AspectContainer.create({ autoApply = true })

    container.test = S.BoolAspect.create({ defaultValue = true })
    T.compare(container.test.value, true)
    container.test.volatileValue = false
    T.compare(container.test.volatileValue, false)
    T.compare(container.test.value, false)
end

local function testNoAutoApply()
    local container = S.AspectContainer.create({ autoApply = false })

    container.test = S.BoolAspect.create({ defaultValue = true })
    T.compare(container.test.value, true)
    container.test.volatileValue = false
    T.compare(container.test.volatileValue, false)
    T.compare(container.test.value, true)
    container:apply()
    T.compare(container.test.volatileValue, false)
    T.compare(container.test.value, false)
end

return {
    testAutoApply = testAutoApply,
    testNoAutoApply = testNoAutoApply,
}
