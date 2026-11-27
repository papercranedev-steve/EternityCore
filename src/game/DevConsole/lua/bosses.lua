-- Boss tests: the addon's Boss 测试 window (/pbboss, ".scenario bosses"). Same engine as the
-- scenarios (scenarios.lua: lineup, premades, supplies, affinity, recorder); the leader lands on
-- the boss in GM mode, everything else within clearRadius is killed, the bosses are respawned and
-- the party waits on a ring 20-30 yd from the boss (or the spot recorded with .scenario setspot).
-- Fields: see the top of scenarios.lua ("Boss tests").
--
-- Spawns from the world DB (creature guid / position). Killed trash stays dead until its respawn
-- timer; a reload kills whatever came back.

local DEFAULT_BOTS = {
    { class = "warrior", role = "tank" },
    { class = "rogue",   role = "meleedps" },
    { class = "mage",    role = "rangedps" },
    { class = "priest",  role = "healer" },
}

local GANDLING = 1853

local bosses = {
    {
        -- 21: VanCleef is 21, the Blackguards 19-20 (premade = the 19 templates)
        -- creature_groups 79336: VanCleef 79336 + stealthed Blackguards 79383, 79368. The deck
        -- (Greenskin patrol, pirates, shipbuilders 17-55 yd) is cleared.
        id = "vancleef", title = "死亡矿井 范克里夫", level = 21,
        entry = { map = 36, x = -87.37, y = -819.90, z = 39.30, o = 0.0 },
        pack = { x = -87.37, y = -819.90, z = 39.30 },
        bosses = { 79336, 79383, 79368 },
        clearRadius = 60,
        hint = "50% 召唤两个迪菲亚爪牙（法术 5200），潜行者在那之前晕住能打断召唤。两个黑衣卫兵是隐身的，开打就上来。",
    },
    {
        -- 34 = Thermaplugg (bombs 30; premade = the 29 templates)
        -- boss_thermaplugg 30139; the door GO 142207 locks when the fight starts. Leprous
        -- Machinesmiths, a Guardian and a Nullifier wander ~60 yd west, inside the door.
        id = "thermaplugg", title = "诺莫瑞根 瑟玛普拉格", level = 34,
        entry = { map = 90, x = -531.32, y = 670.16, z = -325.19, o = 2.915 },
        pack = { x = -531.32, y = 670.16, z = -325.19 },
        bosses = { 30139 },
        clearRadius = 70,
        hint = "墙上的炸弹口会放出行走炸弹，冰霜新星 / 冰锥术定住或打掉；击退（仇恨降低）。门在开打时锁上。",
    },
    {
        -- 42 = both bosses (hall trash 34-39; premade = the 39 templates)
        -- Mograine 40029; Whitemane 39946 behind door GO 104600, joins in phase 1. The pull sends
        -- every live chaplain / wizard / centurion / champion / abbot / monk within 82 yd at the
        -- party: the hall is cleared (90). The event runs once per instance: rerun = .instance unbind.
        id = "cathedral", title = "血色修道院 莫格莱尼和怀特迈恩", level = 42,
        entry = { map = 189, x = 1153.87, y = 1398.39, z = 32.61, o = 6.126 },
        pack = { x = 1153.87, y = 1398.39, z = 32.61 },
        bosses = { 40029, 39946 },
        clearRadius = 90,
        -- the hall door side, not behind the altar
        from = { x = 1100.0, y = 1398.0 },
        approach = { min = 30, max = 40, clear = 20 },
        hint = "莫格莱尼倒下后怀特迈恩出场复活他。她会群体催眠，牧师驱散制裁之锤和心灵控制。每个副本只能打一次，重打先 .instance unbind。",
    },
    {
        -- 60 = max level (Gandling 61)
        -- Gandling is summoned at (180.8, -5.4, 75.6) once the six room bosses are DONE (data 1-6);
        -- setup does that, the last one summons him at once. Wipe: he walks home (data 0 = FAIL) and
        -- stays. After a kill (data 0 = DONE) setup resets data 0 first; never while he is alive
        -- (two Gandlings). The hall's rats and the floor above (40 yd up) are cleared.
        id = "gandling", title = "通灵学院 黑暗院长加丁", level = 60,
        entry = { map = 289, x = 180.77, y = -5.43, z = 75.57, o = 1.292 },
        pack = { x = 180.77, y = -5.43, z = 75.57 },
        bosses = {},
        bossEntries = { GANDLING },
        clearRadius = 60,
        setup = function(leader)
            for _, c in ipairs(leader:nearby(150, "alive")) do
                if c:entry() == GANDLING then return end
            end
            for _, c in ipairs({ ".instance setdata 0 0", ".instance setdata 1 3", ".instance setdata 2 3",
                    ".instance setdata 3 3", ".instance setdata 4 3", ".instance setdata 5 3", ".instance setdata 6 3" }) do
                leader:select(leader)
                leader:cmd(c)
            end
        end,
        hint = "会把队员传送到周围的房间里（房间里刷骷髅守卫，不能变形，牧师束缚亡灵），奥术飞弹，暗影护盾要驱散。免疫打断。",
    },
    {
        -- 60 (Anastari 59 elite; premade = the 60 templates)
        -- 54238 on the Ziggurat platform (148 yd up); Thuzadin Necromancers / Shadowcasters /
        -- Acolytes and banshees 25-40 yd, ghouls 45-55 yd: cleared. Her death opens Ziggurat GO
        -- 175380 (TYPE_BARONESS). The Ash'ari Crystal (34 yd, above) dies too: it only counts
        -- for the Slaughterhouse gate.
        id = "anastari", title = "斯坦索姆 女男爵阿纳斯塔丽", level = 60,
        entry = { map = 329, x = 3855.34, y = -3715.75, z = 148.18, o = 1.274 },
        pack = { x = 3855.34, y = -3715.75, z = 148.18 },
        bosses = { 54238 },
        clearRadius = 60,
        hint = "附身：钻进一名队员身体里控制他（她自己隐身），把被附身的打到 25% 血以下她才出来，之后那人回到附身前的血量。女妖哀嚎群体伤害，沉默，女妖诅咒（命中下降）。",
    },
}

for _, b in ipairs(bosses) do
    b.kind = "boss"
    b.bots = b.bots or DEFAULT_BOTS
    b.approach = b.approach or { min = 20, max = 30, clear = 20 }
    -- a reload replaces the entry instead of adding a second one
    for i = #scenario.list, 1, -1 do
        if scenario.list[i].id == b.id then table.remove(scenario.list, i) end
    end
    scenario.list[#scenario.list + 1] = b
end
