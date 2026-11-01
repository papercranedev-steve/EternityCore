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
        -- 23: the first hall's mobs are 23-25 (user, 2026-11-01: test at the same level)
        id = "stockade", title = "暴风城监狱 迪菲亚囚犯群（场景 1）", level = 23,
        entry = { map = 34, x = 48.98, y = 0.48, z = -16.39, o = 0.0 },
        -- First hall past the entrance (Inmate, Prisoner, Captives 27-36 yd from the door). The
        -- scenario 1 doc pack at (189, 88) is deep in the corridor: every point on the path to it
        -- is within 10 yd of another mob, so there is no safe spot to wait at.
        spot = { x = 50.0, y = 0.5, z = -16.4, o = 0.0 },
        pack = { x = 80.0, y = 1.0, z = -26.0 },
        approach = { min = 24, max = 34, clear = 22 },
        bots = {
            { class = "warrior", role = "tank" },
            { class = "rogue",   role = "meleedps" },
            { class = "mage",    role = "rangedps" },
            { class = "priest",  role = "healer" },
        },
    },
    {
        -- level 60: premade gear only exists for 49 and 60, 49 gear is far too weak here
        -- Pack: creature_groups 52157 (Ravaged Cadaver + Mangled Cadaver, elites, and 3 skeletons).
        -- Not the scenario 4 doc pack at (3973, -3649): that street is crossed by gargoyle / Eye of
        -- Naxxramas patrols and every spot near it got the party feared into other packs.
        id = "strat", title = "斯坦索姆 尸体与骷髅群", level = 60,
        entry = { map = 329, x = 3392.92, y = -3395.03, z = 143.14, o = 1.57 },
        pack = { x = 3687.0, y = -3597.0, z = 138.0 },
        -- No computed spot: the street is packed with mobs that are not in creature_groups, and the
        -- 1.14 client falls through the floor at many navmesh points. Record one with .scenario setspot.
        needsRecordedSpot = true,
        approach = { min = 26, max = 38, clear = 24 },
        bots = {
            { class = "warrior", role = "tank" },
            { class = "rogue",   role = "meleedps" },
            { class = "mage",    role = "rangedps" },
            { class = "priest",  role = "healer" },
        },
    },
}

-- Spots recorded in game with ".scenario setspot" (spots.lua next to this file). They win over
-- 'spot' and the computed spot, because a point a player has stood on is known to be solid ground.
local SPOTS_FILE = SCRIPT_DIR .. "/spots.lua"
scenario.spots = {}
do
    local chunk = loadfile(SPOTS_FILE)
    if chunk then
        local ok, t = pcall(chunk)
        if ok and type(t) == "table" then scenario.spots = t end
    end
end

