local function fetchJoke()
    print("Fetching ...")

    local r = qtc.fetch({ url = "https://official-joke-api.appspot.com/random_joke", convertToTable = true })
    if (type(r) == "table") then
        qtc.writeFlashing(r.setup)
        qtc.waitms(1000)
        qtc.writeFlashing(".")
        qtc.waitms(1000)
        qtc.writeFlashing(".")
        qtc.waitms(1000)
        qtc.writeFlashing(".")
        qtc.waitms(1000)
        qtc.writeFlashing(r.punchline)
    else
        qtc.writeFlashing("echo Error fetching: " .. r)
    end
end

local function setup()
    qtc.createAction("Simple.joke", {
        text = "Tell a joke",
        onTrigger = function() coroutine.resume(coroutine.create(fetchJoke)) end,
        defaultKeySequences = { "Meta+Ctrl+Shift+J", "Ctrl+Shift+Alt+J" },
    })
end


return {
    Name = "Joker",
    Version = "1.0.0",
    CompatVersion = "1.0.0",
    Vendor = "The Qt Company",
    Category = "Fun",
    Dependencies = {
        { Name = "Core", Version = "12.0.82", Required = true },
        --    "Not Existing",
        --    "ALso Unknown"
    },
    setup = setup,
} --[[@as QtcPlugin]]
