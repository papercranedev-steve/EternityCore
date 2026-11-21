-- Companion profession test cases (runner: prof.lua). Checks follow the design doc
-- .kiro/docs/2026-11-21-companion-professions.md; a FAIL is a finding (code or doc), not
-- necessarily a bug.

local ALCHEMY, BLACKSMITHING, ENCHANTING, ENGINEERING = 171, 164, 333, 202
local HERBALISM, LEATHERWORKING, MINING, SKINNING, TAILORING = 182, 165, 186, 393, 197
local FIRST_AID, COOKING, FISHING = 129, 185, 356
local MINING_PICK, SKINNING_KNIFE = 2901, 7005

local PRIMARY = {
    [ALCHEMY] = true, [BLACKSMITHING] = true, [ENCHANTING] = true, [ENGINEERING] = true, [HERBALISM] = true,
    [LEATHERWORKING] = true, [MINING] = true, [SKINNING] = true, [TAILORING] = true,
}
local GATHERING = { [HERBALISM] = true, [MINING] = true, [SKINNING] = true }

-- §2.1 weights: pair, then cloth, leather, mail, plate
local PAIRS = {
    { TAILORING, ENCHANTING, 4, 1, 0, 0 },
    { HERBALISM, ALCHEMY, 3, 2, 2, 1 },
    { SKINNING, LEATHERWORKING, 0, 4, 4, 0 },
    { MINING, BLACKSMITHING, 0, 0, 0, 4 },
    { MINING, ENGINEERING, 1, 1, 2, 2 },
    { HERBALISM, MINING, 1, 1, 1, 1 },
    { HERBALISM, SKINNING, 1, 2, 1, 0 },
    { MINING, SKINNING, 0, 0, 1, 1 },
}
local ARMOR = { [8] = 1, [5] = 1, [9] = 1, [4] = 2, [11] = 2, [3] = 3, [7] = 3, [1] = 4, [2] = 4 }
local ARMOR_NAME = { "cloth", "leather", "mail", "plate" }

local function pairWeight(a, b, class)
    for _, p in ipairs(PAIRS) do
        if (p[1] == a and p[2] == b) or (p[1] == b and p[2] == a) then return p[2 + ARMOR[class]] end
    end
    return 0
end

-- §2.2: level x 5 (at most 300) - 75, +-15, at least 1, at most 225
local function valueRange(level)
    local e = math.min(level * 5, 300) - 75
    return math.max(1, e - 15), math.max(1, math.min(225, e + 15))
end

local function rankMax(v)
    if v <= 75 then return 75 elseif v <= 150 then return 150 elseif v <= 225 then return 225 end
    return 300
end

