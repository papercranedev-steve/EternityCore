-- Combat review: anomaly detectors run by the recorder (recorder.lua) on every tick of a recorded
-- fight. A detector that fires writes two lines to the fight's file:
--   anomaly {type, t, id, kind, sev, who, by, whoG, byG, cause, facts, links}
--           kind: aggro / death / healgap / stuck / idle / add / ccbreak / ccmove / mark, and at the
--           fight's end estimate (the pull's fight estimate far from what happened, review.finish)
--           (review.labels);
--           sev: high / mid / low; who = the one it happened to, by = the one who caused it
--           (names; guids in whoG / byG); cause = one line why; facts = numbers behind the cause;
--           links = ids of earlier anomalies that led to this one (death <- aggro, healgap)
--   detail  {type, t, id, party=[...], enemies=[...]}   live state at that moment that the
--           snap lines don't have: casts with targets, control, AI last decision, healer -> member
--           distance / line of sight, tank -> enemy distance, named threat tables
-- and says one line ("[复盘] #2 13.8s 仇恨丢失 ...") in party chat as the bot it happened to
-- (bot speech on, Commander/Commands/BotSpeech.h), else tells it the leader. dev.ps1 review and the addon's
-- fight log window (recview.lua) read them.
--
-- Adding a detector: append { kind = "...", run = function(rs, ctx) ... emit(rs, ctx, kind, a) end }
-- to review.detectors. ctx (built by the recorder each tick): leader, members (units), party
-- (u:info() rows, same order), enemies, enemyRows (info + threat), evs (events drained this
-- tick), decs ({bot, t, cands} this tick), pull (u:pull() or nil), now. rs = per-fight state.

review = review or {}

review.labels = {
    aggro = "仇恨丢失", death = "死亡", healgap = "治疗空窗", stuck = "施法卡住", idle = "发呆",
    add = "意外加怪", ccbreak = "控制被打破", ccmove = "换锁", mark = "玩家标记",
    estimate = "估算偏差",
}

review.windowMs = 10000        -- events and decisions kept for the cause analysis
review.tell = true             -- tell the leader each anomaly

-- thresholds
local DEATH_LOOKBACK_MS = 6000
local LOW_HP_PCT, LOW_HP_CLEAR_PCT, LOW_HP_MS = 35, 50, 2000
local STUCK_COUNT, STUCK_WINDOW_MS, STUCK_COOLDOWN_MS = 3, 3000, 8000
local IDLE_MS, IDLE_COOLDOWN_MS = 4000, 10000
local AGGRO_REPEAT_MS = 5000
local HEAL_RANGE = 40
local TANK_MELEE_DIST = 8

-- Spell mechanics that hold a creature out of the fight and break on damage (SpellMgr Mechanics):
-- charm, disoriented, fear, sleep, freeze, knockout, polymorph, banish, shackle, horror, sapped
local CC_MECHANICS = { [1] = true, [2] = true, [5] = true, [10] = true, [13] = true, [14] = true,
                       [17] = true, [18] = true, [20] = true, [24] = true, [30] = true }
local TAUNT_NAMES = { "Taunt", "Mocking Blow", "Growl", "Challenging Shout", "Challenging Roar" }

---------------------------------------------------------------------------
-- helpers
---------------------------------------------------------------------------

local function nameOf(rs, g)
    if not g then return nil end
    return rs.w.names[g] or g
end

function review.spellName(w, id)
    if not id or id == 0 then return nil end
    local n = w.spellNames[id]
    if n == nil then
        local s = spell(id)
        n = s and s.name or tostring(id)
        w.spellNames[id] = n
    end
    return n
end

local function rel(rs, t) return (t - rs.w.fightStart) / 1000 end

local function pct(a, b) if not b or b == 0 then return 0 end return math.floor(a * 100 / b + 0.5) end

local function manaPct(i) if i.ptype ~= 0 or not i.maxpower or i.maxpower == 0 then return nil end return pct(i.power, i.maxpower) end

local function castText(rs, u)
    local c = u:casting()
    if not c then return nil end
    local s = c.name
    if c.target then s = s .. "→" .. c.target:name() end
    return s, c
end

