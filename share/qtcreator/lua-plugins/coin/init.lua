-- Copyright (C) 2026 The Qt Company Ltd.
-- SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

local a = require("async")
local fetch = require("Fetch").fetch
local Action = require("Action")
local Menu = require("Menu")
local Device = require("Device")
local Json = require("Json")
local Utils = require("Utils")
local S = require("Settings")
local Gui = require("Gui")
local Mode = require("Mode")

-- Fixed Coin API bits (not user-configurable).
local SCHEDULE_ENDPOINT = "/api/integrationRequest/json"
local CONFIGURATIONS_ENDPOINT = "/api/configurations/json"
-- Coin writes this marker into a parked debug VM's (gzipped) work-item log.
local DEBUG_MARKER = "PrepareVmForDebugging"

-- All user configuration lives in a Tools > Options > Coin page, backed by this
-- aspect container (built in setupAspect). Read values via Settings.<x>.value.
-- Env vars (COIN_USER, COIN_BASE, ...) survive as the aspect defaults, so an
-- env-driven headless launch keeps working until a value is saved in the UI.
local Settings
local optionsPage

-- Forward declaration: OpenNebula-based VM resolution (defined with the XML-RPC
-- helpers below) is used by the earlier waitForVm.
local resolveDebugVm

local function urlencode(s)
    return (s:gsub("[^%w]", function(c)
        return string.format("%%%02X", string.byte(c))
    end))
end

local function userCookie()
    local info = '{"username":"' .. Settings.user.value
        .. '","accessToken":"' .. Settings.accessToken.value .. '"}'
    return "userInfo=" .. urlencode(info)
end

