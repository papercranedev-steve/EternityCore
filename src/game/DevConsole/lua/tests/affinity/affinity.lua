-- Companion affinity tests (design .kiro/docs/2026-11-21-companion-affinity.md): one button per
-- case in the addon's 好感度测试 window (/pba). Temporary bots made temporary companions (nothing is
-- written to the database) with the affinity each case wants, then either automatic steps with
-- checks (the gate table, the real commands at each threshold, level-up points) or a watch while
-- the player plays the case (trades, the bag panel, the talk window, a dungeon clear). Everything
-- goes to a log file the agent reads:
--   ../logs/afftest/<case>-<yyyyMMdd-HHmmss>.log  (relative to mangosd: repack/logs/afftest)
-- Lines: CASE / EXPECT / SETUP / STEP / CMD / CHECK OK|FAIL / WARN / CHANGE / NOTE / RESULT.
--
--   .scenario aff [list]       -> PBAFT;L;<id>;<title>;<auto|watch>  PBAFT;H;<id>;<hint>  ...  PBAFT;E;<current>
--   .scenario aff <id> | reset | stop
--   .scenario aff note [text]  a mark in the log with a snapshot of every bot
--   PBAFT;S;loading|running|ready|error|stopped;<text>   (running: the automatic steps of <id>)
--   PBAFT;R;<ok>;<fail>;<log file name>             after the automatic steps
--   PBAFT;U;<name>;<classId>;<money>;<text>          every 2 s while a case runs
--
-- Case (cases.lua): id, title, hint, expect = { "...", ... } (written to the log as EXPECT),
--   bots = { { id, class, role, level (0 = the leader's), affinity, money, companion = false,
--              tags, items = { {entry, n} }, learn = { spell, ... }, profs = { {skill, value} },
--              unequip = { slot, ... } (emptied before the case: something to trade him), note } },
--   leaderItems = { {entry, n} }   given to the player
--   watchItems = { entry, ... }    their counts (bots and player) are logged when they change
--   steps = function(t) ... end    automatic part, runs as a coroutine (t.wait), see makeT
--   watch = true                   logs changes until stopped

afftest = afftest or {}

local DIR = SCRIPT_DIR .. "/tests/affinity"
local LOG_DIR = "../logs/afftest"
local MONITOR_MS = 2000

do
    local chunk, err = loadfile(DIR .. "/cases.lua")
    if not chunk then error(err, 0) end
    afftest.list = chunk()
end

local function find(id)
    for _, c in ipairs(afftest.list) do
        if c.id == id then return c end
    end
end

local function say(p, text) p:tell("|cffff88cc[好感]|r " .. text) end
local function status(p, state, text) p:tell("PBAFT;S;" .. state .. ";" .. (text or "")) end
local botsOf = function(leader) return scenario.botsOf(leader) end

---------------------------------------------------------------------------
-- log file
---------------------------------------------------------------------------

local function openLog(id)
    local name = id .. "-" .. os.date("%Y%m%d-%H%M%S") .. ".log"
    for _, path in ipairs({ LOG_DIR .. "/" .. name, "../logs/afftest-" .. name }) do
        local f = io.open(path, "w")
        if f then
            f:close()
            afftest.file, afftest.fileName = path, name
            return
        end
    end
    afftest.file, afftest.fileName = nil, nil
end

local function log(text)
    if not afftest.file then return end
    local f = io.open(afftest.file, "a")
    if not f then return end
    for line in tostring(text):gmatch("[^\n]+") do f:write(os.date("%H:%M:%S "), line, "\n") end
    f:close()
end

-- "22 [trade tradeany bags]": the affinity and the gates it allows now (its class only, ready ones)
local function gatesText(b)
    local a = b:affinity()
    local open = {}
    for _, g in ipairs(a.gates) do
        if g.forclass and g.ready and g.allowed then open[#open + 1] = g.key end
    end
    return string.format("%d [%s]%s", a.value, table.concat(open, " "), a.companion and "" or " NOT A COMPANION")
end

local function itemsText(u, entries)
    local parts = {}
    for _, e in ipairs(entries or {}) do parts[#parts + 1] = e .. "x" .. u:itemcount(e) end
    return table.concat(parts, " ")
end

local function snapshot(label, bots, c)
    for _, b in ipairs(bots) do
        log(string.format("%s %s lv%d affinity %s money %s%s", label, b:name(), b:level(), gatesText(b), gold(b:money()),
            (c and c.watchItems) and ("  items " .. itemsText(b, c.watchItems)) or ""))
    end
end

local function fail(leader, msg)
    afftest.running = nil
    log("ERROR " .. msg)
    say(leader, "失败：" .. msg)
    status(leader, "error", msg)
end

local function guarded(leader, fn, ...)
    local ok, err = pcall(fn, ...)
    if not ok then fail(leader, tostring(err)) end
    return ok
end

---------------------------------------------------------------------------
-- monitor: the addon's live lines, and (watch cases) every change to the log
---------------------------------------------------------------------------

local function stopMonitor()
    cancel(afftest.monitor)
    afftest.monitor = nil
end

local function liveLine(b)
    local a = b:affinity()
    local open = {}
    for _, g in ipairs(a.gates) do
        if g.forclass and g.ready and g.allowed then open[#open + 1] = g.key end
    end
    return string.format("PBAFT;U;%s;%d;%d;好感 %d  %s", b:name(), b:class(), b:money(), a.value,
        #open > 0 and table.concat(open, " ") or "(都还锁着)")
end

local function watchOne(u, st, c)
    local aff = u:isbot() and u:affinity().value or nil
    if aff and st.aff and aff ~= st.aff then
        log(string.format("CHANGE %s affinity %d -> %d (%s)", u:name(), st.aff, aff, gatesText(u)))
    end
    st.aff = aff
    local m = u:money()
    if st.money and m ~= st.money then
        log(string.format("CHANGE %s money %s -> %s (%s%s)", u:name(), gold(st.money), gold(m),
            m < st.money and "-" or "+", gold(math.abs(m - st.money))))
    end
    st.money = m
    for _, e in ipairs(c.watchItems or {}) do
        local n = u:itemcount(e)
        if st.items[e] and st.items[e] ~= n then
            log(string.format("CHANGE %s item %d: %d -> %d", u:name(), e, st.items[e], n))
        end
        st.items[e] = n
    end
end

local function monitor(leader, c)
    stopMonitor()
    afftest.monitor = every(MONITOR_MS, function()
        if not leader:exists() then stopMonitor() return false end
        local ours = 0
        for _, b in ipairs(botsOf(leader)) do
            local st = afftest.state[b:name()]
            if st then
                ours = ours + 1
                leader:tell(liveLine(b))
                if c.watch then watchOne(b, st, c) end
            end
        end
        if c.watch then watchOne(leader, afftest.leaderState, c) end
        if ours == 0 then
            stopMonitor()
            log("END the bots are gone")
            afftest.current = nil
            status(leader, "stopped")
            return false
        end
    end)
end

---------------------------------------------------------------------------
-- automatic steps: the t table a case's steps(t) gets (runs as a coroutine)
---------------------------------------------------------------------------

local function makeT(leader, byId, ours)
    local t = { ok = 0, fail = 0, leader = leader, bots = ours }
    function t.bot(id) return byId[id] or error("no bot " .. tostring(id)) end
    t.log = log
    function t.check(cond, text)
        if cond then t.ok = t.ok + 1 else t.fail = t.fail + 1 end
        log((cond and "CHECK OK   " or "CHECK FAIL ") .. text)
        return cond
    end
    function t.warn(text) log("WARN " .. text) end
    -- pause the steps; the bots' AI ticks meanwhile (level-up points, unlock lines)
    function t.wait(ms) coroutine.yield(ms) end
    function t.set(b, value)
        local now = b:setaffinity(value)
        log(string.format("STEP %s affinity = %d -> %s", b:name(), value, gatesText(b)))
        return now
    end
    function t.aff(b) return b:affinity().value end
    -- the gate entry of u:affinity() by key
    function t.gate(b, key)
        for _, g in ipairs(b:affinity().gates) do
            if g.key == key then return g end
        end
    end
    -- a GM command as the player; its text output (logged, one line per reply line)
    function t.cmd(text)
        local out = leader:cmd(text)
        log("CMD " .. text)
        for line in out:gmatch("[^\n]+") do log("    " .. line) end
        return out
    end
    -- the talk window's option ids for `b` (".partybot talk"), as a set; nil + reply if it did not open
    function t.options(b)
        local out = t.cmd(".partybot talk " .. b:name())
        if not out:find("PBTALK;" .. b:name() .. ";BEGIN", 1, true) then return nil, out end
        local set = {}
        for id in out:gmatch("PBTALK;[^;]*;OPT;([^;]*);") do set[id] = true end
        return set, out
    end
    return t
end

-- Runs steps(t) as a coroutine: t.wait(ms) yields, a timer resumes it. done(ok, err) at the end.
local function runSteps(steps, t, done)
    local co = coroutine.create(steps)
    local function resume()
        local ok, res = coroutine.resume(co, t)
        if not ok then return done(false, res) end
        if coroutine.status(co) == "dead" then return done(true) end
        afftest.stepTimer = after(tonumber(res) or 500, resume)
    end
    resume()
end

---------------------------------------------------------------------------
-- loading: bots -> gear, items, affinity -> steps / watch
---------------------------------------------------------------------------

local function levelOf(w, leader) return (w.level and w.level > 0) and w.level or leader:level() end

local function give(leader, u, entry, count)
    leader:select(u)
    leader:cmd(string.format(".additem %d %d", entry, count or 1))
end

local function setupBot(leader, b, w, index, count)
    scenario.applyPremades(b, levelOf(w, leader), w.role or "auto", leader)
    if w.companion ~= false and not b:setcompanion(true, leader, w.tags or {}) then
        say(leader, "|cffffcc00注意|r：" .. b:name() .. " 没能设成同伴")
        log("WARN " .. b:name() .. " is not a temporary companion")
    end
    -- emptied slots (something he will take in a trade): through the real command, so with the
    -- bags gate open for the moment; what comes off goes with clearbags
    if w.unequip and b:affinity().temporary then
        b:setaffinity(100)
        for _, slot in ipairs(w.unequip) do leader:cmd(string.format(".partybot unequipslot %s %d", b:name(), slot)) end
    end
    b:clearbags()
    b:setmoney(w.money or 0)
    for _, it in ipairs(w.items or {}) do give(leader, b, it[1], it[2]) end
    for _, spell in ipairs(w.learn or {}) do
        leader:select(b)
        leader:cmd(".learn " .. spell)
    end
    if w.profs then
        b:forgetprofs()
        for _, p in ipairs(w.profs) do b:setprof(p[1], p[2]) end
    end
    if b:affinity().temporary then b:setaffinity(w.affinity or 0) end
    b:revive()
    b:refill()
    local at = leader:pos()
    local ang = at.o + (index - (count + 1) / 2) * 0.5
    b:tp(at.x + math.cos(ang) * 4.0, at.y + math.sin(ang) * 4.0, at.z, at.o + math.pi)
end

local function finish(leader, c)
    local used = {}
    local byId, ours = {}, {}
    afftest.state = {}
    for i, b in ipairs(botsOf(leader)) do
        local w
        for j, cand in ipairs(c.bots) do
            if not used[j] and scenario.CLASS_ID[cand.class] == b:class() then w = cand used[j] = true break end
        end
        if w then
            setupBot(leader, b, w, i, #c.bots)
            log(string.format("SETUP %s = %s (%s) level %d, affinity %d, %s", b:name(), w.id or "-", w.class, b:level(),
                w.affinity or 0, w.note or ""))
            if w.id then byId[w.id] = b end
            ours[#ours + 1] = b
            afftest.state[b:name()] = { items = {} }
        end
    end
    for _, it in ipairs(c.leaderItems or {}) do give(leader, leader, it[1], it[2]) end
    leader:select(leader)
    afftest.leaderState = { items = {} }
    snapshot("SETUP", ours, c)
    log(string.format("SETUP leader %s level %d%s money %s%s", leader:name(), leader:level(),
        leader:isbot() and " (a bot session: no level-up or clear points)" or "", gold(leader:money()),
        c.watchItems and ("  items " .. itemsText(leader, c.watchItems)) or ""))
    for _, b in ipairs(ours) do watchOne(b, afftest.state[b:name()], c) end
    watchOne(leader, afftest.leaderState, c)
    afftest.running = nil
    afftest.current = c.id

    local function ready()
        if c.watch then
            log("WATCH started")
            say(leader, c.title .. " 就绪：" .. (c.hint or ""))
        else
            say(leader, c.title .. " 完成。")
        end
        say(leader, "记录：" .. (afftest.fileName or "（日志文件打不开）"))
        status(leader, "ready", c.id)
    end
    monitor(leader, c)
    if not c.steps then return ready() end

    local t = makeT(leader, byId, ours)
    status(leader, "running", c.id)
    runSteps(c.steps, t, function(ok, err)
        afftest.stepTimer = nil
        if not ok then log("ERROR " .. tostring(err)) end
        log(string.format("RESULT %d ok, %d fail%s", t.ok, t.fail, ok and "" or ", steps stopped by an error"))
        snapshot("AFTER", filter(ours, function(b) return b:exists() end), c)
        leader:tell(string.format("PBAFT;R;%d;%d;%s", t.ok, t.fail, afftest.fileName or "-"))
        say(leader, string.format("自动检查：|cff20ff20%d 通过|r，%s%s", t.ok,
            t.fail > 0 and ("|cffff4040" .. t.fail .. " 失败|r") or "0 失败", ok and "" or ("，出错：" .. tostring(err))))
        ready()
    end)
end

local function spawnBots(leader, c)
    for _, w in ipairs(c.bots) do addbot(leader, w.class, w.role or "auto", levelOf(w, leader)) end
    waitfor(function()
        local bots = botsOf(leader)
        if #bots < #c.bots then return false end
        for _, b in ipairs(bots) do
            local ai = b:ai()
            if not ai or not ai.initialized then return false end
        end
        return true
    end, function(ok)
        if not ok then return fail(leader, "队友 30 秒内没有就绪") end
        guarded(leader, finish, leader, c)
    end, 30000, 500)
end

local function stopSteps()
    cancel(afftest.stepTimer)
    afftest.stepTimer = nil
end

function afftest.load(leader, id)
    local c = find(id)
    if not c then return fail(leader, "没有这个好感度测试：" .. tostring(id)) end
    if afftest.running then return say(leader, "上一个测试还在准备中，请稍等") end
    if leader:incombat() then return fail(leader, "战斗中不能加载") end
    afftest.running = c.id
    stopMonitor()
    stopSteps()
    openLog(c.id)
    log("CASE " .. c.id .. "  " .. c.title)
    if c.hint then log("HINT " .. c.hint) end
    for _, e in ipairs(c.expect or {}) do log("EXPECT " .. e) end
    status(leader, "loading", c.id)
    say(leader, "准备 " .. c.title .. " ...")

    for _, b in ipairs(botsOf(leader)) do b:removebot() end
    waitfor(function() return #botsOf(leader) == 0 end, function(ok)
        if not ok then return fail(leader, "旧队友没有移除") end
        guarded(leader, function()
            leader:revive()
            leader:combatstop()
            after(500, function() guarded(leader, spawnBots, leader, c) end)
        end)
    end, 10000, 250)
end

function afftest.stop(leader)
    stopMonitor()
    stopSteps()
    log("END stopped")
    for _, b in ipairs(botsOf(leader)) do b:removebot() end
    afftest.current = nil
    afftest.running = nil
    status(leader, "stopped")
    say(leader, "好感度测试已结束")
end

function afftest.command(leader, args)
    args = (args or ""):match("^%s*(.-)%s*$")
    if args == "" or args == "list" then
        for _, c in ipairs(afftest.list) do
            leader:tell(string.format("PBAFT;L;%s;%s;%s", c.id, c.title, c.watch and "watch" or "auto"))
            leader:tell(string.format("PBAFT;H;%s;%s", c.id, ((c.hint or ""):gsub(";", ","))))
        end
        leader:tell("PBAFT;E;" .. (afftest.current or ""))
        if args == "" then
            for _, c in ipairs(afftest.list) do say(leader, ".scenario aff " .. c.id .. "  " .. c.title) end
            say(leader, ".scenario aff reset | stop | note [说明]    窗口：/pba")
        end
    elseif args == "stop" then
        afftest.stop(leader)
    elseif args == "reset" then
        if not afftest.current then return say(leader, "还没有加载过好感度测试") end
        afftest.load(leader, afftest.current)
    elseif args:match("^note") then
        if not afftest.current then return say(leader, "现在没有好感度测试在运行") end
        local text = args:match("^note%s+(.+)$") or ""
        log("NOTE " .. text)
        local c = find(afftest.current)
        snapshot("NOTE", filter(botsOf(leader), function(b) return afftest.state[b:name()] ~= nil end), c)
        if c and c.watchItems then log("NOTE leader items " .. itemsText(leader, c.watchItems)) end
        say(leader, "已记一笔" .. (text ~= "" and ("：" .. text) or ""))
    else
        afftest.load(leader, args)
    end
end
