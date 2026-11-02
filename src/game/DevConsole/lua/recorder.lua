-- Combat recorder. rec.watch(leader, label) follows one party; every fight becomes one JSONL file
-- in rec.dir (relative to the mangosd working directory, i.e. repack\server).
--
-- Line types:
--   begin   {type, t, label, file, members=[{guid,name,class,role,level}]}
--   snap    {type, t, party=[info], enemies=[info + threat=[{g,v}]]}   every rec.interval ms
--   ev      {type, t, ev, src, dst, spell, amount, extra, school, crit} from the server hooks
--           ev: dmg (extra = damage type), heal (extra = effective heal), cast, go, cancel
--               (extra = cast time left), fail (cast refused by the spell checks: extra =
--               SpellCastResult code, reason = los / range / facing / ... for common codes),
--               aura+ (amount = duration, extra = stacks),
--               aura- (extra = remove mode), death
--   aggro   {type, t, enemy, from, to, toRole}   an enemy switched to a non-tank party member
--   dec     {type, t, bot, cands=[{src, what, tier, score, b, c, reason, res}]}   one Commander
--           arbiter decision of a bot (u:decisions); res = exec / failed / nottried /
--           lowertier / dropped (benefit <= cost) / veto
--   note    {type, t, text}   a Commander note sent to the leader (u:notes), from 3 s before the fight
--   plan    {type, t, plan}   TeamPlan state (u:plan) when it changed since the last plan line:
--           focus, pull, clear, retreat, patrol{phase, followers, avoid, lures, kill}, interrupts,
--           intercepts; {} = nothing running
--   end     {type, t, duration, result, stats={guid={...}}, names={guid=name}, dropped}
--
-- Timestamps are server ms (now()). Events from the 3 s before the fight are written after begin.

rec = rec or {}
rec.dir = rec.dir or "../logs/combat"
rec.interval = rec.interval or 500
rec.endAfterMs = rec.endAfterMs or 2000

local function ensureDir(dir)
    local win = dir:gsub("/", "\\")
    os.execute('if not exist "' .. win .. '" mkdir "' .. win .. '"')
end

local function write(w, obj)
    if w.file then w.file:write(json(obj), "\n") end
end

local function memberRows(members)
    return map(members, function(m)
        local ai = m:ai()
        return { guid = m:guid(), name = m:name(), class = m:class(), level = m:level(),
                 role = ai and ai.role or "player" }
    end)
end

local function stat(w, guid)
    local s = w.stats[guid]
    if not s then
        s = { dmgDone = 0, dmgTaken = 0, healDone = 0, healEff = 0, healTaken = 0, casts = 0, cancels = 0,
              fails = 0, failsBy = {}, deaths = 0 }
        w.stats[guid] = s
    end
    return s
end

local function account(w, e)
    if e.ev == "dmg" then
        if e.src then stat(w, e.src).dmgDone = stat(w, e.src).dmgDone + (e.amount or 0) end
        if e.dst then stat(w, e.dst).dmgTaken = stat(w, e.dst).dmgTaken + (e.amount or 0) end
    elseif e.ev == "heal" then
        if e.src then
            local s = stat(w, e.src)
            s.healDone = s.healDone + (e.amount or 0)
            s.healEff = s.healEff + (e.extra or 0)
        end
        if e.dst then stat(w, e.dst).healTaken = stat(w, e.dst).healTaken + (e.extra or 0) end
    elseif e.ev == "go" and e.src then
        stat(w, e.src).casts = stat(w, e.src).casts + 1
    elseif e.ev == "cancel" and e.src then
        stat(w, e.src).cancels = stat(w, e.src).cancels + 1
    elseif e.ev == "fail" and e.src then
        local s = stat(w, e.src)
        s.fails = s.fails + 1
        local key = e.reason or tostring(e.extra or 0)
        s.failsBy[key] = (s.failsBy[key] or 0) + 1
    elseif e.ev == "death" and e.dst then
        stat(w, e.dst).deaths = stat(w, e.dst).deaths + 1
    end
end

local function beginFight(w, members)
    ensureDir(rec.dir)
    local name = (w.label or "fight") .. "-" .. os.date("%Y%m%d-%H%M%S") .. ".jsonl"
    local path = rec.dir .. "/" .. name
    local f, err = io.open(path, "w")
    if not f then
        dev.pushError("recorder", err)
        return
    end
    w.file = f
    w.path = path
    w.fightStart = now()
    w.quietSince = nil
    w.stats = {}
    w.names = {}
    w.lastVictim = {}
    w.lastDec = {}
    -- notes from the 3 s before the fight are kept (the patrol / pull judgement comes first)
    w.lastNote = w.fightStart - 3000
    w.lastPlan = nil
    w.aggroLost = 0
    for _, m in ipairs(members) do w.names[m:guid()] = m:name() end
    write(w, { type = "begin", t = w.fightStart, label = w.label, file = name, members = memberRows(members) })
    for _, e in ipairs(w.pre) do
        e.type = "ev"
        write(w, e)
        account(w, e)
    end
    w.pre = {}
    rec.last = path
end