local function saveSpots()
    local f, err = io.open(SPOTS_FILE, "w")
    if not f then error("cannot write " .. SPOTS_FILE .. ": " .. tostring(err)) end
    f:write("-- written by .scenario setspot\nreturn {\n")
    local ids = {}
    for id in pairs(scenario.spots) do ids[#ids + 1] = id end
    table.sort(ids)
    for _, id in ipairs(ids) do
        local s = scenario.spots[id]
        f:write(string.format("    [%q] = { map = %d, x = %.2f, y = %.2f, z = %.2f, o = %.3f },\n", id, s.map, s.x, s.y, s.z, s.o))
    end
    f:write("}\n")
    f:close()
end

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

-- 'gm' runs .maxskill on u (a GM player; bots have no GM rights). Premade gear teaches missing
-- weapon proficiencies at skill 1, so without this the warriors missed every swing with the
-- level 19 template's two-handed mace (first Stockade run, 2026-11-01).
local function applyPremades(u, level, role, gm)
    local spec = pickPremade("spec", u:class(), level, role)
    if spec then u:premade("spec", spec.entry) end
    local gear = pickPremade("gear", u:class(), level, role)
    if gear then
        u:premade("gear", gear.entry)
        u:clearbags()
    end
    gm = gm or u
    gm:select(u)
    gm:cmd(".maxskill")
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
    -- 'alive', not 'enemy': the leader is in GM mode here, and GM mode makes nothing hostile
    local enemies = filter(leader:nearby(200, "alive"), function(e) return not e:isplayer() end)
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
            -- navmesh z can be below the real floor; teleporting under it drops you out of the world
            local ground = leader:height(p.x, p.y, p.z + 4)
            local z = (ground and ground > -100000 and math.abs(ground - p.z) < 6) and ground or p.z
            -- navz: the height query can hit a ceiling above the path (Stockade), so the fall check
            -- also accepts landing on the path height
            return { x = p.x, y = p.y, z = z + 1.0, o = o, navz = p.z }
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
    if p:exists() and p:gm() then p:gm(false) end
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
        applyPremades(b, s.level, role, leader)
        b:revive()
        b:refill()
        b:resetcd()
        -- small arc behind the leader
        local back = spot.o + math.pi
        local ang = back + (i - (#bots + 1) / 2) * 0.6
        b:tp(spot.x + math.cos(ang) * 3.0, spot.y + math.sin(ang) * 3.0, spot.z, spot.o)
    end
    leader:gm(false)
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
    local here = leader:pos()
    local floor = math.min(scenario.spot.z, scenario.spot.navz or scenario.spot.z)
    if here.z < floor - 5 then
        local e = s.entry
        leader:tp(e.map, e.x, e.y, e.z, e.o)
        return fail(leader, string.format("站位不对（掉到了 z=%.0f），已传回入口", here.z))
    end
    -- bots go where the leader actually stands
    scenario.spot = { x = scenario.spot.x, y = scenario.spot.y, z = here.z + 0.5, o = scenario.spot.o }
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
-- Long paths come back incomplete (path length limit), so hop to the end of the partial path and
-- search again from there; nearby enemies are only known once their grid is loaded anyway.
local function placeLeader(leader, s, hops)
    hops = hops or 0
    local spot = scenario.spots[s.id] or s.spot
    if not spot and s.needsRecordedSpot then
        return fail(leader, "这个场景还没有站位：走到想开怪的位置，输入 .scenario setspot " .. s.id .. "，再点一次场景")
    end
    if not spot then
        local err
        spot, err = computeSpot(leader, s)
        if not spot then
            local path = leader:path(s.pack.x, s.pack.y, s.pack.z)
            local last = path and path.points[#path.points]
            local here = leader:pos()
            local moved = last and math.sqrt((last.x - here.x) ^ 2 + (last.y - here.y) ^ 2) > 20
            if hops < 6 and moved and not path.normal then
                -- stop well short of the pack so we don't land in aggro range
                local i = #path.points
                while i > 1 do
                    local p = path.points[i]
                    if math.sqrt((p.x - s.pack.x) ^ 2 + (p.y - s.pack.y) ^ 2) > s.approach.max + 40 then break end
                    i = i - 1
                end
                local p = path.points[i]
                local ground = leader:height(p.x, p.y, p.z + 4)
                local z = (ground and ground > -100000 and math.abs(ground - p.z) < 6) and ground or p.z
                leader:tp(p.x, p.y, z + 1.0, here.o)
                return after(1500, function()
                    local ok, e = pcall(placeLeader, leader, s, hops + 1)
                    if not ok then fail(leader, tostring(e)) end
                end)
            end
            return fail(leader, err)
        end
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
        -- GM mode while setting up: mobs ignore the leader until the party is in place
        leader:gm(true)
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
            say(leader, ".scenario setspot <id>  把你现在站的位置存为这个场景的开怪站位")
        end
    elseif args:match("^setspot") then
        local id = args:match("^setspot%s+(%S+)") or scenario.current
        local s = id and scenario.find(id)
        if not s then return say(leader, "用法：.scenario setspot <场景id>（例如 strat）") end
        local p = leader:pos()
        if p.map ~= s.entry.map then return say(leader, "你不在这个场景的地图里") end
        if not leader:alive() or leader:incombat() then return say(leader, "活着且不在战斗中才能记录站位") end
        scenario.spots[s.id] = { map = p.map, x = p.x, y = p.y, z = p.z + 0.5, o = p.o }
        saveSpots()
        say(leader, string.format("%s 的站位已保存（%.1f, %.1f, %.1f），下次点这个场景会传到这里", s.title, p.x, p.y, p.z))
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
