-- Supplies at the Stormwind gate: no vendor within the errand range (NpcVisit ANCHOR_YD 35),
-- so a bot that needs something critical has to ask you for a town trip (Life/Errands/TownTrip:
-- you rest and stand still, vendors within TRIP_YD 150 of you), walk to the vendors, buy, and
-- come back (within NEAR_YD 40 it walks back; farther it whispers: come / gate / here).
-- Answer the popup (Requests.lua) or wait 30 s (no answer = yes).
-- Each bot misses something else, so the four together cover the purchase rules
-- (Shared/Supplies priorities, Shared/Vendor::PlanPurchases budget and bag slots).
--
-- Items: 4540 Tough Hunk of Bread, 159 Refreshing Spring Water, 2512 Rough Arrow,
-- 2589 Linen Cloth (fills the bags: white, so it is not sold as junk).
-- Start: the .tele stormwind point inside the gate (not checked in game yet; the Trade District
-- vendors should be 100-150 yd away. Stand where you want and use .scenario life setspot
-- supplies-gate). For the "wait at the gate" answer add a gate once: .partybot gate add.
return {
    id = "supplies-gate", title = "暴风城城门 补给请求", level = 20, faction = "alliance",
    at = { map = 0, x = -8913.23, y = 554.63, z = 93.79, o = 0.64 },
    bots = {
        {
            class = "warrior", role = "tank", money = 2000,
            items = { { 4540, 2 } },
            note = "食物只剩 2 个（必需 5 个），钱够",
        },
        {
            class = "hunter", role = "rangedps", money = 5000,
            items = { { 4540, 20 }, { 2512, 60 } },
            note = "箭只剩 60 支（必需 600），宠物没口粮；要有远程武器才算缺箭",
        },
        {
            class = "mage", role = "rangedps", money = 40,
            note = "没吃没喝，只有 40 铜：只买得起一点",
        },
        {
            class = "priest", role = "healer", money = 5000,
            items = { { 4540, 5 }, { 159, 5 } },
            fill = 2589,
            note = "钱够但包满了，水只剩 5 个（必需 10 个）",
        },
    },
}
