-- Life (non-combat) test scenarios: put the party somewhere with a given money and bag state,
-- then watch the bots' errands (asking the player, walking to a vendor, buying, coming back).
-- One scenario per file in this folder (FILES below); the addon's 生活测试 window (/pbl) lists
-- and starts them. Same TEST-account gate as the combat scenarios (.scenario life ...).
--
--   .scenario life [list]        -> PBLIFE;L;<id>;<title>;<level>
--                                   PBLIFE;B;<id>;<slot>;<class>;<role>;<note>  ...  PBLIFE;E;<current id>
--   .scenario life <id> | reset | stop
--   .scenario life setspot [id]  your position becomes the scenario's start (tests/life/spots.lua)
--   while running, every 2 s per bot:
--     PBLIFE;U;<name>;<classId>;<money>;<dist to you>;<flags>;<missing>
--       flags: m = moving, a = away from you (> AWAY_YD), d = done (the scenario's check holds)
--       missing: "food 2/20!,drink 0/20" (! = critical), Shared/Supplies::Missing
--   PBLIFE;S;loading|ready|error|stopped;<text>
--
-- Scenario file: returns a table
--   id, title, level, faction ("alliance" | "horde" | nil)
--   at = { map, x, y, z, o }                 where you and the bots start
--   bots = { { class, role, money (copper), items = { {entry, count}, ... },
--              fill = entry (fills the free bag slots with it after items), note }, ... }
--   done = function(bot, start, leader) -> bool   optional; start = { money } at ready
--           default: nothing critical missing and back within 10 yd of you

lifetest = lifetest or {}

local DIR = SCRIPT_DIR .. "/tests/life"
local FILES = {
    "supplies_gate.lua",
}
local SPOTS_FILE = DIR .. "/spots.lua"
local MONITOR_MS = 2000
local AWAY_YD = 30
local HORDE_RACES = { [2] = true, [5] = true, [6] = true, [8] = true }

lifetest.list = {}
for _, f in ipairs(FILES) do
    local chunk, err = loadfile(DIR .. "/" .. f)
    if not chunk then error(err, 0) end
    lifetest.list[#lifetest.list + 1] = chunk()
end

-- start positions saved with setspot (override the file's 'at')
lifetest.spots = {}
do
    local chunk = loadfile(SPOTS_FILE)
    if chunk then
        local ok, t = pcall(chunk)
        if ok and type(t) == "table" then lifetest.spots = t end
    end
end

local function saveSpots()
    local f, err = io.open(SPOTS_FILE, "w")
    if not f then error("cannot write " .. SPOTS_FILE .. ": " .. tostring(err)) end
    f:write("-- written by .scenario life setspot\nreturn {\n")
    for id, p in pairs(lifetest.spots) do
        f:write(string.format("    [%q] = { map = %d, x = %.2f, y = %.2f, z = %.2f, o = %.2f },\n", id, p.map, p.x, p.y, p.z, p.o))
    end
    f:write("}\n")
    f:close()
end

local function find(id)
    for _, s in ipairs(lifetest.list) do
        if s.id == id then return s end
    end
end

local function say(p, text) p:tell("|cff88dd66[生活]|r " .. text) end
local function status(p, state, text) p:tell("PBLIFE;S;" .. state .. ";" .. (text or "")) end

local function fail(p, msg)
    lifetest.running = nil
    say(p, "失败：" .. msg)
    status(p, "error", msg)
end

-- runs fn(...) and reports an error to the leader instead of losing it in a timer
local function guarded(leader, fn, ...)
    local ok, err = pcall(fn, ...)
    if not ok then fail(leader, tostring(err)) end
end

local botsOf = function(leader) return scenario.botsOf(leader) end

---------------------------------------------------------------------------
-- monitor: bot state to the addon, and what changed to the chat
---------------------------------------------------------------------------

local function missingText(b)
    local parts = {}
    for _, n in ipairs(b:supplies()) do
        parts[#parts + 1] = string.format("%s %d/%d%s", n.goods, n.have, n.target, n.priority == "critical" and "!" or "")
    end
    return table.concat(parts, ",")
end

local function criticalMissing(b)
    for _, n in ipairs(b:supplies()) do
        if n.priority == "critical" then return true end
    end
    return false
end

local function defaultDone(b, start, leader)
    local d = b:dist(leader)
    return not criticalMissing(b) and d ~= nil and d <= 10
end

local function money(c)
    if c >= 10000 then return string.format("%dg%ds%dc", c // 10000, (c // 100) % 100, c % 100) end
    if c >= 100 then return string.format("%ds%dc", c // 100, c % 100) end
    return c .. "c"
end

local function stopMonitor()
    cancel(lifetest.monitor)
    lifetest.monitor = nil
end

local function monitor(leader, s)
    stopMonitor()
    lifetest.monitor = every(MONITOR_MS, function()
        if not leader:exists() then stopMonitor() return false end
        local bots = botsOf(leader)
        if #bots == 0 then
            stopMonitor()
            lifetest.current = nil
            status(leader, "stopped")
            return false
        end
        local done = s.done or defaultDone
        local ours = 0
        for _, b in ipairs(bots) do
            local st = lifetest.start[b:name()]
            if st then
                ours = ours + 1
                local m = b:money()
                local d = b:dist(leader)
                local away = d == nil or d > AWAY_YD
                local isDone = done(b, st, leader)
                local flags = (b:moving() and "m" or "") .. (away and "a" or "") .. (isDone and "d" or "")
                leader:tell(string.format("PBLIFE;U;%s;%d;%d;%s;%s;%s", b:name(), b:class(), m,
                    d and string.format("%.0f", d) or "-", flags, missingText(b)))

                -- what changed since the last look, in the chat
                if m ~= st.lastMoney then
                    local delta = m - st.lastMoney
                    say(leader, string.format("%s %s %s，现在 %s。还缺：%s", b:name(),
                        delta < 0 and "花了" or "得到", money(math.abs(delta)), money(m), missingText(b) ~= "" and missingText(b) or "无"))
                    st.lastMoney = m
                end
                if away and not st.away then say(leader, b:name() .. " 离开了你（" .. (d and string.format("%.0f 码", d) or "不在这张地图") .. "）") end
                if not away and st.away then say(leader, b:name() .. " 回来了") end
                st.away = away
                if isDone and not st.done then say(leader, "|cff20ff20✓|r " .. b:name() .. " 完成") end
                st.done = isDone
            end
        end
        -- another scenario replaced the party (a combat scenario removes and adds bots)
        if ours == 0 then
            stopMonitor()
            lifetest.current = nil
            status(leader, "stopped")
            return false
        end
    end)
end

---------------------------------------------------------------------------
-- loading: leader to the spot -> bots -> money, bags -> monitor
---------------------------------------------------------------------------

local function setupBot(leader, b, w, level, index, count, at)
    scenario.applyPremades(b, level, w.role or "auto", leader)   -- gear and spec; clears the bags
    b:clearbags()
    b:setmoney(w.money or 0)
    for _, it in ipairs(w.items or {}) do
        leader:select(b)
        leader:cmd(string.format(".additem %d %d", it[1], it[2] or 1))
    end
    if w.fill then
        -- .additem stores as many as fit
        leader:select(b)
        leader:cmd(string.format(".additem %d %d", w.fill, 5000))
    end
    -- errands and town trips are the soul companions' (PartyBotAI::LootsForSelf): the temporary
    -- bot counts as one of the leader's until it logs out (in memory only, never saved)
    if not b:setcompanion(true, leader) then
        say(leader, "|cffffcc00注意|r：" .. b:name() .. " 没能设成同伴，不会跑腿")
    end
    b:revive()
    b:refill()
    b:resetcd()
    -- a row in front of the leader, facing him
    local ang = at.o + (index - (count + 1) / 2) * 0.5
    b:tp(at.x + math.cos(ang) * 4.0, at.y + math.sin(ang) * 4.0, at.z, at.o + math.pi)
end

local function finish(leader, s, at)
    local bots = botsOf(leader)
    local used = {}
    lifetest.start = {}
    for i, b in ipairs(bots) do
        -- the lineup entry of this bot's class not used yet (group order need not follow it)
        local w
        for j, cand in ipairs(s.bots) do
            if not used[j] and scenario.CLASS_ID[cand.class] == b:class() then w = cand used[j] = true break end
        end
        if w then
            setupBot(leader, b, w, s.level, i, #bots, at)
            lifetest.start[b:name()] = { money = b:money(), lastMoney = b:money() }
        end
    end
    lifetest.running = nil
    lifetest.current = s.id
    -- the town trip only asks while the leader rests (city, inn) and stands still
    if not leader:resting() then
        say(leader, "|cffffcc00注意|r：你不在休息区（城市、旅店），队友不会请求进城")
    end
    for _, b in ipairs(bots) do
        say(leader, string.format("%s：%s，缺 %s", b:name(), money(b:money()), missingText(b) ~= "" and missingText(b) or "无"))
    end
    say(leader, s.title .. " 就绪。")
    status(leader, "ready", s.id)
    monitor(leader, s)
end

local function spawnBots(leader, s, at)
    for _, w in ipairs(s.bots) do addbot(leader, w.class, w.role, s.level) end
    waitfor(function()
        local bots = botsOf(leader)
        if #bots < #s.bots then return false end
        for _, b in ipairs(bots) do
            local ai = b:ai()
            if not ai or not ai.initialized then return false end
        end
        return true
    end, function(ok)
        if not ok then return fail(leader, "队友 30 秒内没有就绪") end
        guarded(leader, finish, leader, s, at)
    end, 30000, 500)
end

function lifetest.load(leader, id)
    local s = find(id)
    if not s then return fail(leader, "没有这个生活场景：" .. tostring(id)) end
    if lifetest.running then return say(leader, "上一个场景还在准备中，请稍等") end
    local horde = HORDE_RACES[leader:race()] and "horde" or "alliance"
    if s.faction and s.faction ~= horde then
        return fail(leader, "这个场景要 " .. (s.faction == "horde" and "部落" or "联盟") .. " 角色")
    end
    if leader:incombat() then return fail(leader, "战斗中不能加载") end
    lifetest.running = s.id
    stopMonitor()
    status(leader, "loading", s.id)
    say(leader, "准备 " .. s.title .. " ...")

    for _, b in ipairs(botsOf(leader)) do b:removebot() end
    waitfor(function() return #botsOf(leader) == 0 end, function(ok)
        if not ok then return fail(leader, "旧队友没有移除") end
        guarded(leader, function()
            local at = lifetest.spots[s.id] or s.at
            leader:revive()
            leader:combatstop()
            leader:refill()
            if leader:pos().map ~= at.map then
                leader:tp(at.map, at.x, at.y, at.z, at.o)
            else
                leader:tp(at.x, at.y, at.z, at.o)
            end
            waitfor(function()
                return leader:pos().map == at.map and not leader:teleporting()
            end, function(ok2)
                if not ok2 then return fail(leader, "传送超时") end
                -- the client loads the map; bots spawn where the leader stands
                after(1500, function() guarded(leader, spawnBots, leader, s, at) end)
            end, 30000, 250)
        end)
    end, 10000, 250)
end

function lifetest.stop(leader)
    stopMonitor()
    for _, b in ipairs(botsOf(leader)) do b:removebot() end
    lifetest.current = nil
    lifetest.running = nil
    status(leader, "stopped")
    say(leader, "生活场景已结束")
end

function lifetest.command(leader, args)
    args = (args or ""):match("^%s*(.-)%s*$")
    if args == "" or args == "list" then
        for _, s in ipairs(lifetest.list) do
            leader:tell("PBLIFE;L;" .. s.id .. ";" .. s.title .. ";" .. s.level)
            for i, w in ipairs(s.bots) do
                leader:tell(string.format("PBLIFE;B;%s;%d;%s;%s;%s", s.id, i, w.class, w.role or "auto", (w.note or ""):gsub(";", ",")))
            end
        end
        leader:tell("PBLIFE;E;" .. (lifetest.current or ""))
        if args == "" then
            for _, s in ipairs(lifetest.list) do say(leader, ".scenario life " .. s.id .. "  " .. s.title) end
            say(leader, ".scenario life reset | stop | setspot [id]    窗口：/pbl")
        end
    elseif args == "stop" then
        lifetest.stop(leader)
    elseif args == "reset" then
        if not lifetest.current then return say(leader, "还没有加载过生活场景") end
        lifetest.load(leader, lifetest.current)
    elseif args:match("^setspot") then
        local id = args:match("^setspot%s+(%S+)") or lifetest.current
        local s = id and find(id)
        if not s then return say(leader, "用法：.scenario life setspot <场景id>") end
        local p = leader:pos()
        lifetest.spots[s.id] = { map = p.map, x = p.x, y = p.y, z = p.z + 0.5, o = p.o }
        saveSpots()
        say(leader, string.format("%s 的起点已保存（地图 %d，%.1f, %.1f, %.1f）", s.title, p.map, p.x, p.y, p.z))
    else
        lifetest.load(leader, args)
    end
end
