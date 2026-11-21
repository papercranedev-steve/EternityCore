-- Companion affinity test cases (runner: affinity.lua). The oracle is the design doc
-- .kiro/docs/2026-11-21-companion-affinity.md (§2 sources, §5 unlocks), not the code's own table:
-- a FAIL is a finding (code or doc), not necessarily a bug.
-- Each gate is tested on both sides of its threshold (threshold - 1 refused, threshold allowed),
-- through the real commands the addon sends where there is one.

-- §5, in order: key, affinity, class ("warlock" / "mage" = only that class), ready (feature exists)
local DOC = {
    { "trade", 6 }, { "tradeany", 15 }, { "bags", 22 }, { "summon", 25, "warlock" }, { "craft", 30 },
    { "portal", 45, "mage" }, { "money", 50 }, { "rewards", 60 }, { "account", 100, nil, false },
}

local CHEST = 4                         -- equipment slot
local SOUL_SHARD, RUNE_OF_PORTALS = 6265, 17032
local RITUAL_OF_SUMMONING = 698
local TAILORING, BOLT_OF_LINEN_CLOTH = 197, 2963   -- skill, recipe spell
local LINEN_CLOTH, LINEN_BOLT = 2589, 2996
local BREAD, WATER = 4540, 159
local LINEN_VEST, WORN_SHORTSWORD, MOONCLOTH_BAG = 2568, 25, 14155

local function has(out, text) return out ~= nil and out:find(text, 1, true) ~= nil end