-- Fetch all schedulable configurations (the scheduler's "List Configurations").
-- The reply is a Thrift-JSON message [1,"",0,0,{"1":{"lst":{"rec",N,rec,...}}}];
-- an empty configurations list in the request means "list them all".
-- Returns byId (config name -> record) and a sorted list of names.
local function fetchConfigs()
    local req = '{"3":{"rec":{"1":{"str":"qt-project"},"2":{"str":"qt/qtbase"},'
        .. '"3":{"str":""}}},"5":{"str":"dev"},"6":{"str":"dev"},"11":{"lst":["rec",0]}}'
    local r = a.wait(fetch({
        url = Settings.baseUrl.value .. CONFIGURATIONS_ENDPOINT,
        method = "POST",
        headers = { ["Content-Type"] = "text/plain", ["Cookie"] = userCookie() },
        body = req,
        convertToTable = true,
        timeout = 30000,
    }))
    if type(r) == "string" then
        error("list configurations failed: " .. r)
    end
    local lst = r[5] and r[5]["1"] and r[5]["1"]["lst"]  -- { "rec", N, rec, ... }
    if type(lst) ~= "table" then
        error("unexpected configurations response")
    end
    local byId, names = {}, {}
    for i = 3, #lst do
        local rec = lst[i]
        if type(rec) == "table" and rec["33"] then
            local name = rec["33"].str
            byId[name] = rec
            names[#names + 1] = name
        end
    end
    table.sort(names)
    return byId, names
end

-- Resolve `configName` plus its host-build dependency into the ordered config
-- list the integration request expects. Works for any platform, not just QNX.
local function resolveScheduleConfigs(configName)
    local byId = fetchConfigs()
    local target = byId[configName]
    if not target then
        error("config '" .. configName .. "' not found in resolved configurations")
    end
    local configs = {}
    local dep = target["34"] and target["34"].rec and target["34"].rec["33"]
    local depId = dep and dep.str
    if depId and byId[depId] then
        configs[#configs + 1] = byId[depId]   -- host-build dependency first
    end
    configs[#configs + 1] = target
    return configs
end

-- Build field 11's { "rec", N, rec... } list from a sequence of config records.
local function configsList(configs)
    local lst = { "rec", #configs }
    for _, c in ipairs(configs) do
        lst[#lst + 1] = c
    end
    return lst
end

-- Build the schedule request around the freshly resolved config and POST it.
-- Returns the client task id.
local function scheduleResolved(configName)
    if Settings.user.value == "" then
        error("set your Coin/AD user name in Tools > Options > Coin")
    end
    local configs = resolveScheduleConfigs(configName)
    local taskId = string.format("web_qt_qtbase_%d", os.time() * 1000)
    local req = {
        ["1"] = { i32 = 2 },   -- IntegrationRequestType.PrepareVmForDebugging
        ["2"] = { str = taskId },
        ["3"] = { rec = { ["1"] = { str = "qt-project" }, ["2"] = { str = "qt/qtbase" },
                          ["3"] = { str = "" } } },
        ["5"] = { str = "dev" },
        ["6"] = { str = "dev" },
        ["7"] = { rec = { ["1"] = { str = "qt-project" }, ["2"] = { str = "qt/qt5" },
                          ["3"] = { str = "dev" } } },
        ["9"] = { i32 = 0 }, ["12"] = { tf = 0 }, ["14"] = { i32 = 0 },
        ["15"] = { tf = 1 }, ["16"] = { str = "" }, ["17"] = { tf = 0 },
        ["19"] = { tf = 0 }, ["20"] = { tf = 0 }, ["23"] = { tf = 0 }, ["26"] = { i32 = 2 },
        ["11"] = { lst = configsList(configs) },
        ["13"] = { str = Settings.user.value },
        ["25"] = { i32 = 2 },  -- DebugVmType.PreBuild
    }
    local reply = a.wait(fetch({
        url = Settings.baseUrl.value .. SCHEDULE_ENDPOINT,
        method = "POST",
        headers = { ["Content-Type"] = "text/plain", ["Cookie"] = userCookie() },
        body = Json.encode(req),
        -- Scheduling does real server-side work; allow well over a read's time.
        timeout = 60000,
    }))
    if type(reply) == "string" then
        error("schedule POST failed: " .. reply)
    end
    if reply.error ~= 0 then
        error("schedule POST failed, error " .. tostring(reply.error))
    end
    return taskId
end

-- Read-only: list the recent CreateDebugVm tasks (exercises Fetch GET + JSON).
-- NOTE: the first fetch from a Lua plugin pops a network-permission info bar
-- ("Allow the extension ... to fetch"); click "Always Allow" once, or pre-seed
-- [Lua.Fetch] pluginsAllowedToFetch=Coin in the settings. An un-granted fetch
-- hangs the awaiting coroutine indefinitely (no timeout) -- a Lua-API rough edge.
local function listTasks()
    local r = a.wait(fetch({
        url = Settings.baseUrl.value .. "/api/tasks?type=CreateDebugVm&max=20",
        convertToTable = true,
        timeout = 15000,
    }))
    if type(r) == "string" then
        print("Coin: fetch failed: " .. r)
        return
    end
    local tasks = (type(r) == "table" and r.tasks) or {}
    if #tasks == 0 then
        print("Coin: no CreateDebugVm tasks found")
        return
    end
    print("Coin: recent debug-VM tasks:")
    for _, t in ipairs(tasks) do
        print(string.format("  %-38s %-10s %s", t.id or "?", t.state or "?",
                            t.project or "?"))
    end
end

-- Pull the VM host's IP out of a work-item log. Coin writes an "IPADDRESS="
-- marker; fall back to a looser "IP address" form and finally to any internal
-- 10.2xx address (mirrors coin-vm's IP_PATTERNS).
local function extractIp(text)
    return text:match("IPADDRESS[=:%s]+(%d+%.%d+%.%d+%.%d+)")
        or text:match("IP%s*[Aa]ddress[=:%s]+(%d+%.%d+%.%d+%.%d+)")
        or text:match("(10%.2%d%d%.%d+%.%d+)")
end

-- Return a task's work items as best-first { ident, log } pairs (those with a
-- raw log). Prefer an identifier matching `match` (e.g. "qnx", to pick the QNX
-- SDP VM over the host-build VM) and a "/debugVM/" log path -- same ordering as
-- coin-vm's find_vm.
local function workItemLogs(taskId, match)
    local r = a.wait(fetch({
        url = Settings.baseUrl.value .. "/api/taskWorkItems?id=" .. urlencode(taskId),
        convertToTable = true,
        timeout = 20000,
    }))
    if type(r) == "string" then
        error("taskWorkItems fetch failed: " .. r)
    end
    local logs = {}
    for _, t in ipairs(r.tasks_with_workitems or {}) do
        for _, w in ipairs(t.workitems or {}) do
            local lg = w.storage_paths and w.storage_paths.log_raw
            if lg then
                logs[#logs + 1] = { ident = w.identifier or "", log = lg }
            end
        end
    end
    local function rank(p)
        local m = (match and p.ident:lower():find(match:lower(), 1, true)) and 0 or 2
        local d = p.log:find("/debugVM/", 1, true) and 0 or 1
        return m + d
    end
    table.sort(logs, function(x, y) return rank(x) < rank(y) end)
    return logs
end

-- Fetch a work-item log (raw bytes, no convertToTable) and inflate it. The
-- Fetch reply is a QNetworkReply whose :readAll() preserves binary bytes.
local function fetchLog(logPath)
    local reply = a.wait(fetch({ url = Settings.logHost.value .. logPath, timeout = 30000 }))
    if type(reply) == "string" then
        error("log fetch failed: " .. reply)
    end
    if reply.error ~= 0 then
        error("log fetch failed, error " .. tostring(reply.error))
    end
    local blob = reply:readAll()
    local ok, text = pcall(Utils.gunzip, blob)
    if ok then
        return text
    end
    return blob   -- not gzipped
end

-- Resolve a parked debug VM's IP from a task's work-item logs, natively (Fetch
-- + Utils.gunzip) -- replaces `coin-vm ip`. Returns the IP or nil.
local function findVmIp(taskId, match)
    for _, p in ipairs(workItemLogs(taskId, match)) do
        local ok, text = pcall(fetchLog, p.log)
        if ok then
            local ip = extractIp(text)
            local isDebug = text:find(DEBUG_MARKER, 1, true)
                or p.log:find("/debugVM/", 1, true)
            if ip and isDebug then
                return ip
            end
        end
    end
    return nil
end

-- Poll until the debug VM has an IP and accepts ssh, then return it. Prefer the
-- OpenNebula GUEST_IP (reliable) and fall back to the work-item log's IPADDRESS
-- marker (which is sometimes never written). Gate on a TCP probe, since an IP
-- can appear before the VM actually accepts ssh -- replaces `coin-vm wait`.
local function waitForVm(taskId, timeoutS)
    local deadline = os.time() + (timeoutS or 1800)
    while os.time() < deadline do
        local ok, _, rip = pcall(resolveDebugVm)
        local ip = (ok and rip) or findVmIp(taskId)
        if ip and a.wait(Device.isReachable(ip, 22, 4000)) then
            return ip
        end
        a.wait(Utils.waitms(15000))
    end
    return nil
end

-- Id of the most recent CreateDebugVm task (best-effort resolve for "register
-- the running VM" without an explicit task id; findVmIp then picks its QNX VM).
local function recentDebugVmTask()
    local r = a.wait(fetch({
        url = Settings.baseUrl.value .. "/api/tasks?type=CreateDebugVm&max=20",
        convertToTable = true,
        timeout = 15000,
    }))
    local tasks = (type(r) == "table" and r.tasks) or {}
    return tasks[1] and tasks[1].id or nil
end

-- OpenNebula XML-RPC (native over Fetch, what coin-vm does with a library). Used
-- both to resolve a parked debug VM's IP/id (more reliable than the work-item
-- log) and to terminate it -- there is no Coin HTTP API for either. XML-RPC is a
-- plain HTTP POST, so we build the request and parse the reply by hand. Auth is
-- a real "user:token" (with a TTL).
local function xmlEscape(s)
    return (s:gsub("[&<>\"']", {
        ["&"] = "&amp;", ["<"] = "&lt;", [">"] = "&gt;",
        ['"'] = "&quot;", ["'"] = "&apos;",
    }))
end

local function xmlUnescape(s)
    s = s:gsub("&lt;", "<"):gsub("&gt;", ">"):gsub("&quot;", '"'):gsub("&apos;", "'")
    return (s:gsub("&amp;", "&"))
end

-- The OpenNebula session string ("user:token"): the settings field, else the
-- standard ~/.one/one_auth file (same as the ONE CLI / coin-vm).
local function oneAuthString()
    local s = Settings.oneAuth.value
    if s and s ~= "" then
        return s
    end
    local f = io.open((os.getenv("HOME") or "") .. "/.one/one_auth", "r")
    if f then
        s = f:read("*l")
        f:close()
        if s and s ~= "" then
            return (s:gsub("%s+$", ""))
        end
    end
    error("no OpenNebula token: set it in Tools > Options > Coin, or ~/.one/one_auth")
end

-- POST an XML-RPC methodCall and return the raw response text. `params` is an
-- ordered list of { s = "..." } (string) or { i = N } (int) values.
local function xmlrpcCall(method, params)
    local parts = { '<?xml version="1.0"?><methodCall><methodName>', method,
                    '</methodName><params>' }
    for _, p in ipairs(params) do
        local v = p.i and ("<i4>" .. tostring(p.i) .. "</i4>")
            or ("<string>" .. xmlEscape(p.s) .. "</string>")
        parts[#parts + 1] = "<param><value>" .. v .. "</value></param>"
    end
    parts[#parts + 1] = "</params></methodCall>"
    local reply = a.wait(fetch({
        url = Settings.oneXmlrpc.value,
        method = "POST",
        headers = { ["Content-Type"] = "text/xml" },
        body = table.concat(parts),
        timeout = 20000,
    }))
    if type(reply) == "string" then
        error("OpenNebula XML-RPC fetch failed: " .. reply)
    end
    if reply.error ~= 0 then
        error("OpenNebula XML-RPC HTTP error " .. tostring(reply.error))
    end
    local text = reply:readAll()
    if text:find("<fault>", 1, true) then
        error("OpenNebula fault: " .. (text:match("faultString.-<string>(.-)</string>") or "?"))
    end
    return text
end

-- The OpenNebula result document (a VM_POOL / VM) is returned as an XML-escaped
-- string inside the XML-RPC result array; extract and unescape it.
local function xmlrpcDoc(resp)
    if resp:match("<boolean>(%d)</boolean>") ~= "1" then
        error("OpenNebula call returned failure")
    end
    return xmlUnescape(resp:match("<string>(.-)</string>") or "")
end

-- one.vmpool.info(session, -3=mine, -1, -1, -1=all not-DONE) -> my VMs as
-- { id, name }.
local function myVms()
    local pool = xmlrpcDoc(xmlrpcCall("one.vmpool.info",
        { { s = oneAuthString() }, { i = -3 }, { i = -1 }, { i = -1 }, { i = -1 } }))
    local vms = {}
    for block in pool:gmatch("<VM>(.-)</VM>") do
        vms[#vms + 1] = {
            id = block:match("<ID>(%d+)</ID>"),
            name = block:match("<NAME>(.-)</NAME>"),
        }
    end
    return vms
end

-- Read a VM's monitored GUEST_IP (its reachable ssh address) from OpenNebula.
-- Reliable, unlike the work-item log's IPADDRESS marker. nil if not set yet.
local function vmGuestIp(id)
    local doc = xmlrpcDoc(xmlrpcCall("one.vm.info",
        { { s = oneAuthString() }, { i = tonumber(id) } }))
    local ip = doc:match("<GUEST_IP><!%[CDATA%[(.-)%]%]></GUEST_IP>")
        or doc:match("<GUEST_IP>(.-)</GUEST_IP>")
    if ip == "" then
        ip = nil
    end
    return ip
end

-- Resolve my most recent debug VM (any platform, not just QNX) straight from
-- OpenNebula, and read its GUEST_IP. Returns id, ip. nil, nil if none.
resolveDebugVm = function()
    local best
    for _, vm in ipairs(myVms()) do
        local n = (vm.name or ""):lower()
        if vm.id and n:find("debugvm", 1, true) then
            if not best or tonumber(vm.id) > tonumber(best.id) then
                best = vm
            end
        end
    end
    if not best then
        return nil, nil
    end
    return best.id, vmGuestIp(best.id)
end

-- Terminate my most recent parked debug VM (any platform), found directly in
-- can only ever destroy one of my own VMs, and does not depend on the flaky
-- work-item log). Honors the dry-run setting.
local function terminateVm()
    local id, ip = resolveDebugVm()
    if not id then
        local vms = myVms()
        if #vms == 0 then
            print("Coin: you have no running OpenNebula VMs -- nothing to terminate")
        else
            print("Coin: no debug VM among my VMs:")
            for _, vm in ipairs(vms) do
                print(string.format("  id %-8s %s", vm.id or "?", vm.name or "?"))
            end
        end
        return
    end
    print("Coin: debug VM is OpenNebula id " .. id .. (ip and (" (" .. ip .. ")") or ""))
    if Settings.dryRunTerminate.value then
        print("Coin: DRY RUN -- would terminate VM id " .. id
            .. ". Uncheck 'Dry run terminate' in settings to do it.")
        return
    end
    print("Coin: terminating VM id " .. id .. " ...")
    local resp = xmlrpcCall("one.vm.action",
        { { s = oneAuthString() }, { s = "terminate-hard" }, { i = tonumber(id) } })
    if resp:match("<boolean>(%d)</boolean>") == "1" then
        print("Coin: terminated VM id " .. id)
    else
        print("Coin: terminate returned failure for VM id " .. id)
    end
end

-- Registers the VM at `ip` as a Remote Linux build device and runs tool
-- auto-detection. With the QNX device-detection refactor this yields the QNX
-- toolchains/debuggers and a QNX kit per SDP target -- the payoff of the whole
-- exercise. Returns the device id.
local function registerVm(ip)
    local name = "QNX Debug VM (" .. ip .. ")"
    local params = {
        type = "GenericLinuxOsType",
        displayName = name,
        host = ip,
        port = 22,
        userName = Settings.vmUser.value,
        hostKeyCheckingMode = "none",
    }
    local privateKey = Settings.privateKey.value
    if privateKey and privateKey ~= "" then
        params.privateKeyFile = privateKey
        params.useKeyFile = true
    end

    local id = Device.createDevice(params)
    print("Coin: registered device " .. id .. " (" .. name .. ")")
    -- Bootstrap key access so tool detection can ssh in. deployPublicKey prompts
    -- for the shared VM password via the IDE askpass helper if the key is not yet
    -- authorized; if it already is, ssh authenticates by key and this is a no-op.
    local pubKey = Settings.publicKey.value
    print("Coin: deploying public key " .. pubKey .. " (enter the VM password if prompted) ...")
    local deployed = a.wait(Device.deployPublicKey(id, pubKey))
    if type(deployed) == "string" then
        print("Coin: key deployment failed: " .. deployed .. " (continuing anyway)")
    end
    print("Coin: connecting and detecting toolchains/kits (can take a while) ...")
    local kits = a.wait(Device.detectTools(id))
    if type(kits) == "string" then
        print("Coin: tool detection failed: " .. kits)
        return id
    end
    if #kits == 0 then
        print("Coin: no kits produced (is the QNX SDP present on the VM?)")
    else
        print("Coin: kits for this device:")
        for _, k in ipairs(kits) do
            print(string.format("  %-42s %s", k.name, k.valid and "[valid]" or "[invalid]"))
        end
    end
    return id
end

-- Registers an already-running VM without scheduling a new one. Uses
-- COIN_VM_IP if set, otherwise resolves the most recent debug VM's QNX IP
-- natively from its work-item logs.
local function registerRunning()
    local ip = os.getenv("COIN_VM_IP")
    if not ip or ip == "" then
        local ok, _, rip = pcall(resolveDebugVm)   -- OpenNebula GUEST_IP (reliable)
        ip = (ok and rip) or nil
    end
    if not ip or ip == "" then
        local taskId = recentDebugVmTask()          -- fall back to the work-item log
        ip = taskId and findVmIp(taskId) or nil
    end
    if not ip or not ip:match("^%d+%.%d+%.%d+%.%d+$") then
        print("Coin: no running VM IP (set COIN_VM_IP or use Bring up)")
        return
    end
    registerVm(ip)
end

local function bringUp(configName)
    configName = configName or Settings.config.value
    print("Coin: scheduling a debug VM (" .. configName .. ") as "
        .. Settings.user.value .. " ...")
    local ok, taskId = pcall(scheduleResolved, configName)
    if not ok then
        print("Coin: " .. tostring(taskId))
        return
    end
    print("Coin: scheduled " .. taskId ..
          " -- waiting until it accepts ssh (can take a while) ...")
    -- The IP lives in a gzipped work-item log; resolve it natively and gate on
    -- a TCP probe until the VM actually accepts ssh.
    local ip = waitForVm(taskId)
    if not ip then
        print("Coin: gave up waiting for a reachable VM. Try the workspace `Fetch`.")
        return
    end
    print("Coin: VM up at " .. ip .. " -- registering as a device ...")
    registerVm(ip)
end

-- Pull a module's names into the environment ("using namespace"), as the other
-- Lua plugins do, so Gui's Form/br/st can be used unqualified below.
local function using(tbl)
    local result = _G
    for k, v in pairs(tbl) do result[k] = v end
    return result
end

-- Lay the aspects out as a labelled form on the options page.
local function layoutSettings()
    local _ENV = using(Gui)
    return Form {
        Settings.user, br,
        Settings.accessToken, br,
        Settings.baseUrl, br,
        Settings.config, br,
        Settings.logHost, br,
        Settings.vmUser, br,
        Settings.publicKey, br,
        Settings.privateKey, br,
        Settings.oneXmlrpc, br,
        Settings.oneAuth, br,
        Settings.dryRunTerminate, br,
        st,
    }
end

-- Build the Tools > Options > Coin page. Each aspect defaults to its former env
-- var (or the previous hard-coded value), so nothing changes until a value is
-- saved in the UI. autoApply=false is required by OptionsPage.
local function setupAspect()
    Settings = S.AspectContainer.create({
        autoApply = false,
        settingsGroup = "Coin",
        layouter = layoutSettings,
    })

    Settings.user = S.StringAspect.create({
        settingsKey = "Coin.User",
        labelText = "Coin/AD user name:",
        toolTip = "Your Coin (AD/LDAP) user name; scheduling requires it.",
        defaultValue = os.getenv("COIN_USER") or "",
    })
    Settings.accessToken = S.StringAspect.create({
        settingsKey = "Coin.AccessToken",
        labelText = "Access token:",
        toolTip = "Coin's honor-system login token (the literal the web UI sets).",
        displayStyle = S.StringDisplayStyle.PasswordLineEdit,
        defaultValue = "a token",
    })
    Settings.baseUrl = S.StringAspect.create({
        settingsKey = "Coin.BaseUrl",
        labelText = "Coin base URL:",
        toolTip = "Base URL of the Coin instance.",
        defaultValue = os.getenv("COIN_BASE") or "https://coin.ci.qt.io/coin",
    })
    Settings.config = S.StringAspect.create({
        settingsKey = "Coin.Config",
        labelText = "Platform config:",
        toolTip = "The Coin platform configuration to schedule (a QNX SDP host).",
        defaultValue = os.getenv("COIN_CONFIG") or "qnx-710-x86_64-developer-build-on-linux",
    })
    Settings.logHost = S.StringAspect.create({
        settingsKey = "Coin.LogHost",
        labelText = "Log host:",
        toolTip = "Host serving work-item logs (log_raw), where the VM IP is read.",
        defaultValue = os.getenv("COIN_LOG_HOST") or "https://testresults.qt.io",
    })
    Settings.vmUser = S.StringAspect.create({
        settingsKey = "Coin.VmUser",
        labelText = "VM login user:",
        toolTip = "SSH login on the Linux VM host that carries the SDP.",
        defaultValue = os.getenv("COIN_VM_USER") or "qt",
    })
    Settings.publicKey = S.FilePathAspect.create({
        settingsKey = "Coin.PublicKey",
        labelText = "Public key to deploy:",
        toolTip = "Public key appended to the VM's authorized_keys to bootstrap access.",
        expectedKind = S.Kind.File,
        defaultPath = Utils.FilePath.fromUserInput(
            os.getenv("COIN_PUBKEY") or ((os.getenv("HOME") or "") .. "/.ssh/id_ecdsa.pub")),
    })
    Settings.privateKey = S.FilePathAspect.create({
        settingsKey = "Coin.PrivateKey",
        labelText = "Private key (optional):",
        toolTip = "If set, the device authenticates with this key only; otherwise ssh's "
            .. "default key search (agent + ~/.ssh/id_*) is used.",
        expectedKind = S.Kind.File,
        defaultPath = Utils.FilePath.fromUserInput(os.getenv("COIN_SSH_KEY") or ""),
    })
    Settings.oneXmlrpc = S.StringAspect.create({
        settingsKey = "Coin.OneXmlrpc",
        labelText = "OpenNebula XML-RPC:",
        toolTip = "OpenNebula XML-RPC endpoint, used to terminate a parked VM.",
        defaultValue = os.getenv("ONE_XMLRPC") or "http://opennebula01.on1.qt.io:2633/RPC2",
    })
    Settings.oneAuth = S.StringAspect.create({
        settingsKey = "Coin.OneAuth",
        labelText = "OpenNebula token:",
        toolTip = "OpenNebula 'user:token' credential (has a TTL). If empty, "
            .. "~/.one/one_auth is used.",
        displayStyle = S.StringDisplayStyle.PasswordLineEdit,
        defaultValue = os.getenv("ONE_AUTH") or "",
    })
    Settings.dryRunTerminate = S.BoolAspect.create({
        settingsKey = "Coin.DryRunTerminate",
        labelText = "Dry run terminate (list only):",
        toolTip = "When set, Terminate only reports the VM it would destroy.",
        defaultValue = true,
        labelPlacement = S.LabelPlacement.InExtraLabel,
    })

    optionsPage = S.OptionsPage.create({
        id = "General",
        displayName = "General",
        categoryId = "Coin",
        displayCategory = "Coin",
        aspectContainer = Settings,
    })
end

-- TEMP: live repro of QTCREATORBUG-30629 with the "Access via" fixes in place.
-- Registers the VM host, then a QNX device for the qemu guest reached WITH
-- Access via=host. With the fix the connection should jump through the host.
local function repro30629()
    local hostIp = os.getenv("COIN_VM_IP")
    if not hostIp or hostIp == "" then
        local ok, _, rip = pcall(resolveDebugVm)
        hostIp = (ok and rip) or nil
    end
    if not hostIp then
        print("Coin: [repro] no VM host IP")
        return
    end
    print("Coin: [repro] VM host = " .. hostIp)
    local hostId = Device.createDevice({
        type = "GenericLinuxOsType", displayName = "Coin VM host (" .. hostIp .. ")",
        host = hostIp, port = 22, userName = Settings.vmUser.value, hostKeyCheckingMode = "none",
    })
    a.wait(Device.deployPublicKey(hostId, Settings.publicKey.value))
    local qnxIp = os.getenv("COIN_QNX_IP") or "172.31.1.10"
    local qnxUser = os.getenv("COIN_QNX_USER") or "root"
    local id = Device.createDevice({
        type = "QnxOsType", displayName = "QNX via " .. hostIp, host = qnxIp, port = 22,
        userName = qnxUser, hostKeyCheckingMode = "none", linkDevice = hostId,
    })
    print("Coin: [repro] testing QNX " .. qnxIp .. " (user " .. qnxUser
        .. ") via Access via=" .. hostId .. " ...")
    local r = a.wait(Device.detectTools(id))
    print("Coin: [repro] result => "
        .. (type(r) == "string" and r or ("CONNECTED, " .. #r .. " kits")))
end

-- The Coin "workspace" as a mode (a left-side tab): lists my OpenNebula VMs and
-- exposes the common Coin actions as buttons. Created once at setup; the first
-- user of the Gui.ListWidget and Mode bindings.
local coinMode

local function setupWorkspaceMode()
    local _ENV = using(Gui)

    local list = ListWidget {}
    local configList = ListWidget {}
    local status = Label { text = 'Use "List configurations" or "Fetch" to begin.' }
    local vmRows = {}
    local configNames = {}
    local fetchButton

    -- A read-only TextEdit, not a MarkdownBrowser: the latter sizes to its
    -- content (heightForWidth + a greedy minimum) and fights a QSplitter,
    -- which makes the handle drag inverted/erratic. Plain text behaves.
    local workflow = TextEdit {
        readOnly = true,
        text = [[
Coin workflow

Bring up a new VM:
1. List configurations - fetch the available platform configs.
2. Select a configuration (QNX, Linux, ...), then Bring up VM to
   schedule a debug VM for it. This takes a few minutes; watch
   General Messages for progress.

Use an existing VM:
3. Fetch - list your running Coin VMs.
4. Select a VM, then:
     - Register as device - add it as a Remote Linux build device
       and auto-detect its kits.
     - Terminate selected - shut the VM down.

Credentials (Coin token, OpenNebula auth, SSH keys) are set in
Tools > Options > Coin.
]],
    }

    local function refresh()
        status.text = "Fetching VMs ..."
        local ok, vms = pcall(myVms)
        if not ok then
            vmRows = {}
            list:setItems({})
            status.text = "Error: " .. tostring(vms)
            return
        end
        vmRows = vms
        local items = {}
        for _, vm in ipairs(vms) do
            items[#items + 1] = string.format("%-8s  %s", vm.id or "?", vm.name or "?")
        end
        list:setItems(items)
        status.text = (#vms == 0) and "No running VMs."
            or (#vms .. " VM(s). Select one, then use the buttons below.")
    end

    -- Explicit fetch: disable the button for the duration of the (blocking)
    -- OpenNebula round-trip so it cannot be retriggered, and re-enable it after.
    -- refresh() shows the progress in the status line.
    local function fetchNow()
        fetchButton.enabled = false
        local ok, err = pcall(refresh)
        if not ok then
            status.text = "Error: " .. tostring(err)
            print("Coin: " .. tostring(err))
        end
        fetchButton.enabled = true
    end

    local function terminateSelected()
        local row = list:currentRow()
        local vm = row >= 0 and vmRows[row + 1] or nil
        if not vm then
            status.text = "Select a VM to terminate first."
            return
        end
        if Settings.dryRunTerminate.value then
            status.text = "DRY RUN: would terminate id " .. vm.id
                .. " (uncheck 'Dry run terminate' in Settings)."
            print("Coin: DRY RUN -- would terminate VM id " .. vm.id)
            return
        end
        status.text = "Terminating id " .. vm.id .. " ..."
        local resp = xmlrpcCall("one.vm.action",
            { { s = oneAuthString() }, { s = "terminate-hard" }, { i = tonumber(vm.id) } })
        if resp:match("<boolean>(%d)</boolean>") == "1" then
            print("Coin: terminated VM id " .. vm.id)
        else
            print("Coin: terminate returned failure for id " .. vm.id)
        end
        refresh()
    end

    -- Register the selected VM as a build device by its own GUEST_IP. For a
    -- non-QNX debug VM (a plain Linux host) this reaches it directly; the QNX
    -- "Access via" nesting is only needed for the nested qemu guest.
    local function registerSelected()
        local row = list:currentRow()
        local vm = row >= 0 and vmRows[row + 1] or nil
        if not vm then
            status.text = "Select a VM to register first."
            return
        end
        local ip = vmGuestIp(vm.id)
        if not ip then
            error("VM id " .. vm.id .. " has no GUEST_IP yet (still booting?)")
        end
        status.text = "Registering id " .. vm.id .. " (" .. ip .. ") ..."
        registerVm(ip)
        refresh()
    end

    -- Wrap a handler so it runs in a coroutine (the VM ops use a.wait), shows a
    -- status line, and reports errors instead of throwing out of the callback.
    local function run(msg, fn)
        return function()
            a.sync(function()
                status.text = msg
                local ok, err = pcall(fn)
                if not ok then
                    status.text = "Error: " .. tostring(err)
                    print("Coin: " .. tostring(err))
                end
            end)()
        end
    end

    -- List the available platform configurations (not just QNX) so the user can
    -- pick which one to bring up a debug VM for.
    local function listConfigs()
        status.text = "Listing configurations ..."
        local _, names = fetchConfigs()
        configNames = names
        configList:setItems(names)
        status.text = (#names == 0) and "No configurations returned."
            or (#names .. " configurations. Select one, then Bring up VM.")
    end

    -- Schedule + register a VM for the selected configuration (or the default
    -- from Settings.config if none is selected).
    local function bringUpSelected()
        local row = configList:currentRow()
        local cfg = row >= 0 and configNames[row + 1] or nil
        bringUp(cfg)
        refresh()
    end

    fetchButton = PushButton {
        text = "Fetch",
        onClicked = function() a.sync(fetchNow)() end,
    }

    coinMode = Mode.create {
        id = "Coin.Mode",
        displayName = "Coin",
        priority = 10,
        widget = Widget {
            Column {
                status,
                Splitter {
                    orientation = "horizontal",
                    stretchFactors = { 2, 2, 1 },
                    -- Each pane is a Widget (a QSplitter takes widgets, not
                    -- layouts), so wrap the labelled lists in Widget { Column }.
                    Widget { Column { Label { text = "Debug VMs" }, list } },
                    Widget { Column { Label { text = "Configurations" }, configList } },
                    workflow,
                },
                Row {
                    PushButton {
                        text = "List configurations",
                        onClicked = run("Listing configurations ...", listConfigs),
                    },
                    PushButton {
                        text = "Bring up VM",
                        onClicked = run("Scheduling a VM (see General Messages) ...",
                            bringUpSelected),
                    },
                    st,
                    fetchButton,
                    PushButton {
                        text = "Register as device",
                        onClicked = run("Registering and detecting (see General Messages) ...",
                            registerSelected),
                    },
                    PushButton {
                        text = "Terminate selected",
                        onClicked = run("Working ...", terminateSelected),
                    },
                },
            },
        },
    }
    -- No auto-fetch: the initial load is user-triggered via the Fetch button.
end

local function setup()
    print("Coin: Lua plugin loaded")
    setupAspect()
    setupWorkspaceMode()

    Menu.create("Coin.Menu", {
        title = "Coin Debug VM",
        containers = { "QtCreator.Menu.Tools" },
    })

    Action.create("Coin.Workspace", {
        text = "Show workspace",
        containers = { "Coin.Menu" },
        onTrigger = function() if coinMode then coinMode:activate() end end,
    })

    Action.create("Coin.List", {
        text = "List debug VMs",
        containers = { "Coin.Menu" },
        onTrigger = function() a.sync(listTasks)() end,
    })

    Action.create("Coin.Up", {
        text = "Bring up debug VM",
        containers = { "Coin.Menu" },
        onTrigger = function() a.sync(bringUp)() end,
    })

    Action.create("Coin.Register", {
        text = "Register running VM as device",
        containers = { "Coin.Menu" },
        onTrigger = function() a.sync(registerRunning)() end,
    })

    Action.create("Coin.Terminate", {
        text = "Terminate debug VM",
        containers = { "Coin.Menu" },
        onTrigger = function()
            a.sync(function()
                local ok, err = pcall(terminateVm)
                if not ok then
                    print("Coin: " .. tostring(err))
                end
            end)()
        end,
    })

    Action.create("Coin.Settings", {
        text = "Settings...",
        containers = { "Coin.Menu" },
        onTrigger = function() optionsPage:show() end,
    })

    Action.create("Coin.Repro30629", {
        text = "TEMP: live repro QTCREATORBUG-30629",
        containers = { "Coin.Menu" },
        onTrigger = function()
            a.sync(function()
                local ok, err = pcall(repro30629)
                if not ok then print("Coin: [repro] " .. tostring(err)) end
            end)()
        end,
    })
end

return {
    setup = setup,
}