local function isControlSpell(rs, id)
    local v = rs.ccSpell[id]
    if v == nil then
        v = false
        local s = spell(id)
        if s then
            if CC_MECHANICS[s.mechanic] then v = true end
            for _, e in ipairs(s.effects or {}) do if CC_MECHANICS[e.mechanic] then v = true end end
        end
        rs.ccSpell[id] = v
    end
    return v
end

-- the party's first tank, and its info row
local function mainTank(ctx)
    for k, i in ipairs(ctx.party) do
        if i.role == "tank" then return ctx.members[k], i end
    end
end

local function tauntSpells(rs, tank)
    local g = tank:guid()
    if rs.taunts[g] == nil then
        local list, seen = {}, {}
        for _, name in ipairs(TAUNT_NAMES) do
            for _, s in ipairs(tank:spells(name)) do
                if s.name == name and not seen[name] then list[#list + 1] = s seen[name] = true end
            end
        end
        rs.taunts[g] = list
    end
    return rs.taunts[g]
end

-- "Taunt 冷却 6.2s, Mocking Blow 可用"
local function tauntState(rs, tank)
    local parts = {}
    for _, s in ipairs(tauntSpells(rs, tank)) do
        local left = tank:cooldown(s.id)
        if left == 0 then parts[#parts + 1] = s.name .. " 可用"
        elseif left < 0 then parts[#parts + 1] = s.name .. " 冷却中"
        else parts[#parts + 1] = string.format("%s 冷却 %.1fs", s.name, left / 1000) end
    end
    if #parts == 0 then return "没有嘲讽技能" end
    return table.concat(parts, "，")
end

-- recent decision candidates of a bot whose text matches one of the patterns: "taunt X: failed"
local function decisionsMatching(rs, botG, sinceT, patterns)
    local out = {}
    for _, d in ipairs(rs.decs) do
        if d.bot == botG and d.t >= sinceT then
            for _, c in ipairs(d.cands or {}) do
                local text = (c.src or "") .. " " .. (c.what or "")
                for _, p in ipairs(patterns) do
                    if text:find(p, 1, true) then
                        out[#out + 1] = string.format("%.1fs %s: %s%s", rel(rs, d.t), text, c.res or "?",
                            c.reason and (" (" .. c.reason .. ")") or "")
                        break
                    end
                end
            end
        end
    end
    -- the last few only
    while #out > 4 do table.remove(out, 1) end
    return out
end

local function lastDecisionText(u)
    local ai = u:ai()
    if not ai or not ai.lastDecision then return nil end
    return string.format("%s (%.1fs 前)", ai.lastDecision, (ai.lastDecisionAgoMs or 0) / 1000)
end

-- What each live healer was doing for `target` (a unit). Rows + one line per healer.
local function healerFacts(rs, ctx, target)
    local rows, lines = {}, {}
    for k, i in ipairs(ctx.party) do
        local u = ctx.members[k]
        if i.role == "healer" and i.alive and u ~= nil and u:exists() then
            local dist = u:dist(target)
            local los = u:los(target)
            local cast, c = castText(rs, u)
            local row = { name = i.name, mana = manaPct(i), dist = dist, los = los, cast = cast, cc = i.cc,
                          last = lastDecisionText(u) }
            rows[#rows + 1] = row
            local why
            if u:guid() == target:guid() then why = "治疗自己就是目标"
            elseif i.cc then why = "被控（" .. i.cc .. "）"
            elseif row.mana and row.mana < 10 then why = "没蓝（" .. row.mana .. "%）"
            elseif dist and dist > HEAL_RANGE then why = string.format("距离 %.0f 码", dist)
            elseif not los then why = "没有视线"
            elseif c and c.target and c.target:guid() == target:guid() then why = "正在读条治他（" .. c.name .. "）"
            elseif c then why = "在读 " .. cast
            else why = "没在治疗，最后决策：" .. (row.last or "无") end
            lines[#lines + 1] = i.name .. " " .. why
        end
    end
    if #lines == 0 then lines[1] = "没有活着的治疗" end
    return rows, table.concat(lines, "；")
end

local function linksTo(rs, kinds, whoG, sinceT)
    local out = {}
    for _, a in ipairs(rs.list) do
        if a.t >= sinceT and a.whoG == whoG and kinds[a.kind] then out[#out + 1] = a.id end
    end
    return out
end

---------------------------------------------------------------------------
-- live snapshot (detail line)
---------------------------------------------------------------------------

local function capture(rs, leader)
    local members = leader:group()
    local enemies = engaged(leader, 80)
    local party, foes = {}, {}
    local tank
    for _, m in ipairs(members) do
        local i = m:info()
        local row = { name = i.name, role = i.role or "player", hp = pct(i.hp, i.maxhp), mana = manaPct(i),
                      x = i.x, y = i.y, z = i.z, alive = i.alive, cc = i.cc, moving = m:moving(),
                      cast = castText(rs, m), victim = nameOf(rs, i.victim) }
        local ai = m:ai()
        if ai then
            row.last = lastDecisionText(m)
            if ai.movementHoldMs and ai.movementHoldMs > 0 then row.holdMs = ai.movementHoldMs end
        end
        if i.role == "healer" then
            row.reach = {}
            for _, o in ipairs(members) do
                if o:guid() ~= m:guid() then
                    row.reach[#row.reach + 1] = string.format("%s %.0fy%s", o:name(), m:dist(o) or -1,
                        m:los(o) and "" or " 无视线")
                end
            end
        end
        if i.role == "tank" and not tank then tank = m end
        party[#party + 1] = row
    end
    for _, e in ipairs(enemies) do
        local i = e:info()
        local row = { name = i.name, hp = pct(i.hp, i.maxhp), victim = nameOf(rs, i.victim), cc = i.cc,
                      cast = castText(rs, e), x = i.x, y = i.y, z = i.z }
        if tank then row.tankDist = tank:dist(e) end
        row.threat = map(e:threat(), function(r)
            return string.format("%s %.0f (%.0f%%)", r.unit:name(), r.threat, r.pct)
        end)
        while #row.threat > 4 do table.remove(row.threat) end
        foes[#foes + 1] = row
    end
    return party, foes
end

-- The bot that says an anomaly: the one it happened to, else the main tank bot, else any bot.
local function speakerFor(leader, whoG)
    local who = whoG and unit(whoG)
    if who and who:isbot() then return who end
    local any
    for _, m in ipairs(leader:group()) do
        if m:isbot() then
            local ai = m:ai()
            if ai and ai.role == "tank" then return m end
            any = any or m
        end
    end
    return any
end

-- Writes the anomaly + detail lines and tells the leader. a = {who=unit|guid, by=..., cause,
-- facts, links, sev}. Returns the id.
local function emit(rs, kind, a)
    local w = rs.w
    rs.nextId = rs.nextId + 1
    local id = rs.nextId
    local t = now()
    local whoG = a.whoG
    local byG = a.byG
    local line = { type = "anomaly", t = t, id = id, kind = kind, sev = a.sev or "mid",
                   who = nameOf(rs, whoG), by = nameOf(rs, byG), whoG = whoG, byG = byG,
                   cause = a.cause, facts = a.facts, links = a.links }
    rec.writeLine(w, line)
    rs.list[#rs.list + 1] = { id = id, t = t, kind = kind, whoG = whoG, byG = byG }
    rs.counts[kind] = (rs.counts[kind] or 0) + 1

    local leader = unit(w.leader)
    if leader then
        local party, foes = capture(rs, leader)
        rec.writeLine(w, { type = "detail", t = t, id = id, party = party, enemies = foes })
        if review.tell then
            local text = string.format("[复盘] #%d %.1fs %s %s：%s", id, rel(rs, t),
                review.labels[kind] or kind, line.who or "", a.cause or "")
            if #a.links > 0 then
                text = text .. "（← #" .. table.concat(map(a.links, tostring), " #") .. "）"
            end
            -- said in party chat by the bot it happened to (else the tank bot) when bot speech
            -- is on (`.partybot ai say`), else a system line to the leader
            local speaker = speakerFor(leader, whoG)
            if not (speaker and speaker.partysay and speaker:partysay(text)) then
                leader:tell("|cffff8800" .. text:gsub("^%[复盘%]", "[复盘]|r"))
            end
        end
    end
    return id
end

---------------------------------------------------------------------------
-- detectors
---------------------------------------------------------------------------

review.detectors = {}

-- An enemy turns to a party member who is not a tank (also its first target).
review.detectors[#review.detectors + 1] = { kind = "aggro", run = function(rs, ctx)
    local roleOf, unitOf = {}, {}
    for k, i in ipairs(ctx.party) do roleOf[i.guid] = i.role or "player" unitOf[i.guid] = ctx.members[k] end
    local tank, ti = mainTank(ctx)
    for k, e in ipairs(ctx.enemyRows) do
        local victim = e.victim
        local prev = rs.lastVictim[e.guid]
        local confused = e.cc and (e.cc:find("flee") or e.cc:find("confused"))
        if victim and victim ~= prev and not confused then
            rs.lastVictim[e.guid] = victim
            local role = roleOf[victim]
            local key = e.guid .. ">" .. victim
            if role and role ~= "tank" and (rs.aggroAt[key] or -1e9) + AGGRO_REPEAT_MS < ctx.now then
                rs.aggroAt[key] = ctx.now
                local threatOf = {}
                for _, r in ipairs(e.threat or {}) do threatOf[r.g] = r.v end
                local top = {}
                for n, r in ipairs(e.threat or {}) do
                    if n > 4 then break end
                    top[#top + 1] = string.format("%s %.0f", nameOf(rs, r.g), r.v)
                end
                local enemyUnit = ctx.enemies[k]
                local facts = { enemy = e.name, from = nameOf(rs, prev), toRole = role, threat = top }
                local cause
                local vt = threatOf[victim] or 0
                if not tank then
                    cause = "队里没有坦克"
                else
                    local tt = threatOf[ti.guid]
                    local dist = tank:dist(enemyUnit)
                    facts.tankDist = dist
                    facts.tankThreat = tt
                    facts.taunt = tauntState(rs, tank)
                    facts.tankDecisions = decisionsMatching(rs, ti.guid, ctx.now - 3000,
                        { "aunt", "Mocking", "Growl", "Challenging", e.name })
                    facts.tankCast = castText(rs, tank)
                    if not ti.alive then cause = "坦克已死"
                    elseif ti.cc then cause = "坦克被控（" .. ti.cc .. "）"
                    elseif not tt or tt == 0 then cause = "坦克没碰过这只怪"
                    elseif dist and dist > TANK_MELEE_DIST then cause = string.format("坦克离它 %.0f 码", dist)
                    elseif vt > tt then cause = string.format("%s 威胁 %.0f 超过坦克 %.0f（%d%%）",
                        nameOf(rs, victim), vt, tt, pct(vt, tt))
                    else cause = "威胁没超过坦克却换了目标（嘲讽/恐惧/脚本？）" end
                    if role == "healer" then cause = cause .. "，治疗仇恨" end
                    cause = cause .. "；" .. facts.taunt
                end
                emit(rs, "aggro", { whoG = victim, byG = victim, sev = (role == "healer" or role == "rangedps") and "high" or "mid",
                                    cause = e.name .. " → " .. nameOf(rs, victim) .. "：" .. cause, facts = facts,
                                    links = linksTo(rs, { add = true }, e.guid, ctx.now - 10000) })
            end
        elseif victim then
            rs.lastVictim[e.guid] = victim
        end
    end
end }

-- A party member died: what hit them in the last seconds, what the healers were doing.
review.detectors[#review.detectors + 1] = { kind = "death", run = function(rs, ctx)
    for _, ev in ipairs(ctx.evs) do
        if ev.ev == "death" and ev.dst and rs.isMember[ev.dst] then
            local from = ev.t - DEATH_LOOKBACK_MS
            local taken, healed, bySrc, last2 = 0, 0, {}, 0
            for _, e in ipairs(rs.recent) do
                if e.dst == ev.dst and e.t >= from and e.t <= ev.t then
                    if e.ev == "dmg" then
                        taken = taken + (e.amount or 0)
                        local k = e.src or "?"
                        bySrc[k] = (bySrc[k] or 0) + (e.amount or 0)
                        if e.t >= ev.t - 2000 then last2 = last2 + (e.amount or 0) end
                    elseif e.ev == "heal" then
                        healed = healed + (e.extra or 0)
                    end
                end
            end
            local srcs = {}
            for g, v in pairs(bySrc) do srcs[#srcs + 1] = { g = g, v = v } end
            table.sort(srcs, function(a, b) return a.v > b.v end)
            local top = {}
            for n, s in ipairs(srcs) do if n > 3 then break end top[#top + 1] = string.format("%s %d", nameOf(rs, s.g), s.v) end
            local victim = unit(ev.dst)
            local hrows, hline = {}, "（单位已不存在）"
            if victim then hrows, hline = healerFacts(rs, ctx, victim) end
            local maxhp = rs.maxhp[ev.dst] or 0
            local cause = string.format("%.0f 秒受伤 %d（%s），受治疗 %d", DEATH_LOOKBACK_MS / 1000, taken,
                table.concat(top, "，"), healed)
            if maxhp > 0 and last2 > maxhp * 0.6 then
                cause = cause .. string.format("；2 秒内 %d（%d%% 血量）", last2, pct(last2, maxhp))
            end
            cause = cause .. "；" .. hline
            emit(rs, "death", { whoG = ev.dst, byG = srcs[1] and srcs[1].g, sev = "high", cause = cause,
                                facts = { taken = taken, healed = healed, last2s = last2, sources = top, healers = hrows },
                                links = linksTo(rs, { aggro = true, healgap = true, ccbreak = true }, ev.dst, ev.t - 15000) })
        end
    end
end }

-- A member below LOW_HP_PCT for LOW_HP_MS with no healer casting on them.
review.detectors[#review.detectors + 1] = { kind = "healgap", run = function(rs, ctx)
    for k, i in ipairs(ctx.party) do
        local u = ctx.members[k]
        local hp = pct(i.hp, i.maxhp)
        if not i.alive or hp >= LOW_HP_CLEAR_PCT then
            rs.lowSince[i.guid] = nil
            rs.lowReported[i.guid] = nil
        elseif hp < LOW_HP_PCT then
            rs.lowSince[i.guid] = rs.lowSince[i.guid] or ctx.now
            if not rs.lowReported[i.guid] and ctx.now - rs.lowSince[i.guid] >= LOW_HP_MS then
                local healing = false
                for kk, h in ipairs(ctx.party) do
                    if h.role == "healer" then
                        local c = ctx.members[kk]:casting()
                        if c and c.target and c.target:guid() == i.guid then healing = true end
                    end
                end
                if not healing then
                    rs.lowReported[i.guid] = true
                    local hrows, hline = healerFacts(rs, ctx, u)
                    emit(rs, "healgap", { whoG = i.guid, sev = "high",
                        cause = string.format("%d%% 血 %.1f 秒没人治：%s", hp, (ctx.now - rs.lowSince[i.guid]) / 1000, hline),
                        facts = { hp = hp, lowMs = ctx.now - rs.lowSince[i.guid], healers = hrows },
                        links = linksTo(rs, { aggro = true }, i.guid, ctx.now - 10000) })
                end
            end
        end
    end
end }

-- The same bot refused STUCK_COUNT casts for the same reason within STUCK_WINDOW_MS.
review.detectors[#review.detectors + 1] = { kind = "stuck", run = function(rs, ctx)
    for _, e in ipairs(ctx.evs) do
        if e.ev == "fail" and e.src and rs.isMember[e.src] then
            local key = e.src .. "/" .. (e.reason or tostring(e.extra))
            local list = rs.fails[key] or {}
            list[#list + 1] = e
            local keep = {}
            for _, f in ipairs(list) do if f.t >= e.t - STUCK_WINDOW_MS then keep[#keep + 1] = f end end
            rs.fails[key] = keep
            if #keep >= STUCK_COUNT and (rs.stuckUntil[key] or 0) <= e.t then
                rs.stuckUntil[key] = e.t + STUCK_COOLDOWN_MS
                local me, target = unit(e.src), e.dst and unit(e.dst)
                local facts = { reason = e.reason or tostring(e.extra), count = #keep,
                                spells = map(keep, function(f) return review.spellName(rs.w, f.spell) end),
                                target = nameOf(rs, e.dst) }
                if me and target then
                    facts.dist = me:dist(target)
                    facts.los = me:los(target)
                end
                if me then facts.moving = me:moving() facts.last = lastDecisionText(me) end
                emit(rs, "stuck", { whoG = e.src, sev = "low",
                    cause = string.format("%.0f 秒内 %d 次 %s 失败：%s → %s%s", STUCK_WINDOW_MS / 1000, #keep,
                        facts.reason, review.spellName(rs.w, e.spell) or "?", facts.target or "?",
                        facts.dist and string.format("（%.0f 码%s）", facts.dist, facts.los and "" or "，无视线") or ""),
                    facts = facts, links = {} })
            end
        end
    end
end }

-- A bot in the fight that did nothing (no cast, swing, heal) for IDLE_MS while standing still.
review.detectors[#review.detectors + 1] = { kind = "idle", run = function(rs, ctx)
    if #ctx.enemies == 0 or ctx.now - rs.w.fightStart < IDLE_MS then return end
    local allControlled, lowest = true, 100
    for _, e in ipairs(ctx.enemyRows) do if not e.cc then allControlled = false end end
    for _, i in ipairs(ctx.party) do if i.alive then lowest = math.min(lowest, pct(i.hp, i.maxhp)) end end
    if allControlled then return end
    for k, i in ipairs(ctx.party) do
        local u = ctx.members[k]
        local last = rs.lastAct[i.guid] or rs.w.fightStart
        if i.role and i.role ~= "player" and i.alive and not i.cc and not i.cast and ctx.now - last >= IDLE_MS
            and not (i.role == "healer" and lowest >= 80) and not u:moving()
            and (rs.idleUntil[i.guid] or 0) <= ctx.now then
            rs.idleUntil[i.guid] = ctx.now + IDLE_COOLDOWN_MS
            local ai = u:ai() or {}
            local victim = i.victim and unit(i.victim)
            local facts = { last = lastDecisionText(u), holdMs = ai.movementHoldMs, victim = nameOf(rs, i.victim),
                            victimDist = victim and u:dist(victim) or nil,
                            decisions = decisionsMatching(rs, i.guid, ctx.now - 3000, { "" }) }
            emit(rs, "idle", { whoG = i.guid, sev = "low",
                cause = string.format("%.1f 秒没出手，目标 %s%s，最后决策：%s", (ctx.now - last) / 1000,
                    facts.victim or "无", facts.victimDist and string.format("（%.0f 码）", facts.victimDist) or "",
                    facts.last or "无"),
                facts = facts, links = {} })
        end
    end
end }

-- An enemy joined that the pull did not expect (or, without a pull, not in the first tick).
review.detectors[#review.detectors + 1] = { kind = "add", run = function(rs, ctx)
    local pull = ctx.pull
    if pull and (pull.planned or pull.t or 0) >= rs.w.fightStart - 60000 then
        for _, x in ipairs(pull.expected or {}) do rs.known[x.g] = true end
        if pull.target then rs.known[pull.target] = true end
        rs.havePull = true
    end
    local first = not rs.seenTick
    rs.seenTick = true
    for k, e in ipairs(ctx.enemyRows) do
        if not rs.known[e.guid] then
            rs.known[e.guid] = true
            if not (first and not rs.havePull) then
                local u = ctx.enemies[k]
                local near, nearD
                for _, m in ipairs(ctx.members) do
                    local d = m:dist(u)
                    if d and (not nearD or d < nearD) then near, nearD = m, d end
                end
                local firstThreat = e.threat and e.threat[1] and e.threat[1].g
                local facts = { enemy = e.name, level = e.level, nearest = near and near:name(), nearestDist = nearD,
                                victim = nameOf(rs, e.victim), topThreat = nameOf(rs, firstThreat),
                                expected = rs.havePull and #(pull and pull.expected or {}) or nil,
                                patrolPlan = ctx.plan and ctx.plan.patrol and ctx.plan.patrol.phase or nil }
                emit(rs, "add", { whoG = e.guid, byG = firstThreat, sev = "mid",
                    cause = string.format("%s 不在%s，离 %s %.0f 码，打向 %s%s", e.name,
                        rs.havePull and "开怪预计里" or "开战时的怪里", facts.nearest or "?", nearD or -1,
                        facts.victim or "?", facts.patrolPlan and ("，巡逻计划 " .. facts.patrolPlan) or ""),
                    facts = facts, links = {} })
            end
        end
    end
end }

-- A party member's control on an enemy ended early by damage (not when only controlled enemies
-- are left: then the main tank wakes one on purpose, ControlledEnd).
review.detectors[#review.detectors + 1] = { kind = "ccbreak", run = function(rs, ctx)
    for _, e in ipairs(ctx.evs) do
        if e.ev == "aura+" and e.src and rs.isMember[e.src] and e.dst and not rs.isMember[e.dst]
            and isControlSpell(rs, e.spell) then
            rs.cc[e.dst .. "/" .. e.spell] = { t = e.t, dur = e.amount or 0, src = e.src }
        elseif e.ev == "aura-" and e.dst and e.spell then
            local key = e.dst .. "/" .. e.spell
            local c = rs.cc[key]
            if c then
                rs.cc[key] = nil
                local early = c.dur > 0 and e.t - c.t < c.dur - 1000
                -- One target per caster (Shackle Undead, Polymorph, ...): the same caster putting
                -- the same spell on another enemy frees this one in the same server tick.
                local moved
                if early and (e.extra or 0) == 0 then
                    for _, n in ipairs(ctx.evs) do
                        if n.ev == "aura+" and n.src == c.src and n.dst ~= e.dst and n.spell
                            and math.abs(n.t - e.t) <= 100
                            and review.spellName(rs.w, n.spell) == review.spellName(rs.w, e.spell) then
                            moved = n
                            break
                        end
                    end
                end
                if moved then
                    emit(rs, "ccmove", { whoG = e.dst, byG = c.src, sev = "low",
                        cause = string.format("%s 把 %s 从 %s 换到 %s（锁了 %.1f/%.0f 秒）", nameOf(rs, c.src) or "?",
                            review.spellName(rs.w, e.spell), nameOf(rs, e.dst) or "?", nameOf(rs, moved.dst) or "?",
                            (e.t - c.t) / 1000, c.dur / 1000),
                        facts = { caster = nameOf(rs, c.src), to = nameOf(rs, moved.dst), heldMs = e.t - c.t,
                                  durationMs = c.dur },
                        links = {} })
                elseif early and (e.extra or 0) == 0 then
                    local otherLoose = false
                    for _, row in ipairs(ctx.enemyRows) do
                        if row.guid ~= e.dst and row.alive and not row.cc then otherLoose = true end
                    end
                    if otherLoose then
                        local breaker, hit
                        for n = #rs.recent, 1, -1 do
                            local d = rs.recent[n]
                            if d.t < e.t - 500 then break end
                            if d.ev == "dmg" and d.dst == e.dst and d.t <= e.t + 50 then breaker, hit = d.src, d break end
                        end
                        emit(rs, "ccbreak", { whoG = e.dst, byG = breaker, sev = "mid",
                            cause = string.format("%s 的 %s 在 %.1f/%.0f 秒时被 %s 打破%s", nameOf(rs, e.dst),
                                review.spellName(rs.w, e.spell), (e.t - c.t) / 1000, c.dur / 1000,
                                nameOf(rs, breaker) or "?（没找到伤害）",
                                hit and hit.spell and ("（" .. review.spellName(rs.w, hit.spell) .. "）") or ""),
                            facts = { caster = nameOf(rs, c.src), heldMs = e.t - c.t, durationMs = c.dur,
                                      breakerSpell = hit and review.spellName(rs.w, hit.spell) },
                            links = {} })
                    end
                end
            end
        end
    end
end }

---------------------------------------------------------------------------
-- recorder interface
---------------------------------------------------------------------------

-- new per-fight state (beginFight)
function review.begin(w, members)
    local rs = { w = w, nextId = 0, list = {}, counts = {}, recent = {}, decs = {}, isMember = {}, maxhp = {},
                 lastVictim = {}, aggroAt = {}, lowSince = {}, lowReported = {}, fails = {}, stuckUntil = {},
                 lastAct = {}, idleUntil = {}, known = {}, cc = {}, ccSpell = {}, taunts = {},
                 healerMana = {}, dealt = 0 }
    for _, m in ipairs(members) do rs.isMember[m:guid()] = true end
    w.review = rs
    return rs
end

-- one recorder tick (after the snap line is written)
function review.observe(w, ctx)
    local rs = w.review
    if not rs then return end
    for k, i in ipairs(ctx.party) do
        rs.isMember[i.guid] = true
        rs.maxhp[i.guid] = i.maxhp
    end
    for _, e in ipairs(ctx.evs) do
        rs.recent[#rs.recent + 1] = e
        if e.src and (e.ev == "go" or e.ev == "cast" or e.ev == "dmg" or e.ev == "heal") then rs.lastAct[e.src] = e.t end
        if e.ev == "dmg" and e.src and rs.isMember[e.src] and e.dst and not rs.isMember[e.dst] then
            rs.dealt = rs.dealt + (e.amount or 0)
        end
    end
    -- For the "estimate" check at the end (review.finish): the pull's fight estimate, the healer
    -- bots' mana at the start and now.
    if ctx.pull and ctx.pull.estimate and not rs.estimate then rs.estimate = ctx.pull.estimate end
    for _, i in ipairs(ctx.party) do
        if i.role == "healer" and i.ptype == 0 then
            local h = rs.healerMana[i.guid]
            if not h then
                h = { start = i.power, max = i.maxpower }
                rs.healerMana[i.guid] = h
            end
            h.last = i.power
        end
    end
    for _, d in ipairs(ctx.decs) do rs.decs[#rs.decs + 1] = d end
    local cutoff = ctx.now - review.windowMs
    local function trim(list)
        local out = {}
        for _, x in ipairs(list) do if x.t >= cutoff then out[#out + 1] = x end end
        return out
    end
    rs.recent = trim(rs.recent)
    rs.decs = trim(rs.decs)

    for _, d in ipairs(review.detectors) do
        local ok, err = pcall(d.run, rs, ctx)
        if not ok then dev.pushError("review." .. d.kind, err) end
    end
end

-- Fight end (recorder endFight, before the end line): the pull's fight estimate against what
-- happened. An estimate that is far off decided the pull wrongly (patrol taken along, healer
-- mana gate), so it is an anomaly of its own (strat-20261108-141921: 38 s planned, 128 s fought).
local ESTIMATE_SEC_RATIO = 1.5
local ESTIMATE_MANA_POOL_SHARE = 0.25

function review.finish(w)
    local rs = w.review
    local est = rs and rs.estimate
    if not est or not est.sec or est.sec <= 0 then return end
    local sec = (now() - w.fightStart) / 1000
    if sec <= 0 then return end
    local used, pool = 0, 0
    for _, h in pairs(rs.healerMana) do
        used = used + math.max(0, h.start - (h.last or h.start))
        pool = pool + (h.max or 0)
    end
    local expectedUsed = math.max(0, (est.manaStart or 0) - (est.manaLeft or 0))
    local ratio = sec / est.sec
    local secOff = ratio > ESTIMATE_SEC_RATIO or ratio < 1 / ESTIMATE_SEC_RATIO
    local manaOff = pool > 0 and math.abs(used - expectedUsed) > ESTIMATE_MANA_POOL_SHARE * pool
    if not secOff and not manaOff then return end
    local dps = rs.dealt / sec
    local cause = string.format("开怪估算 %.0f 秒，实际 %.0f 秒（%.1f 倍）；队伍伤害估 %.0f/s（%s），实际 %.0f/s",
        est.sec, sec, ratio, est.partyDps or 0, est.dpsSource or "?", dps)
    if pool > 0 then
        cause = cause .. string.format("；治疗蓝估用 %.0f，实际用 %.0f", expectedUsed, used)
    end
    emit(rs, "estimate", { sev = "mid", cause = cause, links = {},
        facts = { estSec = est.sec, sec = sec, estPartyDps = est.partyDps, dpsSource = est.dpsSource, partyDps = dps,
                  estManaUsed = expectedUsed, manaUsed = used, healerPool = pool,
                  estTankLowPct = est.tankLowPct, estIncomingDps = est.incomingDps } })
end

-- A player mark (".scenario mark [text]"): an anomaly with the live state, nothing detected.
function review.mark(w, who, text)
    local rs = w.review
    if not rs then return nil end
    return emit(rs, "mark", { whoG = who and who:guid(), sev = "high", cause = (text ~= "" and text) or "（玩家标记）",
                              facts = {}, links = {} })
end
