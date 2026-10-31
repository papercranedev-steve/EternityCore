-- Test scenarios: .scenario <id> builds a fixed party at a fixed place for the TEST account.
--
-- The leader (the player) gets the scenario level, a premade spec and premade gear for its class.
-- Bots are temporary party bots with fixed class/role, premade spec and premade gear, so every run
-- starts the same. The recorder (recorder.lua) is started for the party.
--
-- Messages to the player: plain text for the chat window, and "PBSCN;..." lines for the addon.
--
-- Scenario fields:
--   id, title          id used in .scenario <id>; title shown in the addon
--   level              party level
--   entry              {map, x, y, z, o}  safe teleport target inside the map (instance entrance)
--   spot               {x, y, z, o}       where the party waits; nil = computed from 'pack'
--   pack               {x, y, z}          centre of the target pack (for spot search and respawn)
--   approach           min/max distance of the computed spot from the pack centre
--   bots               { {class=, role=}, ... }

scenario = scenario or {}

local LEADER_ROLE = {
    [1] = "meleedps", [2] = "meleedps", [3] = "rangedps", [4] = "meleedps", [5] = "rangedps",
    [7] = "meleedps", [8] = "rangedps", [9] = "rangedps", [11] = "meleedps",
}

scenario.list = {
    {
        id = "stockade", title = "暴风城监狱 迪菲亚囚犯群（场景 1）", level = 25,
        entry = { map = 34, x = 48.98, y = 0.48, z = -16.39, o = 0.0 },
        pack = { x = 189.4, y = 88.5, z = -33.86 },
        approach = { min = 24, max = 34, clear = 22 },
        bots = {
            { class = "warrior", role = "tank" },
            { class = "rogue",   role = "meleedps" },
            { class = "mage",    role = "rangedps" },
            { class = "priest",  role = "healer" },
        },
    },
    {
        id = "strat", title = "斯坦索姆 亡灵怪群（场景 4）", level = 58,
        entry = { map = 329, x = 3392.92, y = -3395.03, z = 143.14, o = 1.57 },
        pack = { x = 3973.0, y = -3649.0, z = 134.5 },
        approach = { min = 26, max = 38, clear = 24 },
        bots = {
            { class = "warrior", role = "tank" },
            { class = "rogue",   role = "meleedps" },
            { class = "mage",    role = "rangedps" },
            { class = "priest",  role = "healer" },
        },
    },
}

function scenario.find(id)
    for _, s in ipairs(scenario.list) do
        if s.id == id then return s end
    end
end

local function say(p, text)
    p:tell("|cff66ccff[场景]|r " .. text)
end

local function status(p, state, text)
    p:tell("PBSCN;S;" .. state .. ";" .. (text or ""))
end

-- best premade template: same class, highest level <= level, preferring the role
local function pickPremade(kind, class, level, role)
    local best, bestScore
    for _, t in ipairs(premades(class)) do
        if t.kind == kind and t.level <= level then
            local score = t.level * 10 + ((t.role == role) and 5 or 0)
            if not best or score > bestScore then best, bestScore = t, score end
        end
    end
    return best
end

local function applyPremades(u, level, role)
    local spec = pickPremade("spec", u:class(), level, role)
    if spec then u:premade("spec", spec.entry) end
    local gear = pickPremade("gear", u:class(), level, role)
    if gear then
        u:premade("gear", gear.entry)
        u:clearbags()
    end
    return spec, gear
end

local function botsOf(leader)
    return filter(leader:group(), function(m) return m:isbot() end)
end

