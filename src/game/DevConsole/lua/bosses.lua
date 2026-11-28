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
local DONE = 3

-- instance data i of the leader's map (".instance getdata" prints "Data[i] = v")
local function instData(leader, i)
    leader:select(leader)
    return tonumber(tostring(leader:cmd(".instance getdata " .. i)):match("=%s*(%d+)"))
end

-- set instance data i to DONE. The handler runs again each time, which a respawned boss needs (its
-- template flags are back); once = only when not DONE yet, for handlers that toggle a door with
-- DoUseDoorOrButton (a second DONE would close it again)
local function markDone(leader, i, once)
    if once and instData(leader, i) == DONE then return end
    leader:select(leader)
    leader:cmd(".instance setdata " .. i .. " " .. DONE)
end

local function killEntries(leader, radius, entries)
    local want = {}
    for _, e in ipairs(entries) do want[e] = true end
    for _, c in ipairs(leader:nearby(radius, "alive")) do
        if not c:isplayer() and want[c:entry()] then c:kill() end
    end
end

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

    -- Bosses with mechanics from .kiro/docs/2026-11-28-dungeon-boss-encounters.md (section 2).
    -- Spawns checked against `creature` 2026-11-28. Level = the boss's level (60 at most).
    {
        -- 4275 in the top room; Shadow Port to 3 fixed points (some on the ledges)
        id = "arugal", title = "影牙城堡 大法师阿鲁高", level = 26,
        entry = { map = 33, x = -76.75, y = 2152.41, z = 155.79, o = 2.496 },
        pack = { x = -76.75, y = 2152.41, z = 155.79 },
        bosses = { 16255 },
        clearRadius = 50,
        hint = "每 22-48 秒暗影传送到房间 3 个固定点之一（有的在高台上）；虚空箭；阿鲁高的诅咒把人变成狼人并控制 10 秒，自己会好，不用打他；近身时放雷霆震击。",
    },
    {
        -- boss_arcanist_doan, library wing
        id = "doan", title = "血色修道院 奥法师杜安", level = 37,
        entry = { map = 189, x = 148.32, y = -428.69, z = 18.49, o = 6.248 },
        pack = { x = 148.32, y = -428.69, z = 18.49 },
        bosses = { 40124 },
        clearRadius = 60,
        hint = "50% 放奥术屏障（免疫）然后读爆炸（30 码，伤害很高），全队包括坦克都要跑出 30 码。平时有近身魔爆术、群体沉默、随机变羊。",
    },
    {
        -- boss_herod, armory wing; 20 Scarlet Trainees spawn when he dies
        id = "herod", title = "血色修道院 赫洛德", level = 40,
        entry = { map = 189, x = 1965.09, y = -431.61, z = 6.26, o = 2.985 },
        pack = { x = 1965.09, y = -431.61, z = 6.26 },
        bosses = { 89097 },
        clearRadius = 60,
        hint = "每 20-30 秒旋风斩，近战要出去；50% 激怒。死后刷 20 个血色新兵冲过来，别急着坐下。",
    },
    {
        -- boss_archaedas: stone until someone uses the Altar of Archaedas (GO 133234, 9 yd from
        -- him). The wall earthen (custodians 7309, hallshapers 7077), the guardians 7076 and the
        -- vault warders 10120 are frozen adds he wakes: kept and respawned. Leaving 38 yd = evade.
        id = "archaedas", title = "奥达曼 阿扎达斯", level = 47,
        entry = { map = 70, x = 104.34, y = 272.31, z = -51.70, o = 3.648 },
        pack = { x = 104.34, y = 272.31, z = -51.70 },
        bosses = { 33537 },
        bossEntries = { 7309, 7077, 7076, 10120 },
        clearRadius = 70,
        hint = "右键他脚边的祭坛开打。每 10 秒唤醒墙边土灵，66% 唤醒守护者，33% 唤醒石穴看守者（读条 5 秒）；地面震颤群体昏迷。出房间会脱战。打完重来先 .instance unbind。",
    },
    {
        -- boss_noxxion
        id = "noxxion", title = "玛拉顿 诺克赛恩", level = 48,
        entry = { map = 349, x = 1130.40, y = -191.34, z = -80.00, o = 4.643 },
        pack = { x = 1130.40, y = -191.34, z = -80.00 },
        bosses = { 54445 },
        clearRadius = 60,
        hint = "每 40 秒不能选中 15 秒，召唤 5 个幼体（用群攻清）；剧毒箭雨要驱毒。",
    },
    {
        -- 5710 (creature_spells) + Ogom the Wretched 39827 (creature_groups). Behind the barrier
        -- GO 149431 and immune until the six protectors on the upper ring (5712-5717, up to 256 yd
        -- away) are dead and TYPE_PROTECTORS (5) is DONE: setup kills them and sets it.
        id = "jammalan", title = "阿塔哈卡神庙 预言者迦玛兰", level = 54,
        entry = { map = 109, x = -426.68, y = -85.74, z = -88.14, o = 3.211 },
        pack = { x = -426.68, y = -85.74, z = -88.14 },
        bosses = { 39737, 39827 },
        clearRadius = 60,
        setup = function(leader)
            killEntries(leader, 300, { 5712, 5713, 5714, 5715, 5716, 5717 })
            markDone(leader, 5)
        end,
        hint = "楼上 6 个保护者已杀、屏障已开。妖术（变身，诅咒类，之后被控）要快驱散；烈焰风暴地面区域要躲；地缚图腾；80% 以下自己放治疗波。奥戈姆一起上。",
    },
    {
        -- boss_general_angerforge: 8 reservists + 2 medics below 30% (once)
        id = "angerforge", title = "黑石深渊 安格弗将军", level = 57,
        entry = { map = 230, x = 652.39, y = 21.37, z = -59.98, o = 0.087 },
        pack = { x = 652.39, y = 21.37, z = -59.98 },
        bosses = { 45954 },
        clearRadius = 60,
        hint = "低于 30% 叫来 8 个预备兵和 2 个医师（医师会治疗，先杀）。蓝不满别开。",
    },
    {
        -- npc_golem_lord_argelmach: on aggro every live Ragereaver Golem 8906 / Wrath Hammer
        -- Construct 8907 within 80 yd joins (3 + 3, 6-73 yd). They are kept: the test is about
        -- the pull (clear them first or fight them all).
        id = "argelmach", title = "黑石深渊 傀儡统帅阿格曼奇", level = 57,
        entry = { map = 230, x = 846.80, y = 16.28, z = -53.64, o = 3.142 },
        pack = { x = 846.80, y = 16.28, z = -53.64 },
        bosses = { 45931 },
        adds = { 8906, 8907 },
        clearRadius = 80,
        approach = { min = 25, max = 40, clear = 15 },
        hint = "房间里 6 个傀儡保留着：开打时 80 码内活着的傀儡全部上来，开怪前要先一个个清掉。闪电护盾、闪电链、震击。",
    },
    {
        -- 9156 (creature_spells); Burning Spirits come from the dwarf runes (instance_blackrock_depths)
        id = "flamelash", title = "黑石深渊 弗莱拉斯大使", level = 57,
        entry = { map = 230, x = 1009.75, y = -239.02, z = -61.30, o = 2.304 },
        pack = { x = 1009.75, y = -239.02, z = -61.30 },
        bosses = { 47778 },
        clearRadius = 60,
        hint = "战斗中燃烧之魂从房间四周的符文刷出走向他，每碰到一只他叠一层燃烧之魂，要在路上拦住打掉。",
    },
    {
        -- Tomb of the Seven: Anger'rel 9035, Seeth'rel 9038, Dope'rel 9040, Gloom'rel 9037,
        -- Vile'rel 9036, Hate'rel 9034, Doom'rel 9039 (boss_doomrel). Friendly until Doom'rel's
        -- gossip starts the event; once per instance.
        id = "seven", title = "黑石深渊 七贤之墓", level = 57,
        entry = { map = 230, x = 1281.14, y = -282.15, z = -78.14, o = 2.251 },
        pack = { x = 1281.14, y = -282.15, z = -78.14 },
        bosses = { 91020, 91021, 91022, 91023, 91024, 91025, 91026 },
        clearRadius = 70,
        hint = "和杜姆雷尔对话开始。矮人一个接一个上，维尔雷尔是治疗，最后杜姆雷尔自己上并召唤虚空行者。整场不许坐下。打完重来先 .instance unbind。",
    },
    {
        -- boss_magmus (Iron Hall, before the throne room); the statues breathe fire at fixed spots
        id = "magmus", title = "黑石深渊 玛格姆斯", level = 57,
        entry = { map = 230, x = 1380.72, y = -659.31, z = -91.97, o = 1.588 },
        pack = { x = 1380.72, y = -659.31, z = -91.97 },
        bosses = { 47477 },
        clearRadius = 60,
        hint = "爆炸，低于 51% 战争践踏。大厅两边的矮人雕像向固定位置喷火，站位要避开。",
    },
    {
        -- boss_emperor_dagran_thaurissan 47613 + boss_moira_bronzebeard 47217
        id = "thaurissan", title = "黑石深渊 索瑞森大帝和茉艾拉", level = 60,
        entry = { map = 230, x = 1380.18, y = -831.65, z = -87.59, o = 1.326 },
        pack = { x = 1382.0, y = -831.5, z = -87.59 },
        bosses = { 47613, 47217 },
        clearRadius = 70,
        hint = "茉艾拉公主每 10 秒治疗大帝，还会加盾、心灵震爆（联盟任务要她活着，部落可以先杀）；大帝：索瑞森之手随机昏迷，每 18 秒烈焰化身群体火焰伤害。",
    },
    {
        -- boss_overlord_wyrmthalak: 2 Spirestone Warlords + 2 Smolderthorn Berserkers behind
        -- the party below 50%
        id = "wyrmthalak", title = "黑石塔下层 维姆萨拉克", level = 60,
        entry = { map = 229, x = -22.63, y = -486.19, z = 90.75, o = 3.142 },
        pack = { x = -22.63, y = -486.19, z = 90.75 },
        bosses = { 45757 },
        clearRadius = 70,
        hint = "低于 50% 从身后刷 2 个尖石督军和 2 个烟棘狂战士，副坦接住。冲击波、击退、挫志怒吼。",
    },
    {
        -- boss_jandice_barov
        id = "jandice", title = "通灵学院 詹迪斯·巴罗夫", level = 60,
        entry = { map = 289, x = 268.22, y = 73.63, z = 95.92, o = 1.641 },
        pack = { x = 268.22, y = 73.63, z = 95.92 },
        bosses = { 48807 },
        clearRadius = 50,
        hint = "每 25 秒隐身、不能选中，同时召唤一圈幻象（一打就死，用群攻清），她出来后坦克马上接住。血之诅咒，放逐。",
    },
    {
        -- boss_dathrohan_balnazzar 52149 (10812 -> 10813 at 40%), Stratholme live side
        id = "balnazzar", title = "斯坦索姆 巴纳扎尔", level = 60,
        entry = { map = 329, x = 3415.84, y = -3044.54, z = 136.81, o = 5.428 },
        pack = { x = 3415.84, y = -3044.54, z = 136.81 },
        bosses = { 52149 },
        clearRadius = 60,
        hint = "人形阶段：十字军之锤群体昏迷。40% 变身巴纳扎尔：支配（不能驱散）、心灵尖啸群体恐惧、沉睡、暗影震击。",
    },
    {
        -- boss_immol_thar: force field until the 5 pylons are off = TYPE_CRISTAL_EVENT (1) DONE;
        -- setup sets it (his guardians 11466 within clearRadius are already dead, others would
        -- attack him)
        id = "immolthar", title = "厄运之槌西 伊莫塔尔", level = 60,
        entry = { map = 429, x = -38.08, y = 812.44, z = -29.45, o = 4.695 },
        pack = { x = -38.08, y = 812.44, z = -29.45 },
        bosses = { 84376 },
        clearRadius = 70,
        setup = function(leader) markDone(leader, 1, true) end,
        hint = "5 个水晶塔已设成关掉（打过一次重来先 .instance unbind）。感染撕咬、践踏、伊莫塔尔之眼（召唤小怪，先杀）、传送门（把一个人传走）、激怒。",
    },
    {
        -- boss_prince_tortheldrin: immune to players until TYPE_IMMOL_THAR (2) is DONE
        id = "tortheldrin", title = "厄运之槌西 托塞德林王子", level = 60,
        entry = { map = 429, x = 132.63, y = 625.91, z = -48.38, o = 4.625 },
        pack = { x = 132.63, y = 625.91, z = -48.38 },
        bosses = { 56951 },
        clearRadius = 60,
        setup = function(leader) markDone(leader, 2) end,
        hint = "伊莫塔尔已设成死了。旋风斩（近战出去）、痛击、法术反制；奥术冲击击退坦克；召唤（把目标拉到身边）。",
    },
    {
        -- boss_kromcrush: 2 Gordok Reavers at 50%
        id = "kromcrush", title = "厄运之槌北 克罗卡斯指挥官", level = 60,
        entry = { map = 429, x = 627.59, y = 481.72, z = 29.46, o = 3.136 },
        pack = { x = 627.59, y = 481.72, z = 29.46 },
        bosses = { 56944 },
        clearRadius = 60,
        hint = "致死顺劈、破胆怒吼（恐惧）；50% 叫来 2 个戈多克掠夺者；25% 反击风暴，这时近战别打。",
    },
    {
        -- boss_king_gordok 128489 + boss_chorush 56945; Cho'Rush turns friendly once the king is
        -- dead (TYPE_GORDOK_TRIBUTE)
        id = "gordok", title = "厄运之槌北 戈多克大王和克鲁什", level = 60,
        entry = { map = 429, x = 829.70, y = 481.33, z = 37.40, o = 3.194 },
        pack = { x = 831.8, y = 485.4, z = 37.40 },
        bosses = { 128489, 56945 },
        clearRadius = 60,
        hint = "先杀大王：狂暴冲锋、战争践踏、致死打击。克鲁什是施法者（法师/萨满/牧师技能随机），打断和控制；大王死后他变友好，别打死。打完重来先 .instance unbind。",
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
