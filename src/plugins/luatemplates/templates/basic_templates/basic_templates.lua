return {
    Name = "BasicTemplates",
    Version = "1.0.0",
    CompatVersion = "1.0.0",
    Vendor = "The Qt Company",
    Category = "Templates",
    Dependencies = {
        { Name = "LuaTemplates", Version = "13.0.82", Required = true },
    },
    setup = function() require "init".setup() end,
} --[[@as QtcPlugin]]
