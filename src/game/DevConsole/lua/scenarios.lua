-- Test scenarios: .scenario <id> builds a party at a fixed place for the TEST account.
--
-- The leader (the player) gets the scenario level, a premade spec and premade gear for its class.
-- Bots are temporary party bots with the lineup's class/role, premade spec and premade gear, so
-- every run with the same lineup starts the same; on top, what their level gives (u:trainup:
-- trainer spells, class quest abilities, talent points) and full supplies (stockUp). The recorder (recorder.lua) is started for the party.
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
--   bots               default lineup { {class=, role=, items=}, ... } (1-4 bots)
--                      items: { {itemId, count}, ... } extra items for a bot of this class and
--                      role, added after the premade gear with the supplies (stockUp)
--   spotOf             id whose recorded spot (.scenario setspot) this scenario shares
--   respawn            what to respawn on load: radius around 'pack' (default 120), or a list of
--                      `creature` guids (bosses whose hall the tester cleared by hand)
--   cleared            `creature` guids killed on load and never respawned (mobs the party is
--                      assumed to have cleared on the way, e.g. one standing on the spot)
--   commands           GM commands the leader runs once the party is in place (event triggers)
--   setup              function(leader, s) run after 'commands' (instance state that needs a check)
--   fresh              true: the event or boss runs once per instance, so every load (start and
--                      重开) unbinds the leader from the map and enters a new instance (stepping out
--                      to OUTSIDE first when already inside)
--
-- Boss tests (kind = "boss", bosses.lua; the addon's Boss 测试 window, not the scenario list):
--   bosses             `creature` guids of the boss and its linked adds: respawned on load, never cleared
--   bossEntries        creature entries never cleared (summoned bosses, e.g. Gandling; frozen adds
--                      that wake during the fight, e.g. Archaedas's earthen); static spawns of them
--                      are respawned on load. They do not block the waiting spot.
--   adds               creature entries of awake adds the boss calls in (Argelmach's golems): never
--                      cleared, static spawns respawned on load, but the waiting spot keeps clear of them
--   clearRadius        on load every other living creature within this many yards of 'pack' is
--                      killed (the leader is still in GM mode), so the party walks straight in
--   entry              the boss's own position: the leader lands there in GM mode, clears, and the
--                      spot is searched on a ring around the boss (ringSpot) unless one is recorded
--   from               {x, y} optional: prefer ring spots on this side (the room's door)
--   hint               one line of mechanics / lineup notes for the window's tooltip (no ';')
--
-- Lineup: ".scenario <id> [class:role ...]" replaces the default bots, e.g.
-- ".scenario stockade paladin:tank rogue:meleedps hunter:rangedps druid:healer". The last lineup is
-- kept for ".scenario reset" unless reset gets its own.
-- "class:role:affinity" (0-100, e.g. "rogue:meleedps:60") makes that bot a temporary soul
-- companion of the leader with this affinity (setcompanion / setaffinity: in memory only, gone
-- when it is removed). Without it the bot is a plain party bot as before.

scenario = scenario or {}

-- GM Island: where a 'fresh' load steps out of the instance before unbinding
local OUTSIDE = { map = 1, x = 16226.2, y = 16257.0, z = 13.2, o = 0.0 }   -- game_tele GMIsland

local LEADER_ROLE = {
    [1] = "meleedps", [2] = "meleedps", [3] = "rangedps", [4] = "meleedps", [5] = "rangedps",
    [7] = "meleedps", [8] = "rangedps", [9] = "rangedps", [11] = "meleedps",
}

local CLASS_ID = {
    warrior = 1, paladin = 2, hunter = 3, rogue = 4, priest = 5, shaman = 7, mage = 8, warlock = 9, druid = 11,
}

-- same as the addon's CLASS_ROLES ("auto" is always allowed)
local CLASS_ROLES = {
    warrior = { tank = true, meleedps = true },
    paladin = { tank = true, healer = true, meleedps = true },
    hunter  = { rangedps = true },
    rogue   = { meleedps = true },
    priest  = { healer = true, rangedps = true },
    shaman  = { healer = true, meleedps = true, rangedps = true },
    mage    = { rangedps = true },
    warlock = { rangedps = true },
    druid   = { tank = true, healer = true, meleedps = true, rangedps = true },
}

local HORDE_RACES = { [2] = true, [5] = true, [6] = true, [8] = true }
local FACTION_CLASS = { paladin = "alliance", shaman = "horde" }

-- Supplies for every bot (stockUp), so a soul companion (it buys its own, nothing is free) does not
-- run dry during a test, plain bots alike:
--  - four bags (Traveler's Backpack, 16 slots);
--  - u:stock(): what a companion keeps (Shared/Supplies): ammo for the worn ranged weapon, food,
--    drink, buff and resurrection reagents, the Ankh, pet food, conjured food and water, poisons;
--    its food and drink (bought or conjured) topped up to FOOD_STACKS full stacks;
--  - healing potions, and mana potions for mana users: the best one it can use (ItemKit uses them);
--  - what no vendor sells: Soul Shards (the shaman's totems come with u:trainup's class quests).
local BAG = 4500
local HEAL_POTIONS = { 13446, 3928, 1710, 929, 858, 118 }     -- Major .. Minor Healing Potion
local MANA_POTIONS = { 13444, 13443, 6149, 3827, 3385, 2455 }  -- Major .. Minor Mana Potion
local POTIONS = 5
local FOOD_STACKS = 5   -- food and drink from u:stock topped up to 5 full stacks each (one ran out)
local MANA_CLASSES = { [2] = true, [3] = true, [5] = true, [7] = true, [8] = true, [9] = true, [11] = true }
local CLASS_EXTRAS = {
    [9] = { { 6265, 20 } },     -- warlock: Soul Shard
}

local function bestUsable(u, ids)
    for _, id in ipairs(ids) do
        if u:canuse(id) then return id end
    end
end

-- after the premade gear (clearbags empties the bags first); 'extra' = the scenario's items
local function stockUp(u, extra)
    u:wearbags(BAG)
    local items = {}
    for _, it in ipairs(CLASS_EXTRAS[u:class()] or {}) do items[#items + 1] = it end
    for _, it in ipairs(extra or {}) do items[#items + 1] = it end
    for _, it in ipairs(items) do
        if u:itemcount(it[1]) < (it[2] or 1) then u:additem(it[1], (it[2] or 1) - u:itemcount(it[1])) end
    end
    u:stock(FOOD_STACKS)
    local heal = bestUsable(u, HEAL_POTIONS)
    if heal then u:additem(heal, POTIONS) end
    local mana = MANA_CLASSES[u:class()] and bestUsable(u, MANA_POTIONS)
    if mana then u:additem(mana, POTIONS) end
end

local function lineupText(lineup)
    local parts = {}
    for _, b in ipairs(lineup) do
        parts[#parts + 1] = b.class .. ":" .. (b.role or "auto") .. (b.affinity and (":" .. b.affinity) or "")
    end
    return table.concat(parts, ",")
end

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
            -- the tank's premade crossbow (Blackcrow) gets its arrows from stockUp: tests the
            -- ranged pull with ammo
            { class = "warrior", role = "tank" },
            { class = "rogue",   role = "meleedps" },
            { class = "mage",    role = "rangedps" },
            { class = "priest",  role = "healer" },
        },
    },
    {
        -- 34: the pack is 33-34 (premade = the 29 templates; try 39 if they are too weak)
        -- Pack: creature_groups 40048 (Scarlet Adept 40051 healer/Holy Smite + Gallants 40048, 40052).
        -- Entrance (areatrigger 614) is ~85 yd away through the first hall. No closer pack with the
        -- same mechanics: 40057 (Adept + Gallant, 38 yd from the door) has 40064 19 yd and 40061 13 yd away.
        id = "library", title = "血色修道院图书馆 施法者和治疗怪（场景 2）", level = 34,
        entry = { map = 189, x = 253.67, y = -206.62, z = 18.68, o = 4.712 },
        pack = { x = 220.7, y = -286.9, z = 19.3 },
        -- No computed spot: patrol 40047 walks the corridor 20 yd west of the pack, groups 40073
        -- (19.5 yd), 40045 (25 yd), singles 40049/40077/40080 (25-38 yd). Record one.
        needsRecordedSpot = true,
        approach = { min = 24, max = 34, clear = 22 },
        bots = {
            -- Kick / Counterspell lock the Holy school (Smite + Heal); the priest dispels Hammer of Justice
            { class = "warrior", role = "tank" },
            { class = "rogue",   role = "meleedps" },
            { class = "mage",    role = "rangedps" },
            { class = "priest",  role = "healer" },
        },
    },
    {
        -- 20: the pack is 19-20 (premade = the 19 templates)
        -- Pack: creature_groups 27369 (Druids of the Fang 27369, 27371 + Deviate Viper 27372),
        -- ~265 yd from areatrigger 228; the near-door druid groups have one druid each and
        -- ungrouped Shamblers / Ectoplasm / Venomwings 6-25 yd away.
        id = "wc", title = "哀嚎洞穴 尖牙德鲁伊（场景 3）", level = 20,
        entry = { map = 43, x = -158.44, y = 131.60, z = -74.26, o = 5.847 },
        pack = { x = -45.3, y = 371.4, z = -59.7 },
        -- Computed spot: within 60 yd only Ectoplasm 27379 (30 yd), a snake (36 yd), Viper 27378
        -- (41 yd), Druid 27384 (52 yd), all north; the water mobs are 46 yd below.
        approach = { min = 24, max = 34, clear = 22 },
        bots = {
            -- priest: the only magic dispel at 20 (Slumber). No Counterspell yet (24): Kick and Shield Bash
            { class = "warrior", role = "tank" },
            { class = "rogue",   role = "meleedps" },
            { class = "mage",    role = "rangedps" },
            { class = "priest",  role = "healer" },
        },
    },
    {
        -- 52: the pack is 50-52 (premade = the 49 templates; 60 if they are too weak)
        -- Pack: creature_groups 45906 (Fireguards 45906, 45907 + Anvilrage Guardsman 47819 + Footman
        -- 90872). Every Fireguard is 180+ yd from areatrigger 1466: no closer elemental pack.
        id = "brd", title = "黑石深渊 火焰元素（场景 5）", level = 52,
        entry = { map = 230, x = 456.93, y = 34.09, z = -68.09, o = 4.712 },
        pack = { x = 660.2, y = -155.7, z = -73.3 },
        -- No computed spot: Footman 47821 wanders at 24.7 yd, groups 47816 (25-29 yd), 45899 (34 yd),
        -- 90653 (35 yd). Record one.
        needsRecordedSpot = true,
        approach = { min = 24, max = 32, clear = 22 },
        bots = {
            -- warlock instead of mage: Fireguards are fire immune and Polymorph does not work on
            -- elementals; Banish does
            { class = "warrior", role = "tank" },
            { class = "rogue",   role = "meleedps" },
            { class = "warlock", role = "rangedps" },
            { class = "priest",  role = "healer" },
        },
    },

    {
        -- 49: waves are 42-47 (Sezz'ziz 47 elite); 49 for the 49 premade gear, 39 gear is too weak
        -- for ~55 mobs in a row.
        -- Trigger: open any Troll Cage (GO 141070-141074, ~1886, 1297, 48.3) with Executioner's Key
        -- 8444 (given by 'commands'). The prisoners walk to the stair top, waves 1-3 run on timers
        -- (instance_zulfarrak). Waves are summoned; nothing to respawn. Static mobs at the stair
        -- foot (55-80 yd) are out of aggro range, the prisoners walk into them in wave 3.
        -- The event runs once per instance: fresh = a new instance on every load.
        -- Spot fixed: the prisoners' stair-top home; they count as live units for the clear check.
        id = "zfstairs", title = "祖尔法拉克 台阶事件（场景 9）", level = 49,
        fresh = true,
        entry = { map = 209, x = 1887.2, y = 1264.5, z = 42.0, o = 1.571 },
        spot = { x = 1887.2, y = 1264.5, z = 42.0, o = 1.571 },
        pack = { x = 1886.6, y = 1297.5, z = 48.3 },
        respawn = {},
        commands = { ".additem 8444" },
        approach = { min = 24, max = 34, clear = 22 },
        bots = {
            -- priest: Dispel Magic (Shadow Word: Pain), Cure Disease (Fevered Plague)
            { class = "warrior", role = "tank" },
            { class = "rogue",   role = "meleedps" },
            { class = "mage",    role = "rangedps" },
            { class = "priest",  role = "healer" },
        },
    },

    {
        -- 44: party level of the doc (premade = the 39 templates)
        -- Stranglethorn, Mosh'Ogg camp. Pack: Shaman 624 + Spellcrafter 628 (9.8 yd apart, no
        -- creature_groups). Patrols Mauler 853 (walks through the spot) and Spellcrafter 528 (passes
        -- the Shaman at 0.7 yd) are part of the test.
        -- Entry = the spot: the leader is in GM mode while setting up.
        id = "openelite", title = "荆棘谷 Mosh'Ogg 精英与巡逻（场景 11）", level = 44,
        entry = { map = 0, x = -12349.0, y = -1020.0, z = 8.5, o = 4.53 },
        spot = { x = -12349.0, y = -1020.0, z = 8.5, o = 4.53 },
        pack = { x = -12354.9, y = -1052.8, z = 4.9 },
        -- the doc's party came down from the north: Mauler 755 (on the spot), Mauler 854, Shaman 855
        cleared = { 755, 854, 855 },
        approach = { min = 26, max = 38, clear = 20 },
        bots = {
            { class = "warrior", role = "tank" },
            { class = "rogue",   role = "meleedps" },
            { class = "mage",    role = "rangedps" },
            { class = "priest",  role = "healer" },
        },
    },
    {
        -- Scenario 12: the "strat" pack and spot (creature_groups 52157); 52158 (24-33 yd east) is the
        -- doc's adjacent pack. Own id so the recordings are tankdeath-*.jsonl.
        -- The tester kills the tank mid-fight, e.g.
        --   wowdbg.ps1 "filter(me():group(), function(m) return m:isbot() and m:ai() and m:ai().role == 'tank' end)[1]:sethp(1)"
        -- (sethp(1): the next hit kills it, a real death with a killer; kill(): dies on the spot)
        id = "tankdeath", title = "斯坦索姆 坦克阵亡与灭团（场景 12）", level = 60,
        entry = { map = 329, x = 3392.92, y = -3395.03, z = 143.14, o = 1.57 },
        pack = { x = 3687.0, y = -3597.0, z = 138.0 },
        spotOf = "strat",
        needsRecordedSpot = true,
        approach = { min = 26, max = 38, clear = 24 },
        bots = {
            { class = "warrior", role = "tank" },
            { class = "rogue",   role = "meleedps" },
            { class = "mage",    role = "rangedps" },
            { class = "priest",  role = "healer" },
        },
    },
    {
        -- 20: party level of the doc (premade = the 19 templates)
        -- Fixed spot S from the doc: ".partybot ai clear" (radius 40, nearest pack first) needs the
        -- player standing here; the path from the entrance crosses the Defias / goblin packs.
        -- After "ready": stand still and type .partybot ai clear; order R -> A -> C -> F -> D.
        -- Not checked in game: 79242 is 18 yd from S (aggro ~19 yd), the upper corridor Wizard /
        -- Taskmasters are 38 yd away 32 yd above. Re-record with setspot once a good point is found.
        id = "chainclear", title = "死亡矿井 熔炉连续清怪（场景 13）", level = 20,
        entry = { map = 36, x = -14.57, y = -385.48, z = 62.46, o = 1.57 },
        spot = { x = -225.0, y = -600.0, z = 19.8, o = 0.73 },
        -- rough centre of packs R/A/C/F/D; the default 120 yd respawn covers the whole foundry
        pack = { x = -210.2, y = -586.8, z = 22.0 },
        approach = { min = 20, max = 40, clear = 18 },
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

-- "class:role[:affinity] ..." (spaces or commas; role may be left out = auto, affinity = not a
-- companion) -> lineup, or nil, error. Empty text -> nil, nil (use the default).
local function parseLineup(leader, text)
    local lineup = {}
    local faction = HORDE_RACES[leader:race()] and "horde" or "alliance"
    for token in (text or ""):gmatch("[^%s,]+") do
        local class, role, aff = token:lower():match("^(%a+):?(%a*):?(%d*)$")
        if not class or not CLASS_ID[class] then return nil, "未知职业：" .. token end
        local affinity = tonumber(aff)
        if affinity and affinity > 100 then return nil, "好感度是 0-100：" .. token end
        role = (role ~= "") and role or "auto"
        if role ~= "auto" and not CLASS_ROLES[class][role] then
            return nil, class .. " 不能当 " .. role
        end
        if FACTION_CLASS[class] and FACTION_CLASS[class] ~= faction then
            return nil, class .. " 不是你这个阵营的职业"
        end
        lineup[#lineup + 1] = { class = class, role = role, affinity = affinity }
    end
    if #lineup == 0 then return nil, nil end
    if #lineup > 4 then return nil, "最多 4 个队友" end
    return lineup
end

local function say(p, text)
    p:tell("|cff66ccff[场景]|r " .. text)
end

-- addon message prefix of a scenario's window: boss tests have their own (PBBOSS)
local function prefixOf(s)
    return (s and s.kind == "boss") and "PBBOSS" or "PBSCN"
end

-- id: the scenario the state is about (default: the one loading, else the current one)
local function status(p, state, text, id)
    p:tell(prefixOf(scenario.find(id or scenario.running or scenario.current)) .. ";S;" .. state .. ";" .. (text or ""))
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
-- Out of combat first: in combat CanEquipItem only accepts weapons, the armor of the template
-- lands in the bags and clearbags destroys it. Strat 2026-11-15: the hunter and the resto druid
-- were flagged in combat with no enemy at finish() and ended up with weapons only.
local function applyPremades(u, level, role, gm)
    local spec = pickPremade("spec", u:class(), level, role)
    if spec then u:premade("spec", spec.entry) end
    local gear = pickPremade("gear", u:class(), level, role)
    if gear then
        u:combatstop()
        u:premade("gear", gear.entry)
        u:clearbags()
    end
    gm = gm or u
    gm:select(u)
    gm:cmd(".maxskill")
    return spec, gear
end

-- what to tell the player when a premade template does not fit the wanted role
local function premadeWarnings(u, role, spec, gear)
    local out = {}
    local function check(kind, t)
        if not t then
            out[#out + 1] = u:name() .. " 没有" .. kind .. "模板"
        elseif role ~= "auto" and t.role ~= role then
            out[#out + 1] = string.format("%s 的%s用了 %d 级 %s 模板", u:name(), kind, t.level, t.role)
        end
    end
    check("天赋", spec)
    check("装备", gear)
    return out
end

local function botsOf(leader)
    return filter(leader:group(), function(m) return m:isbot() end)
end

-- shared with the life test scenarios (tests/life/life.lua)
scenario.applyPremades = applyPremades
scenario.botsOf = botsOf
scenario.CLASS_ID = CLASS_ID

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

local function guidSet(list)
    local set = {}
    for _, g in ipairs(list or {}) do set[g] = true end
    return set
end

local function hasEntry(list, c)
    for _, e in ipairs(list or {}) do
        if c:entry() == e then return true end
    end
    return false
end

-- s.respawn: radius around the pack (default 120) or a list of `creature` guids; s.cleared stays dead.
-- Boss tests also respawn static spawns (dbguid ~= 0) of s.bossEntries and s.adds; summoned ones
-- (Gandling) are left alone.
local function respawnPack(leader, s)
    local cleared = guidSet(s.cleared)
    local respawn = s.respawn or s.bosses
    local only = type(respawn) == "table" and guidSet(respawn)
    local radius = type(respawn) == "number" and respawn or 120
    local n = 0
    for _, c in ipairs(leader:nearby(only and 250 or radius, "dead")) do
        if not c:isplayer() and not cleared[c:dbguid()] then
            local p = c:pos()
            local want
            if only then
                want = only[c:dbguid()]
                    or (c:dbguid() ~= 0 and (hasEntry(s.bossEntries, c) or hasEntry(s.adds, c)))
            else
                want = math.sqrt((p.x - s.pack.x) ^ 2 + (p.y - s.pack.y) ^ 2) < radius
            end
            if want then
                c:respawn()
                n = n + 1
            end
        end
    end
    return n
end

-- s.cleared: kill these spawns (while the leader is still in GM mode, so nothing aggroes)
local function killCleared(leader, s)
    if not s.cleared then return 0 end
    local cleared = guidSet(s.cleared)
    local n = 0
    for _, c in ipairs(leader:nearby(250, "alive")) do
        if not c:isplayer() and cleared[c:dbguid()] then
            c:kill()
            n = n + 1
        end
    end
    return n
end

local function isBoss(s, c, guids)
    return guids[c:dbguid()] or hasEntry(s.bossEntries, c)
end

-- s.clearRadius: kill every living creature within that radius of the pack except the bosses and
-- s.adds (the leader is in GM mode and stands near the pack: its grids are loaded)
local function clearAround(leader, s)
    if not s.clearRadius then return 0 end
    local guids = guidSet(s.bosses)
    local here = leader:pos()
    local off = math.sqrt((here.x - s.pack.x) ^ 2 + (here.y - s.pack.y) ^ 2 + (here.z - s.pack.z) ^ 2)
    local n = 0
    for _, c in ipairs(leader:nearby(math.min(200, s.clearRadius + off), "alive")) do
        local p = c:pos()
        local d = math.sqrt((p.x - s.pack.x) ^ 2 + (p.y - s.pack.y) ^ 2 + (p.z - s.pack.z) ^ 2)
        if not c:isplayer() and d <= s.clearRadius and not isBoss(s, c, guids) and not hasEntry(s.adds, c) then
            c:kill()
            n = n + 1
        end
    end
    return n
end

-- Boss tests: a spot on a ring around the boss (the leader stands on the boss spawn, 'entry').
-- A candidate has ground near the boss's floor, a straight-ish navmesh path and line of sight to
-- the boss, and no living non-boss creature within approach.clear. Best: on the 'from' side if
-- given, else the floor closest to the boss's, at the middle distance.
local function ringSpot(leader, s)
    local a = s.approach
    local guids = guidSet(s.bosses)
    local others = filter(leader:nearby(math.min(200, a.max + a.clear + 10), "alive"), function(c)
        return not c:isplayer() and not isBoss(s, c, guids)
    end)
    local function clearOf(x, y, z)
        for _, c in ipairs(others) do
            local p = c:pos()
            if math.sqrt((p.x - x) ^ 2 + (p.y - y) ^ 2 + (p.z - z) ^ 2) < a.clear then return false end
        end
        return true
    end
    local mid = (a.min + a.max) / 2
    local best, bestScore
    for _, d in ipairs({ a.min, mid, a.max }) do
        for k = 0, 15 do
            local ang = k * math.pi / 8
            local x, y = s.pack.x + math.cos(ang) * d, s.pack.y + math.sin(ang) * d
            local g = leader:height(x, y, s.pack.z + 4)
            local dz = g and math.abs(g - s.pack.z)
            if dz and g > -100000 and dz < 8 and clearOf(x, y, g) and leader:lospoint(x, y, g) then
                local path = leader:path(x, y, g)
                if path and path.normal and path.length < d * 1.4 then
                    local score = dz * 3 + math.abs(d - mid)
                    if s.from then
                        score = score + math.sqrt((x - s.from.x) ^ 2 + (y - s.from.y) ^ 2)
                    end
                    if not best or score < bestScore then
                        best, bestScore = { x = x, y = y, z = g + 1.0, o = math.atan(s.pack.y - y, s.pack.x - x), navz = g }, score
                    end
                end
            end
        end
    end
    return best
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
    local lineup = scenario.lineup
    local used, warnings = {}, {}
    -- group order need not follow the lineup: match each bot to an unused entry of its class,
    -- preferring the same role
    local function entryFor(b, role)
        local best
        for i, w in ipairs(lineup) do
            if not used[i] and CLASS_ID[w.class] == b:class() then
                if w.role == role or w.role == "auto" then best = i break end
                best = best or i
            end
        end
        if best then used[best] = true end
        return best and lineup[best]
    end
    for i, b in ipairs(bots) do
        local role = (b:ai() and b:ai().role) or "auto"
        local want = entryFor(b, role)
        local spec, gear = applyPremades(b, s.level, role, leader)
        for _, w in ipairs(premadeWarnings(b, role, spec, gear)) do warnings[#warnings + 1] = w end
        -- the template may be of a lower level: trainer spells, class quest abilities and talent
        -- points of the scenario level on top (before stockUp: the supplies follow the spells)
        b:trainup()
        -- extra items: from the scenario's default entry with this class and role
        local items = {}
        for _, w in ipairs(s.bots) do
            if want and w.class == want.class and w.role == role then
                items = w.items or {}
                break
            end
        end
        stockUp(b, items)
        -- a soul companion with this affinity (gates, traits, own loot, buys its own supplies)
        if want and want.affinity then
            if b:setcompanion(true, leader) then
                b:setaffinity(want.affinity)
            else
                warnings[#warnings + 1] = b:name() .. " 没能设成同伴"
            end
        end
        b:revive()
        b:refill()
        b:resetcd()
        -- small arc behind the leader
        local back = spot.o + math.pi
        local ang = back + (i - (#bots + 1) / 2) * 0.6
        b:tp(spot.x + math.cos(ang) * 3.0, spot.y + math.sin(ang) * 3.0, spot.z, spot.o)
        -- every hunter fights with a pet: a companion has none until it tames one itself (not
        -- built yet), so it gets one here as a plain bot does (after the teleport: at its feet)
        if b:class() == CLASS_ID.hunter then b:givepet() end
    end
    killCleared(leader, s)
    leader:gm(false)
    leader:refill()
    leader:resetcd()
    local respawned = respawnPack(leader, s)
    rec.watch(leader, s.id)
    for _, c in ipairs(s.commands or {}) do
        leader:select(leader)
        leader:cmd(c)
    end
    if s.setup then s.setup(leader, s) end
    local killed = scenario.killed
    scenario.killed = nil
    scenario.running = nil
    scenario.current = s.id
    local names = map(bots, function(b)
        local a = b:affinity()
        local aff = a.temporary and ("，好感 " .. a.value) or ""
        return b:name() .. "(" .. ((b:ai() and b:ai().role) or "?") .. aff .. ")"
    end)
    say(leader, string.format("%s 就绪：%d 级，队友 %s，刷新了 %d 只怪%s。%s",
        s.title, s.level, table.concat(names, " "), respawned,
        killed and string.format("，清掉了周围 %d 只", killed) or "",
        rec.enabled and ("战斗会自动录制（级别 " .. rec.level .. "）。") or "战斗录制已关闭（/pbr 里打开）。"))
    for _, w in ipairs(warnings) do say(leader, "|cffffcc00注意|r：" .. w) end
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
    local lineup = scenario.lineup
    for _, b in ipairs(lineup) do
        addbot(leader, b.class, b.role, s.level)
    end
    say(leader, "正在生成队友：" .. lineupText(lineup))
    waitfor(function()
        local bots = botsOf(leader)
        if #bots < #lineup then return false end
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
    local spot = scenario.spots[s.spotOf or s.id] or s.spot
    if s.clearRadius then
        -- boss test: the leader stands on the boss spawn; clear first, then the ring spot sees
        -- only what is left
        scenario.killed = clearAround(leader, s)
        if not spot then
            spot = ringSpot(leader, s)
            if not spot then
                return fail(leader, "boss 周围找不到站位：走到想开打的位置，输入 .scenario setspot " .. s.id .. "，再点一次")
            end
        end
    end
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

-- lineup: { {class=, role=}, ... } or nil = the scenario's default bots; fresh: new instance (s.fresh)
function scenario.load(leader, id, lineup, fresh)
    local s = scenario.find(id)
    if not s then return fail(leader, "没有这个场景：" .. tostring(id)) end
    if scenario.running then return say(leader, "上一个场景还在准备中，请稍等") end
    scenario.running = s.id
    scenario.lineup = lineup or s.bots
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
        local function inMap()
            local p = leader:pos()
            return p.map == e.map and not leader:teleporting()
        end
        local function enter()
            if leader:pos().map ~= e.map then
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
        end
        if s.fresh or fresh then
            -- a new instance: .instance unbind skips the map the player is on, so step out first
            local function unbindAndEnter()
                leader:select(leader)
                leader:cmd(".instance unbind " .. e.map)
                say(leader, "已解除副本绑定，进入新的副本")
                enter()
            end
            if leader:pos().map == e.map then
                leader:tp(OUTSIDE.map, OUTSIDE.x, OUTSIDE.y, OUTSIDE.z, OUTSIDE.o)
                waitfor(function() return leader:pos().map ~= e.map and not leader:teleporting() end, function(ok3)
                    if not ok3 then return fail(leader, "传送出副本超时") end
                    -- let the client finish loading GM Island before the next far teleport
                    after(1000, function()
                        local okp, err = pcall(unbindAndEnter)
                        if not okp then fail(leader, tostring(err)) end
                    end)
                end, 30000, 250)
            else
                unbindAndEnter()
            end
        else
            enter()
        end
    end, 10000, 250)
end

-- Reload the last scenario (after a wipe), with 'lineup' or the last one; fresh = in a new instance
-- even if the scenario is not marked fresh (.scenario fresh: instance state stuck after a run)
function scenario.reset(leader, lineup, fresh)
    if not scenario.current then return say(leader, "还没有加载过场景") end
    scenario.load(leader, scenario.current, lineup or scenario.lineup, fresh)
end

function scenario.stop(leader)
    rec.stop()
    for _, b in ipairs(botsOf(leader)) do b:removebot() end
    local id = scenario.current
    scenario.current = nil
    say(leader, "场景已结束，队友已移除")
    status(leader, "stopped", "", id)
end

-- list replies for one window: prefix;L;<id>;<title>;<level>  prefix;B;<id>;<lineup>
-- [prefix;H;<id>;<hint>]  ...  prefix;E;<current id>
local function sendList(leader, boss)
    local prefix = boss and "PBBOSS" or "PBSCN"
    for _, s in ipairs(scenario.list) do
        if (s.kind == "boss") == boss then
            leader:tell(prefix .. ";L;" .. s.id .. ";" .. s.title .. ";" .. s.level)
            leader:tell(prefix .. ";B;" .. s.id .. ";" .. lineupText(s.bots))
            if s.hint then leader:tell(prefix .. ";H;" .. s.id .. ";" .. (s.hint:gsub(";", ","))) end
        end
    end
    leader:tell(prefix .. ";E;" .. (scenario.current or ""))
end

-- .scenario [list | <id> [lineup] | reset [lineup] | fresh [lineup] | stop | status | setspot <id> | mark [text]
--            | rec list | rec open <file>]      lineup: class:role ... (see the top of this file)
-- (rec: the addon's fight log window, recview.lua)
-- list replies: PBSCN;L;<id>;<title>;<level>  PBSCN;B;<id>;<default lineup>  ...  PBSCN;E;<current id>
function scenario.command(leader, args)
    args = (args or ""):match("^%s*(.-)%s*$")
    if args == "bosses" then
        -- the addon's Boss 测试 window (/pbboss); loading, reset, stop, mark are the same commands
        sendList(leader, true)
    elseif args == "" or args == "list" then
        sendList(leader, false)
        if args == "" then
            for _, s in ipairs(scenario.list) do
                say(leader, ".scenario " .. s.id .. "  " .. s.title .. "（默认 " .. lineupText(s.bots) .. "）")
            end
            say(leader, ".scenario bosses  Boss 测试列表（插件 /pbboss）：清掉 boss 周围的怪，传到 boss 跟前")
            say(leader, ".scenario <id> 职业:职责 ...  换阵容，例如 .scenario stockade paladin:tank rogue hunter druid:healer")
            say(leader, "职业:职责:好感度  设成这个好感度的同伴（只在内存里），例如 rogue:meleedps:60")
            say(leader, ".scenario reset [阵容]  重开当前场景    .scenario stop  结束")
            say(leader, ".scenario fresh [阵容]  解除副本绑定，在新的副本里重开当前场景")
            say(leader, ".scenario setspot <id>  把你现在站的位置存为这个场景的开怪站位")
            say(leader, ".scenario mark [说明]  战斗中觉得不对时标记这一刻（记下现场，复盘时看）")
            say(leader, "战斗录像：插件的“战斗录像”窗口（/pbr）")
        end
    elseif args == "rec" or args:match("^rec%s") then
        recview.command(leader, args:match("^rec%s*(.*)$"))
    elseif args == "life" or args:match("^life%s") then
        -- life (non-combat) test scenarios: tests/life/life.lua, the addon's 生活测试 window
        lifetest.command(leader, args:match("^life%s*(.*)$"))
    elseif args == "prof" or args:match("^prof%s") then
        -- companion profession tests: tests/prof/prof.lua, the addon's 专业测试 window
        proftest.command(leader, args:match("^prof%s*(.*)$"))
    elseif args == "aff" or args:match("^aff%s") then
        -- companion affinity tests: tests/affinity/affinity.lua, the addon's 好感度测试 window
        afftest.command(leader, args:match("^aff%s*(.*)$"))
    elseif args:match("^mark") then
        local text = args:match("^mark%s+(.+)$") or ""
        if not rec.mark(leader, text) then say(leader, "现在没有在录制战斗，标记不了") end
    elseif args:match("^setspot") then
        local id = args:match("^setspot%s+(%S+)") or scenario.current
        local s = id and scenario.find(id)
        if not s then return say(leader, "用法：.scenario setspot <场景id>（例如 strat）") end
        local p = leader:pos()
        if p.map ~= s.entry.map then return say(leader, "你不在这个场景的地图里") end
        if not leader:alive() or leader:incombat() then return say(leader, "活着且不在战斗中才能记录站位") end
        scenario.spots[s.spotOf or s.id] = { map = p.map, x = p.x, y = p.y, z = p.z + 0.5, o = p.o }
        saveSpots()
        say(leader, string.format("%s 的站位已保存（%.1f, %.1f, %.1f），下次点这个场景会传到这里", s.title, p.x, p.y, p.z))
    elseif args == "stop" then
        scenario.stop(leader)
    elseif args == "status" then
        local st = rec.status()
        say(leader, "当前场景：" .. (scenario.current or "无") .. "，阵容：" .. (scenario.lineup and lineupText(scenario.lineup) or "无")
            .. "，录制：" .. (not st.enabled and "关闭" or (st.recording and "进行中" or (st.watching and "等待战斗" or "等组队")))
            .. "（级别 " .. st.level .. "）")
    else
        -- <id> [lineup] | reset [lineup]
        local id, rest = args:match("^(%S+)%s*(.*)$")
        local lineup, err = parseLineup(leader, rest)
        if err then
            -- not fail(): a scenario that is still loading must keep its 'running' flag
            say(leader, "阵容不对：" .. err)
            return status(leader, "error", err, id ~= "reset" and id or nil)
        end
        if id == "reset" then
            scenario.reset(leader, lineup)
        elseif id == "fresh" then
            scenario.reset(leader, lineup, true)
        else
            scenario.load(leader, id, lineup)
        end
    end
end