local function endFight(w, members, reason)
    for _, e in ipairs(combatlog.drain()) do
        e.type = "ev"
        write(w, e)
        account(w, e)
    end
    local alive = 0
    for _, m in ipairs(members) do if m:exists() and m:alive() then alive = alive + 1 end end
    local result = (reason == "stopped") and "stopped" or ((alive == 0) and "wipe" or "ok")
    write(w, { type = "end", t = now(), duration = now() - w.fightStart, reason = reason,
               result = result, aliveMembers = alive, aggroLost = w.aggroLost,
               stats = w.stats, names = w.names, dropped = combatlog.dropped() })
    w.file:close()
    w.file = nil
    rec.lastSummary = { file = w.path, duration = now() - w.fightStart, aggroLost = w.aggroLost, result = result }
    local leader = w.leader and unit(w.leader)
    if leader then
        leader:tell(string.format("[录制] 战斗结束 %.1f 秒，记录：%s", (now() - w.fightStart) / 1000, w.path))
    end
end

local function tick(w)
    local leader = unit(w.leader)
    if not leader then
        if w.file then w.file:close() w.file = nil end
        combatlog.enable(false)
        rec.watcher = nil
        return false
    end

    local members = leader:group()
    local enemies = engaged(leader, 80)
    local inCombat = #enemies > 0
    for _, m in ipairs(members) do if m:incombat() then inCombat = true end end

    -- track party and engaged enemies (events where either side is tracked are kept)
    local tracked = {}
    for _, m in ipairs(members) do tracked[#tracked + 1] = m end
    for _, e in ipairs(enemies) do tracked[#tracked + 1] = e end
    combatlog.track(tracked)

    if not w.file then
        -- keep the last 3 s of events so the pull is in the file
        local cutoff = now() - 3000
        for _, e in ipairs(combatlog.drain()) do w.pre[#w.pre + 1] = e end
        local keep = {}
        for _, e in ipairs(w.pre) do if e.t >= cutoff then keep[#keep + 1] = e end end
        w.pre = keep
        if inCombat then beginFight(w, members) end
        if not w.file then return end
    end

    -- snapshot
    local roleOf = {}
    local party = {}
    for _, m in ipairs(members) do
        local i = m:info()
        party[#party + 1] = i
        roleOf[i.guid] = i.role or "player"
        w.names[i.guid] = i.name
    end
    local enemyRows = {}
    for _, e in ipairs(enemies) do
        local i = e:info()
        i.threat = map(e:threat(), function(r) return { g = r.unit:guid(), v = r.threat } end)
        enemyRows[#enemyRows + 1] = i
        w.names[i.guid] = i.name
        local prev = w.lastVictim[i.guid]
        if i.victim and prev ~= i.victim then
            local role = roleOf[i.victim]
            if prev and role and role ~= "tank" then
                w.aggroLost = w.aggroLost + 1
                write(w, { type = "aggro", t = now(), enemy = i.guid, from = prev, to = i.victim, toRole = role })
            end
            w.lastVictim[i.guid] = i.victim
        end
    end
    write(w, { type = "snap", t = now(), party = party, enemies = enemyRows })

    -- Commander decisions of the bots since the last tick (u:decisions keeps the last 20)
    for _, m in ipairs(members) do
        local log = m:decisions(w.lastDec[m:guid()] or 0)
        if log then
            for _, d in ipairs(log) do
                write(w, { type = "dec", t = d.t, bot = m:guid(), cands = d.cands })
                w.lastDec[m:guid()] = d.t
            end
        end
    end

    -- Commander notes to the leader since the last tick, and the plan state when it changed
    -- (u:notes / u:plan are missing on a mangosd built before 2026-11-02: skipped)
    for _, n in ipairs(leader.notes and leader:notes(w.lastNote) or {}) do
        write(w, { type = "note", t = n.t, text = n.text })
        w.lastNote = n.t
    end
    local plan = leader.plan and leader:plan()
    if plan then
        local text = json(plan)
        if text ~= w.lastPlan then
            write(w, { type = "plan", t = now(), plan = plan })
            w.lastPlan = text
        end
    end

    for _, e in ipairs(combatlog.drain()) do
        e.type = "ev"
        write(w, e)
        account(w, e)
    end
    w.file:flush()

    if inCombat then
        w.quietSince = nil
    else
        w.quietSince = w.quietSince or now()
        if now() - w.quietSince >= rec.endAfterMs then endFight(w, members, "combat ended") end
    end
end

-- Start following a party. label goes into the file name (scenario id).
function rec.watch(leader, label)
    rec.stop()
    local w = { leader = leader:guid(), label = label or "fight", pre = {} }
    rec.watcher = w
    combatlog.enable(true)
    w.timer = every(rec.interval, function()
        local ok, err = pcall(tick, w)
        if not ok then
            dev.pushError("recorder", err)
            if w.file then w.file:close() w.file = nil end
            combatlog.enable(false)
            rec.watcher = nil
            return false
        end
        if rec.watcher ~= w then return false end
    end)
    return w
end

function rec.stop()
    local w = rec.watcher
    if not w then return end
    cancel(w.timer)
    if w.file then
        local leader = unit(w.leader)
        endFight(w, leader and leader:group() or {}, "stopped")
    end
    combatlog.enable(false)
    rec.watcher = nil
end

function rec.status()
    local w = rec.watcher
    if not w then return { watching = false, last = rec.last, lastSummary = rec.lastSummary } end
    local leader = unit(w.leader)
    return { watching = true, leader = leader and leader:name(), label = w.label, recording = w.file ~= nil,
             file = w.path, last = rec.last, lastSummary = rec.lastSummary }
end