-- Every gate allowed exactly when its affinity <= the bot's (its class only, ready or not).
local function checkAllowedSet(t, b, value)
    local wrong = {}
    for _, g in ipairs(b:affinity().gates) do
        if g.allowed ~= (g.affinity <= value) then
            wrong[#wrong + 1] = string.format("%s(%d) %s", g.key, g.affinity, g.allowed and "allowed" or "refused")
        end
    end
    t.check(#wrong == 0, string.format("%s at %d: allowed = every gate at or below %d%s", b:name(), value, value,
        #wrong > 0 and ("; wrong: " .. table.concat(wrong, ", ")) or ""))
end

-- The ready gates for b's class that open after `before` up to `after` (what it should announce).
local function opening(b, before, after)
    local keys = {}
    for _, g in ipairs(b:affinity().gates) do
        if g.ready and g.forclass and g.affinity > before and g.affinity <= after then keys[#keys + 1] = g.key end
    end
    return keys
end

return {
    {
        id = "gates", title = "门槛表：每档前后一分",
        hint = "自动：门槛表和设计文档 §5 对一遍；战士在每个门槛的前一分、正好一分查哪些允许；职业限制；上限 100；不是同伴的 bot 不受限。",
        bots = {
            { id = "war", class = "warrior", level = 30, note = "普通职业" },
            { id = "lock", class = "warlock", level = 30, note = "召唤只给术士" },
            { id = "mage", class = "mage", level = 30, note = "传送门只给法师" },
        },
        steps = function(t)
            local war, lock, mage = t.bot("war"), t.bot("lock"), t.bot("mage")
            -- 1. the table against the doc
            local gates = war:affinity().gates
            t.check(#gates == #DOC, string.format("gate table has %d rows, doc §5 has %d", #gates, #DOC))
            for i, d in ipairs(DOC) do
                local g = t.gate(war, d[1])
                if t.check(g ~= nil, "gate " .. d[1] .. " exists") then
                    t.check(g.affinity == d[2], string.format("gate %s at %d (doc %d)", d[1], g.affinity, d[2]))
                    local ready = d[4] ~= false
                    t.check(g.ready == ready, string.format("gate %s ready %s (expected %s)", d[1], tostring(g.ready), tostring(ready)))
                    t.check(gates[i] and gates[i].key == d[1], string.format("row %d is %s (doc order)", i, d[1]))
                end
            end
            -- 2. both sides of every threshold, and the whole allowed set there
            for _, v in ipairs({ 0, 5, 6, 14, 15, 21, 22, 24, 25, 29, 30, 44, 45, 49, 50, 59, 60, 99, 100 }) do
                t.set(war, v)
                checkAllowedSet(t, war, v)
            end
            -- 3. class gates
            local function forclass(b, key) local g = t.gate(b, key) return g and g.forclass end
            t.check(forclass(lock, "summon") and not forclass(war, "summon") and not forclass(mage, "summon"),
                "summon applies to the warlock only")
            t.check(forclass(mage, "portal") and not forclass(war, "portal") and not forclass(lock, "portal"),
                "portal applies to the mage only")
            for _, b in ipairs({ lock, mage }) do
                t.set(b, 24) checkAllowedSet(t, b, 24)
                t.set(b, 25) checkAllowedSet(t, b, 25)
                t.set(b, 44) checkAllowedSet(t, b, 44)
                t.set(b, 45) checkAllowedSet(t, b, 45)
            end
            -- 4. range
            t.check(t.set(war, 150) == 100, "set 150: capped at 100")
            t.check(t.set(war, 0) == 0, "set 0: 0")
            -- 5. a plain party bot (not a companion) is allowed everything
            t.set(war, 0)
            war:setcompanion(false)
            local a = war:affinity()
            local refused = {}
            for _, g in ipairs(a.gates) do if not g.allowed then refused[#refused + 1] = g.key end end
            t.check(not a.companion and #refused == 0, "not a companion: every gate allowed" ..
                (#refused > 0 and (", refused " .. table.concat(refused, " ")) or ""))
            local out = t.cmd(".partybot bags " .. war:name())
            t.check(has(out, "PBINV;" .. war:name() .. ";END"), "not a companion: .partybot bags works at affinity 0")
            war:setcompanion(true, t.leader)
            t.check(war:affinity().companion and t.aff(war) == 0, "companion again, affinity 0")
        end,
    },
    {
        id = "commands", title = "门槛：真命令和交谈选项",
        hint = "自动：用插件发的真命令在门槛前后各试一次：翻包 22、拿钱 50、代工 30、召唤 25、传送门 45、挑奖励 60。站着别动，队友要在你 10 码内。",
        bots = {
            { id = "war", class = "warrior", level = 30, money = 50000, note = "5g，翻包、拿钱、挑奖励" },
            { id = "tailor", class = "priest", level = 30, profs = { { TAILORING, 75 } }, learn = { BOLT_OF_LINEN_CLOTH },
              note = "裁缝 75，会亚麻布卷，代工" },
            { id = "lock", class = "warlock", level = 40, learn = { RITUAL_OF_SUMMONING }, items = { { SOUL_SHARD, 3 } },
              note = "召唤仪式、3 个灵魂碎片" },
            { id = "mage", class = "mage", level = 60, items = { { RUNE_OF_PORTALS, 5 } }, note = "60 级，5 个传送门符文" },
        },
        steps = function(t)
            local war, tailor, lock, mage = t.bot("war"), t.bot("tailor"), t.bot("lock"), t.bot("mage")
            local n = war:name()
            -- bags (22)
            t.set(war, 21)
            local out = t.cmd(".partybot bags " .. n)
            t.check(has(out, "PBERR;" .. n .. ";locked;22;21"), "21: .partybot bags refused (locked;22;21)")
            t.check(not has(out, "PBINV;" .. n .. ";BEGIN"), "21: no bag contents sent")
            out = t.cmd(".partybot unequipslot " .. n .. " 0")
            t.check(has(out, "PBERR;" .. n .. ";locked;22;21"), "21: unequipslot refused too")
            local opts, reply = t.options(war)
            t.check(opts ~= nil and not opts.bags, "21: talk opens, no 'let me see your bags'" .. (opts and "" or (" (" .. reply .. ")")))
            t.set(war, 22)
            out = t.cmd(".partybot bags " .. n)
            t.check(has(out, "PBINV;" .. n .. ";END"), "22: .partybot bags sends his bags")
            t.check(has(out, "PBINV;" .. n .. ";M;50000;0;50"), "22: money line 5g, take not allowed yet (M;50000;0;50)")
            opts = t.options(war)
            t.check(opts ~= nil and opts.bags, "22: talk offers 'let me see your bags'")
            -- take money (50)
            local before = t.leader:money()
            t.set(war, 49)
            out = t.cmd(".partybot takemoney " .. n .. " 10000")
            t.check(has(out, "PBERR;" .. n .. ";locked;50;49"), "49: takemoney refused (locked;50;49)")
            t.check(war:money() == 50000 and t.leader:money() == before, "49: no money moved")
            t.set(war, 50)
            out = t.cmd(".partybot bags " .. n)
            t.check(has(out, "PBINV;" .. n .. ";M;50000;1;50"), "50: money line says take allowed (M;50000;1;50)")
            out = t.cmd(".partybot takemoney " .. n .. " 10000")
            t.check(has(out, "PBOK;" .. n .. ";takemoney;10000;40000"), "50: takemoney 1g ok, 4g left")
            t.check(war:money() == 40000 and t.leader:money() == before + 10000,
                string.format("50: 1g moved (his %s, yours +%s)", gold(war:money()), gold(t.leader:money() - before)))
            out = t.cmd(".partybot takemoney " .. n .. " 100000")
            t.check(has(out, "PBERR;" .. n .. ";nomoney;40000"), "50: taking 10g of 4g refused (nomoney)")
            t.leader:setmoney(before)
            t.log("STEP your money back to " .. gold(before))
            -- pick rewards (60): no command, the gate RewardChoice asks
            t.set(war, 59)
            t.check(not t.gate(war, "rewards").allowed, "59: rewards not his to pick by you")
            t.set(war, 60)
            t.check(t.gate(war, "rewards").allowed, "60: you pick his rewards")
            -- craft (30)
            local tn = tailor:name()
            t.set(tailor, 29)
            out = t.cmd(".partybot recipes " .. tn .. " " .. TAILORING)
            t.check(has(out, "PBERR;" .. tn .. ";locked;30;29"), "29: recipes refused (locked;30;29)")
            opts = t.options(tailor)
            t.check(opts ~= nil and not opts["craft:" .. TAILORING], "29: talk has no tailoring craft option")
            t.set(tailor, 30)
            out = t.cmd(".partybot recipes " .. tn .. " " .. TAILORING)
            t.check(has(out, "PBRCP;" .. tn .. ";BEGIN;" .. TAILORING), "30: recipes window opens for tailoring")
            t.check(has(out, ";R;" .. BOLT_OF_LINEN_CLOTH .. ";"), "30: Bolt of Linen Cloth listed")
            opts = t.options(tailor)
            t.check(opts ~= nil and opts["craft:" .. TAILORING], "30: talk offers tailoring craft")
            -- summon (25): you stand next to him, so the answer at 25 is 'near', not 'locked'
            local ln = lock:name()
            t.set(lock, 24)
            out = t.cmd(".partybot summon " .. ln)
            t.check(has(out, "PBERR;" .. ln .. ";locked"), "24: summon refused (locked)")
            t.set(lock, 25)
            out = t.cmd(".partybot summon " .. ln)
            t.check(not has(out, "locked") and not has(out, "notlearned"), "25: summon not locked any more")
            t.check(has(out, "PBERR;" .. ln .. ";near"), "25: refused only because you are close (near)")
            -- portal (45): the option needs a portal spell and the runes too
            t.set(mage, 44)
            opts = t.options(mage)
            t.check(opts ~= nil and not opts.portal, "44: talk has no portal")
            t.set(mage, 45)
            opts = t.options(mage)
            if not t.check(opts ~= nil and opts.portal, "45: talk offers a portal") then
                t.warn("no portal at 45: does the level 60 mage know a Portal spell? runes " .. mage:itemcount(RUNE_OF_PORTALS))
            end
        end,
    },
    {
        id = "levelup", title = "加分：升级和上限",
        hint = "自动：你在队里时他升一级 +1（升三级 +3），到 100 不再涨。要你用游戏客户端登 TEST（无头登录不算真人）。看聊天：5→6 时他会说一句交易解锁。",
        expect = {
            "5 -> level up 1 -> 6, and one party line for the trade unlock",
            "6 -> up 3 levels -> 9, no line",
            "99 -> up 1 -> 100, no line (account gate not built)",
            "100 -> up 1 -> stays 100",
        },
        bots = { { id = "war", class = "warrior", level = 20 } },
        steps = function(t)
            local war = t.bot("war")
            if t.leader:isbot() then t.warn("the leader is a bot session: no level-up points expected, checks will fail") end
            local lv = war:level()
            local function up(to, want, text)
                war:setlevel(to)
                t.log(string.format("STEP %s level %d", war:name(), to))
                t.wait(2500)
                t.check(t.aff(war) == want, string.format("%s: affinity %d (expected %d)", text, t.aff(war), want))
            end
            t.set(war, 5)
            t.wait(1500)
            up(lv + 1, 6, "1 level up")
            up(lv + 4, 9, "3 levels up")
            t.set(war, 99)
            t.wait(1500)
            up(lv + 5, 100, "99 + 1 level")
            up(lv + 6, 100, "at 100 + 1 level")
        end,
    },
    {
        id = "unlock", title = "解锁台词和信任栏",
        watch = true,
        hint = "半自动：三个人从 0 一路升到 100，每 4 秒一档。看队伍聊天每跨过一个门槛说一句；打开 伙伴→好感 页看“信任”栏跟着变绿。日志 EXPECT 写着每档该谁说哪句。",
        bots = {
            { id = "war", class = "warrior", level = 30, note = "6 句：交易、任意、翻包、代工、拿钱、挑奖励" },
            { id = "lock", class = "warlock", level = 30, note = "多一句召唤" },
            { id = "mage", class = "mage", level = 30, note = "多一句传送门" },
        },
        steps = function(t)
            local prev = 0
            for _, v in ipairs({ 5, 6, 14, 15, 21, 22, 24, 25, 29, 30, 44, 45, 49, 50, 59, 60, 99, 100 }) do
                for _, b in ipairs(t.bots) do
                    local keys = opening(b, prev, v)
                    t.set(b, v)
                    t.log(string.format("EXPECT %s says %s", b:name(), #keys > 0 and table.concat(keys, ", ") or "nothing"))
                    checkAllowedSet(t, b, v)
                end
                prev = v
                t.wait(4000)
            end
        end,
    },
    {
        id = "trade", title = "交易：5 / 6 / 14 / 15",
        watch = true,
        hint = "四个战士好感 5、6、14、15（名字后面显示），胸甲都脱了。包里给了你面包、水、亚麻外衣、破旧短剑、亚麻布、月布包。逐个交易，看收不收。详细预期在日志 EXPECT。",
        expect = {
            "affinity 5: anything (even 1 gold alone) -> trade cancelled, he says it is too early",
            "affinity 6 and 14: gold / bread / water / linen vest (empty chest) / mooncloth bag -> accepted, vest worn, bag swapped in",
            "affinity 6 and 14: worn shortsword (worse than his) or linen cloth, alone or with good things -> whole trade cancelled, he says why",
            "affinity 15: everything accepted, including shortsword and linen cloth",
            "nothing in the window -> trade goes through as before (no line)",
        },
        bots = {
            { id = "w5", class = "warrior", level = 30, affinity = 5, unequip = { CHEST }, note = "好感 5" },
            { id = "w6", class = "warrior", level = 30, affinity = 6, unequip = { CHEST }, note = "好感 6" },
            { id = "w14", class = "warrior", level = 30, affinity = 14, unequip = { CHEST }, note = "好感 14" },
            { id = "w15", class = "warrior", level = 30, affinity = 15, unequip = { CHEST }, note = "好感 15" },
        },
        leaderItems = {
            { BREAD, 10 }, { WATER, 10 }, { LINEN_VEST, 4 }, { WORN_SHORTSWORD, 4 }, { LINEN_CLOTH, 20 }, { MOONCLOTH_BAG, 4 },
        },
        watchItems = { BREAD, WATER, LINEN_VEST, WORN_SHORTSWORD, LINEN_CLOTH, MOONCLOTH_BAG },
    },
    {
        id = "panels", title = "背包面板和拿钱按钮：21 / 22 / 49 / 50",
        watch = true,
        hint = "四个战士好感 21、22、49、50，各带 5g。逐个观察（Inspect）看背包面板，再点“拿钱”试；也和他们交谈看有没有“让我看看你的包”。",
        expect = {
            "21: bag panel says he does not let you see his bags yet (22, now 21); no slots; talk has no bags option; right-click unequip says not enough affinity",
            "22: slots shown, money line 5g, take-money button grey with 'needs 50' tooltip; talk offers bags",
            "49: same as 22",
            "50: take-money button active; the popup like the guild bank; money moves (CHANGE lines)",
        },
        bots = {
            { id = "w21", class = "warrior", level = 30, affinity = 21, money = 50000, note = "好感 21" },
            { id = "w22", class = "warrior", level = 30, affinity = 22, money = 50000, note = "好感 22" },
            { id = "w49", class = "warrior", level = 30, affinity = 49, money = 50000, note = "好感 49" },
            { id = "w50", class = "warrior", level = 30, affinity = 50, money = 50000, note = "好感 50" },
        },
    },
    {
        id = "services", title = "交谈服务：召唤、传送门、代工",
        watch = true,
        hint = "术士 25、法师 45、裁缝 30、裁缝 29。和每人交谈：法师开门、裁缝 30 用你的亚麻布做布卷，29 的没有代工。召唤：.partybot stay 让大家留下，跑 100 码外等 15 秒。",
        expect = {
            "warlock 25: no summon at your side ('near'); once you are 100 yd away (others stay) he asks or the unit menu summon works, two others click",
            "mage 45: talk offers a portal, a page of cities, he opens it with his rune (rune count -1)",
            "priest 30: talk offers tailoring craft, Bolt of Linen Cloth from your linen (your linen -2 per bolt, bolts to you)",
            "priest 29: talk has no craft option",
        },
        bots = {
            { id = "lock", class = "warlock", level = 40, affinity = 25, learn = { RITUAL_OF_SUMMONING },
              items = { { SOUL_SHARD, 3 } }, note = "好感 25，召唤" },
            { id = "mage", class = "mage", level = 60, affinity = 45, items = { { RUNE_OF_PORTALS, 5 } }, note = "好感 45，传送门" },
            { id = "tailor30", class = "priest", level = 30, affinity = 30, profs = { { TAILORING, 75 } },
              learn = { BOLT_OF_LINEN_CLOTH }, note = "好感 30，裁缝" },
            { id = "tailor29", class = "priest", level = 30, affinity = 29, profs = { { TAILORING, 75 } },
              learn = { BOLT_OF_LINEN_CLOTH }, note = "好感 29，裁缝（对照）" },
        },
        leaderItems = { { LINEN_CLOTH, 20 } },
        watchItems = { LINEN_CLOTH, LINEN_BOLT, SOUL_SHARD, RUNE_OF_PORTALS },
    },
    {
        id = "clear", title = "加分：副本通关",
        watch = true,
        hint = "两个 60 级好感 30。带他们进满级副本（斯坦索姆、通灵、黑上黑下、厄运），选中最后一个首领 .die。首通 +2，重置再杀 +1；团本首通 +5；黑石深渊不加。",
        expect = {
            "a max-level dungeon segment, last boss dies: +2 each (firstclear), the chat says 好感度 +2",
            "the same segment again after a reset: +1 each (clear)",
            "a raid's last boss: +5 first, +3 later; Onyxia +3 / +2",
            "Blackrock Depths' emperor, or a boss that is not the last: nothing",
            "a bot that died in the fight still gets it",
        },
        bots = {
            { id = "war", class = "warrior", level = 60, affinity = 30 },
            { id = "priest", class = "priest", level = 60, affinity = 30 },
        },
    },
}