-- Walkable spot near the pack: along the navmesh path from the entrance, the point closest to the
-- pack that is within [min, max] of the pack centre and at least 'clear' yards from every live enemy.
local function computeSpot(leader, s)
    local a = s.approach
    local path = leader:path(s.pack.x, s.pack.y, s.pack.z)
    if not path or #path.points < 2 then return nil, "no path from entrance to pack" end
    local enemies = filter(leader:nearby(200, "enemy"), function(e) return not e:isplayer() end)
    local function clearOf(x, y, z)
        for _, e in ipairs(enemies) do
            local p = e:pos()
            local d = math.sqrt((p.x - x) ^ 2 + (p.y - y) ^ 2 + (p.z - z) ^ 2)
            if d < a.clear then return false end
        end
        return true
    end
    -- densify the path and walk it backwards from the pack
    local pts = {}
    for i = 1, #path.points - 1 do
        local p, q = path.points[i], path.points[i + 1]
        local len = math.sqrt((q.x - p.x) ^ 2 + (q.y - p.y) ^ 2)
        local steps = math.max(1, math.floor(len / 1.5))
        for k = 0, steps - 1 do
            local f = k / steps
            pts[#pts + 1] = { x = p.x + (q.x - p.x) * f, y = p.y + (q.y - p.y) * f, z = p.z + (q.z - p.z) * f }
        end
    end
    pts[#pts + 1] = path.points[#path.points]
    for i = #pts, 1, -1 do
        local p = pts[i]
        local d = math.sqrt((p.x - s.pack.x) ^ 2 + (p.y - s.pack.y) ^ 2)
        if d >= a.min and d <= a.max and clearOf(p.x, p.y, p.z) then
            local o = math.atan(s.pack.y - p.y, s.pack.x - p.x)
            return { x = p.x, y = p.y, z = p.z + 0.5, o = o }
        end
    end
    return nil, "no clear spot on the path"
end

local function respawnPack(leader, s, radius)
    local n = 0
    for _, c in ipairs(leader:nearby(radius or 120, "dead")) do
        if not c:isplayer() then
            local p = c:pos()
            if math.sqrt((p.x - s.pack.x) ^ 2 + (p.y - s.pack.y) ^ 2) < (radius or 120) then
                c:respawn()
                n = n + 1
            end
        end
    end
    return n
end

local function fail(p, msg)
    say(p, "失败：" .. msg)
    status(p, "error", msg)
    scenario.running = nil
end

-- Step 4: bots exist and are initialised -> gear, position, recorder.
local function finish(leader, s)
    local bots = botsOf(leader)
    local spot = scenario.spot
    for i, b in ipairs(bots) do
        local want = s.bots[i]
        local role = (b:ai() and b:ai().role) or (want and want.role) or "auto"
        applyPremades(b, s.level, role)
        b:revive()
        b:refill()
        b:resetcd()
        -- small arc behind the leader
        local back = spot.o + math.pi
        local ang = back + (i - (#bots + 1) / 2) * 0.6
        b:tp(spot.x + math.cos(ang) * 3.0, spot.y + math.sin(ang) * 3.0, spot.z, spot.o)
    end
    leader:refill()
    leader:resetcd()
    local respawned = respawnPack(leader, s)
    rec.watch(leader, s.id)
    scenario.running = nil
    scenario.current = s.id
    local names = map(bots, function(b) return b:name() .. "(" .. ((b:ai() and b:ai().role) or "?") .. ")" end)
    say(leader, string.format("%s 就绪：%d 级，队友 %s，刷新了 %d 只怪。战斗会自动录制。",
        s.title, s.level, table.concat(names, " "), respawned))
    status(leader, "ready", s.id)
end

-- Step 3: leader is at the spot -> spawn bots and wait for them.
local function spawnBots(leader, s)
    for _, b in ipairs(s.bots) do
        addbot(leader, b.class, b.role, s.level)
    end
    say(leader, "正在生成队友 ...")
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
        finish(leader, s)
    end, 30000, 500)
end

-- Step 2: leader is inside the map -> find the spot, move there.
local function placeLeader(leader, s)
    local spot = s.spot
    if not spot then
        local err
        spot, err = computeSpot(leader, s)
        if not spot then return fail(leader, err) end
    end
    scenario.spot = spot
    leader:tp(spot.x, spot.y, spot.z, spot.o)
    after(1000, function()
        local ok, err = pcall(spawnBots, leader, s)
        if not ok then fail(leader, tostring(err)) end
    end)
end

function scenario.load(leader, id)
    local s = scenario.find(id)
    if not s then return fail(leader, "没有这个场景：" .. tostring(id)) end
    if scenario.running then return say(leader, "上一个场景还在准备中，请稍等") end
    scenario.running = s.id
    rec.stop()
    status(leader, "loading", s.id)
    say(leader, "准备 " .. s.title .. " ...")

    -- 1. remove old bots
    for _, b in ipairs(botsOf(leader)) do b:removebot() end

    waitfor(function() return #botsOf(leader) == 0 end, function(ok)
        if not ok then return fail(leader, "旧队友没有移除") end
        -- 2. leader: alive, out of GM mode, level, spec, gear
        leader:revive()
        leader:combatstop()
        if leader:gm() then leader:gm(false) end
        leader:setlevel(s.level)
        applyPremades(leader, s.level, LEADER_ROLE[leader:class()] or "meleedps")
        leader:refill()
        leader:resetcd()

        local e = s.entry
        local pos = leader:pos()
        local function inMap()
            local p = leader:pos()
            return p.map == e.map and not leader:teleporting()
        end
        if pos.map ~= e.map then
            leader:tp(e.map, e.x, e.y, e.z, e.o)
        else
            leader:tp(e.x, e.y, e.z, e.o)
        end
        waitfor(inMap, function(ok2)
            if not ok2 then return fail(leader, "传送超时") end
            -- give the client a moment to finish loading the map
            after(1500, function()
                local okp, err = pcall(placeLeader, leader, s)
                if not okp then fail(leader, tostring(err)) end
            end)
        end, 30000, 250)
    end, 10000, 250)
end

-- Reload the last scenario (after a wipe).
function scenario.reset(leader)
    if not scenario.current then return say(leader, "还没有加载过场景") end
    scenario.load(leader, scenario.current)
end

function scenario.stop(leader)
    rec.stop()
    for _, b in ipairs(botsOf(leader)) do b:removebot() end
    scenario.current = nil
    say(leader, "场景已结束，队友已移除")
    status(leader, "stopped", "")
end

-- .scenario [list | <id> | reset | stop | status]
function scenario.command(leader, args)
    args = (args or ""):match("^%s*(.-)%s*$")
    if args == "" or args == "list" then
        for _, s in ipairs(scenario.list) do
            leader:tell("PBSCN;L;" .. s.id .. ";" .. s.title .. ";" .. s.level)
        end
        leader:tell("PBSCN;E;" .. (scenario.current or ""))
        if args == "" then
            for _, s in ipairs(scenario.list) do say(leader, ".scenario " .. s.id .. "  " .. s.title) end
            say(leader, ".scenario reset  重开当前场景    .scenario stop  结束")
        end
    elseif args == "reset" then
        scenario.reset(leader)
    elseif args == "stop" then
        scenario.stop(leader)
    elseif args == "status" then
        local st = rec.status()
        say(leader, "当前场景：" .. (scenario.current or "无") .. "，录制：" .. (st.recording and "进行中" or (st.watching and "等待战斗" or "关闭")))
    else
        scenario.load(leader, args)
    end
end