-- §2.1-§2.3 on one rolled bot
local function checkRecruit(t, b)
    local p = b:prof()
    local name = b:name() .. " (lv" .. b:level() .. ")"
    local prim, sec = {}, {}
    for _, s in ipairs(p.list) do
        if PRIMARY[s.skill] then prim[#prim + 1] = s else sec[s.skill] = s end
    end
    t.check(#prim == 2, name .. ": 2 primary professions (has " .. #prim .. ")")
    t.check(sec[FIRST_AID] and sec[COOKING] and sec[FISHING], name .. ": first aid, cooking, fishing known")
    if #prim == 2 then
        local w = pairWeight(prim[1].skill, prim[2].skill, b:class())
        t.check(w > 0, string.format("%s: pair %s + %s allowed for %s (weight %d)", name, prim[1].name, prim[2].name,
            ARMOR_NAME[ARMOR[b:class()]], w))
        t.log(string.format("PAIR %s %s %s+%s%s", ARMOR_NAME[ARMOR[b:class()]], name, prim[1].name, prim[2].name,
            (GATHERING[prim[1].skill] and GATHERING[prim[2].skill]) and " (gathering only)" or ""))
    end
    local lo, hi = valueRange(b:level())
    for _, s in ipairs(prim) do
        t.check(s.value >= lo and s.value <= hi, string.format("%s: %s value %d in %d..%d", name, s.name, s.value, lo, hi))
        t.check(s.max == rankMax(s.value), string.format("%s: %s max %d = lowest rank holding it (%d)", name, s.name, s.max, rankMax(s.value)))
        if s.tool and s.tool > 0 then
            t.check(s.hastool, string.format("%s: %s comes with its tool %d", name, s.name, s.tool))
        end
        if s.kind == "crafting" then
            t.check(s.recipes.known > 0, string.format("%s: %s knows the basic recipes (known %d)", name, s.name, s.recipes.known))
            local older = s.recipes.below - s.recipes.known - s.recipes.recentmissing
            t.check(older <= 0, string.format("%s: %s every older recipe known (known %d, at or below %d, recent missing %d)",
                name, s.name, s.recipes.known, s.recipes.below, s.recipes.recentmissing))
        end
        if s.artisan then t.check(s.max < 300, name .. ": " .. s.name .. " no Artisan at recruiting") end
    end
    for _, s in pairs(sec) do
        t.check(s.value <= 150 and s.max <= 150, string.format("%s: %s %d/%d within Journeyman (150)", name, s.name, s.value, s.max))
        t.check(s.value >= math.min(lo, 150), string.format("%s: %s value %d not below %d", name, s.name, s.value, math.min(lo, 150)))
    end
end

local function recruitSteps(t)
    for _, b in ipairs(t.bots) do checkRecruit(t, b) end
end

local function gain(r, skill) return r.gains[skill] or 0 end

return {
    {
        id = "recruit", title = "招来：分专业和技能值",
        hint = "自动：4 个职业 4 个等级各掷一次专业，查搭配、技能值范围、等级上限、配方。点重开会换人再掷。",
        bots = {
            { class = "mage", level = 60, recruit = true, note = "cloth 60" },
            { class = "rogue", level = 40, recruit = true, note = "leather 40" },
            { class = "hunter", level = 20, recruit = true, note = "mail 20" },
            { class = "warrior", level = 1, recruit = true, note = "plate 1" },
        },
        steps = recruitSteps,
    },
    {
        id = "recruit-gatherer", title = "招来：采集狂",
        hint = "自动：带采集狂标签的 4 个人掷专业，纯采集搭配权重 x3。多点几次重开看搭配分布（日志 PAIR 行）。",
        bots = {
            { class = "priest", level = 60, recruit = true, tags = { "gatherer" }, note = "cloth 60" },
            { class = "druid", level = 30, recruit = true, tags = { "gatherer" }, note = "leather 30" },
            { class = "hunter", level = 50, recruit = true, tags = { "gatherer" }, note = "mail 50" },
            { class = "warrior", level = 40, recruit = true, tags = { "gatherer" }, note = "plate 40" },
        },
        steps = recruitSteps,
    },
    {
        id = "offline-rate", title = "离线：每小时和每天上限",
        hint = "自动：普通的和手巧的各离线 1 小时、10 小时、再 10 小时，查每小时 3/4 点、每天 20/30 点。",
        bots = {
            { class = "mage", level = 40, money = 500000, profs = { { TAILORING, 100 }, { ENCHANTING, 100 } }, note = "普通，50g" },
            { class = "warrior", level = 40, money = 500000, tags = { "crafty" },
              profs = { { BLACKSMITHING, 100 }, { ENGINEERING, 100 } }, note = "手巧，50g" },
        },
        steps = function(t)
            local mage, war = t.bot("mage"), t.bot("warrior")
            local r = t.away(mage, 1)
            t.check(gain(r, TAILORING) == 3 and gain(r, ENCHANTING) == 3, "normal, 1h: +3 each crafting profession")
            r = t.away(war, 1)
            t.check(gain(r, BLACKSMITHING) == 4 and gain(r, ENGINEERING) == 4, "crafty, 1h: +4 each")
            r = t.away(mage, 10)
            t.check(gain(r, TAILORING) == 17 and gain(r, ENCHANTING) == 17, "normal, 10h more: +17 each (20 a day)")
            t.check(t.skill(mage, TAILORING).today == 20, "normal: today 20")
            r = t.away(war, 10)
            t.check(gain(r, BLACKSMITHING) == 26 and gain(r, ENGINEERING) == 26, "crafty, 10h more: +26 each (30 a day)")
            r = t.away(mage, 10)
            t.check(next(r.gains) == nil and not r.short, "normal, day used up: no gain, not 'short of money'")
            t.check(r.spent == 0, "normal, day used up: nothing spent")
        end,
    },
    {
        id = "offline-money", title = "离线：钱和性格",
        hint = "自动：没钱的、节俭、普通、败家各离线 5 小时（同样的专业，20g），查钱不够时不涨、花钱比例 30%/50%/80% 以内。",
        bots = {
            { class = "mage", level = 40, money = 0, profs = { { TAILORING, 160 }, { ENCHANTING, 160 } }, note = "没钱" },
            { class = "priest", level = 40, money = 200000, tags = { "frugal" }, profs = { { TAILORING, 160 }, { ENCHANTING, 160 } }, note = "节俭 20g" },
            { class = "hunter", level = 40, money = 200000, profs = { { TAILORING, 160 }, { ENCHANTING, 160 } }, note = "普通 20g" },
            { class = "rogue", level = 40, money = 200000, tags = { "spendthrift" }, profs = { { TAILORING, 160 }, { ENCHANTING, 160 } }, note = "败家 20g" },
        },
        steps = function(t)
            local r = t.away(t.bot("mage"), 5)
            t.check(next(r.gains) == nil and r.short and r.spent == 0, "no money: no gain, short of money, nothing spent")
            local spent = {}
            for _, w in ipairs({ { "priest", 0.3, "frugal" }, { "hunter", 0.5, "normal" }, { "rogue", 0.8, "spendthrift" } }) do
                r = t.away(t.bot(w[1]), 5)
                spent[w[3]] = r.spent
                local free = math.max(0, r.before - r.reserve)
                t.check(r.spent <= free * w[2], string.format("%s: spent %s <= %d%% of the free money %s (20g - reserve %s)",
                    w[3], gold(r.spent), w[2] * 100, gold(free), gold(r.reserve)))
                -- a point at 160 costs well under 1g
                t.check(r.spent > 0 or free * w[2] < 10000, string.format("%s: spent something when %d%% of the free money is 1g or more", w[3], w[2] * 100))
            end
            t.log(string.format("SPENT frugal %s, normal %s, spendthrift %s", gold(spent.frugal), gold(spent.normal), gold(spent.spendthrift)))
        end,
    },
    {
        id = "offline-gather", title = "离线：采集和工具",
        hint = "自动：没工具不涨；给矿锄和剥皮刀后 7 小时不涨、24 小时 +3、再 24 小时不涨；采集狂 24 小时 +5。",
        bots = {
            { class = "warrior", level = 30, money = 10000, profs = { { MINING, 50 }, { SKINNING, 50 } }, note = "没工具" },
            { class = "druid", level = 30, money = 10000, tags = { "gatherer" }, items = { { SKINNING_KNIFE, 1 } },
              profs = { { HERBALISM, 50 }, { SKINNING, 50 } }, note = "采集狂，有剥皮刀" },
        },
        steps = function(t)
            local war, druid = t.bot("warrior"), t.bot("druid")
            local r = t.away(war, 24)
            t.check(next(r.gains) == nil, "no tools, 24h: no gain")
            t.check(not t.skill(war, MINING).hastool and not t.skill(war, SKINNING).hastool, "no tools: hastool false")
            t.give(war, MINING_PICK)
            t.give(war, SKINNING_KNIFE)
            t.check(t.skill(war, MINING).hastool and t.skill(war, SKINNING).hastool, "tools given: hastool true")
            r = t.away(war, 7)
            t.check(next(r.gains) == nil, "tools, 7h: no gain (a point every 8h)")
            r = t.away(war, 24)
            t.check(gain(r, MINING) == 3 and gain(r, SKINNING) == 3, "tools, 24h: +3 each")
            r = t.away(war, 24)
            t.check(next(r.gains) == nil, "day used up: no gain")
            t.check(r.spent == 0 and not r.short, "gathering costs nothing")
            r = t.away(druid, 24)
            t.check(gain(r, HERBALISM) == 5 and gain(r, SKINNING) == 5, "gatherer, 24h: +5 each (every 4h, 5 a day; herbalism needs no tool)")
        end,
    },
    {
        id = "town-trainer", title = "进城：学熟练和配方",
        at = "capital", watch = true,
        hint = "传到主城。站着别动等它们问进城，答应。看它们最后去专业训练师学熟练。不对就点“记一笔”。",
        bots = {
            { class = "mage", level = 30, money = 100000, profs = { { TAILORING, 75 }, { ENCHANTING, 70 } }, note = "裁缝 75/75、附魔 70/75，10g" },
            { class = "warrior", level = 30, money = 100000, items = { { MINING_PICK, 1 } },
              profs = { { BLACKSMITHING, 75 }, { MINING, 75 } }, note = "锻造 75/75、采矿 75/75，10g" },
        },
    },
    {
        id = "artisan", title = "大师之行",
        at = "capital", watch = true,
        hint = "传到主城。等它们问大师级（一次进城问一次），答“带你去”。跟着任务日志和地图红旗去训练师。",
        bots = {
            { class = "warrior", level = 60, money = 500000, items = { { MINING_PICK, 1 } },
              profs = { { BLACKSMITHING, 200 }, { MINING, 200 } }, note = "锻造 200/225、采矿 200/225，50g" },
            { class = "mage", level = 60, money = 500000, profs = { { TAILORING, 200 }, { ENCHANTING, 200 } }, note = "裁缝 200/225、附魔 200/225，50g" },
        },
    },
    {
        id = "gather-online", title = "在线采集",
        watch = true,
        hint = "在原地生成（和你同级）。去打野兽、找草药和矿点，看它们剥皮、采集，技能涨。你自己的同样专业会让给你。",
        bots = {
            { class = "hunter", level = 0, money = 10000, items = { { SKINNING_KNIFE, 1 } },
              profs = { { SKINNING, 1 }, { LEATHERWORKING, 1 } }, note = "剥皮 1，有剥皮刀" },
            { class = "druid", level = 0, money = 10000, items = { { MINING_PICK, 1 } },
              profs = { { HERBALISM, 1 }, { MINING, 1 } }, note = "草药 1、采矿 1，有矿锄" },
        },
    },
}
