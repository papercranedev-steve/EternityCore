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
--   anomaly / detail   an anomaly the review detectors found and the live state then (review.lua)
--   dec     {type, t, bot, cands=[{src, what, tier, score, b, c, reason, res}]}   one Commander
--           arbiter decision of a bot (u:decisions); res = exec / failed / nottried /
--           lowertier / dropped (benefit <= cost) / veto / order (heal order)
--   gap     {type, t, bot, what="dec"}   decisions of the bot were lost since the last tick
--   note    {type, t, text}   a Commander note sent to the leader (u:notes), from 3 s before the fight
--   plan    {type, t, plan}   TeamPlan state (u:plan) when it changed since the last plan line:
--           focus, pull, clear, retreat, patrol{phase, followers, avoid, lures, kill}, interrupts,
--           intercepts, controls[{enemy, bot, spell, area}]; {} = nothing running
--   offers  {type, t, offers=[{kind, target, points, now, reason, cands=[{bot, spell, rank, why}],
--           rejected=[same: judged not worth it], primary, backup}]}   the brain's duty needs as
--           offered (u:offers), at most every rec.offersInterval ms, when changed
--   pull    {type, t, planned, phase, target, targetName, ranged, returnToSpot,
--           estimate={sec, incomingDps, partyDps, dpsSource, manaStart, manaLeft, tankLowPct},
--           expected=[{g, name, aggro}], report=[lines the player was told]}   the pull as
--           planned (u:pull), once per pull and again when its target or expected list changed
--   trace   {type, t, id, parent, kind, label, data, in | inRef, pin}   one node of the Commander's
--           calculation tree (u:trace): kind brain / assigner / bot / pull / patrolcheck /
--           control / fight / foe / party / patrol; data = its values (JSON object); in = the
--           full input of a pure calculation (FightLoad: fightload(json) re-runs it), inRef =
--           id of an earlier trace line of this file with the same input; pin = "pull" /
--           "pullcheck" for the planning sets (written when the pull line is). Whole trees only.
--   end     {type, t, duration, result, stats={guid={...}}, names={guid=name},
--           spells={id=name} (every spell in the ev lines), anomalies={kind=count}, dropped}
--
-- Timestamps are server ms (now()). Events from the 3 s before the fight are written after begin.

rec = rec or {}
rec.dir = rec.dir or "../logs/combat"
-- 200 ms: the Commander layers update about every 200 ms (2026-11-07); slower sampling missed
-- target switches between two snaps
rec.interval = rec.interval or 200
rec.endAfterMs = rec.endAfterMs or 2000
rec.offersInterval = rec.offersInterval or 1000
-- Calculation trace (Commander/Trace/Trace.h, u:trace): kept here for rec.traceKeepMs, written
-- for the trees that ended within rec.traceBeforeMs before .. rec.traceAfterMs after an anomaly
-- (player marks included) and in the first rec.traceStartMs of the fight; the pull planning
-- (pinned) when the pull line is written.
rec.traceKeepMs = rec.traceKeepMs or 6000
rec.traceBeforeMs = rec.traceBeforeMs or 4000
rec.traceAfterMs = rec.traceAfterMs or 1000
rec.traceStartMs = rec.traceStartMs or 2000

local function ensureDir(dir)
    local win = dir:gsub("/", "\\")
    os.execute('if not exist "' .. win .. '" mkdir "' .. win .. '"')
end

local function write(w, obj)
    if w.file then w.file:write(json(obj), "\n") end
end
rec.writeLine = write

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
    if e.spell then review.spellName(w, e.spell) end
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

---------------------------------------------------------------------------
-- calculation trace
---------------------------------------------------------------------------

-- One trace line: the JSON texts of data and input go in as they are.
local function writeTrace(w, r, pin)
    if not w.file then return end
    local head = { type = "trace", t = r.t, id = r.id, parent = r.parent, kind = r.kind, label = r.label, pin = pin }
    local text = json(head)
    local extra = {}
    if r.data then extra[#extra + 1] = '"data":' .. r.data end
    if r.input then
        local ref = w.traceInputs[r.input]
        if ref then
            extra[#extra + 1] = '"inRef":' .. ref
        else
            w.traceInputs[r.input] = r.id
            extra[#extra + 1] = '"in":' .. r.input
        end
    end
    if #extra > 0 then text = text:sub(1, -2) .. "," .. table.concat(extra, ",") .. "}" end
    w.file:write(text, "\n")
end

-- Poll the leader's trace (arms it) into w.traceBuf; trees by root id.
local function pollTrace(w, leader)
    if not leader.trace then return end
    local recs, gap = leader:trace(w.traceSeq or 0)
    if not recs then return end
    if gap and w.file then write(w, { type = "gap", t = now(), what = "trace" }) end
    local buf = w.traceBuf
    for _, r in ipairs(recs) do
        buf.byId[r.id] = r
        buf.list[#buf.list + 1] = r
        if r.id > (w.traceSeq or 0) then w.traceSeq = r.id end
    end
    local cutoff = now() - rec.traceKeepMs
    local keep = {}
    for _, r in ipairs(buf.list) do
        if r.t >= cutoff then keep[#keep + 1] = r else buf.byId[r.id] = nil end
    end
    buf.list = keep
end

local function rootOf(buf, r)
    local seen = 0
    while r.parent and buf.byId[r.parent] and seen < 64 do
        r = buf.byId[r.parent]
        seen = seen + 1
    end
    return r
end

-- Every buffered tree whose root ended inside an open window, once.
local function writeTraceWindows(w)
    if not w.file or #w.traceWindows == 0 then return end
    local t = now()
    local open = {}
    for _, win in ipairs(w.traceWindows) do if t <= win.to + rec.interval then open[#open + 1] = win end end
    local buf = w.traceBuf
    local roots = {}
    for _, r in ipairs(buf.list) do
        local root = rootOf(buf, r)
        if not w.traceDone[root.id] then
            for _, win in ipairs(open) do
                if root.t >= win.from and root.t <= win.to then roots[root.id] = true break end
            end
        end
    end
    -- children before their parents, as added (buffer order)
    for _, r in ipairs(buf.list) do
        local root = rootOf(buf, r)
        if roots[root.id] then writeTrace(w, r) end
    end
    for id in pairs(roots) do w.traceDone[id] = true end
    local still = {}
    for _, win in ipairs(open) do if t <= win.to then still[#still + 1] = win end end
    w.traceWindows = still
end

local function openTraceWindow(w, from, to)
    w.traceWindows[#w.traceWindows + 1] = { from = from, to = to }
end

-- The pinned planning sets ("pull", "pullcheck") when they changed since last written.
local function writePinnedTrace(w, leader)
    if not leader.tracepinned or not w.file then return end
    for _, tag in ipairs({ "pull", "pullcheck" }) do
        local recs, version = leader:tracepinned(tag, w.pinVersion[tag])
        if recs then
            for _, r in ipairs(recs) do writeTrace(w, r, tag) end
        end
        if version then w.pinVersion[tag] = version end
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
    w.spellNames = {}
    w.lastDec = {}
    -- notes from the 3 s before the fight are kept (the patrol / pull judgement comes first)
    w.lastNote = w.fightStart - 3000
    w.lastPlan = nil
    w.lastPull = nil
    w.traceInputs = {}
    w.traceDone = {}
    w.traceWindows = {}
    w.pinVersion = {}
    w.anomaliesSeen = 0
    openTraceWindow(w, w.fightStart - 1000, w.fightStart + rec.traceStartMs)
    for _, m in ipairs(members) do w.names[m:guid()] = m:name() end
    review.begin(w, members)
    write(w, { type = "begin", t = w.fightStart, label = w.label, file = name, members = memberRows(members) })
    for _, e in ipairs(w.pre) do
        e.type = "ev"
        write(w, e)
        account(w, e)
        w.review.recent[#w.review.recent + 1] = e
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
    -- fight-end checks (estimate vs actual): their anomalies go before the end line
    local ok, err = pcall(review.finish, w)
    if not ok then dev.pushError("review.finish", err) end
    local counts = w.review.counts
    local spells = {}
    for id, n in pairs(w.spellNames) do spells[tostring(id)] = n end
    write(w, { type = "end", t = now(), duration = now() - w.fightStart, reason = reason,
               result = result, aliveMembers = alive, aggroLost = counts.aggro or 0, anomalies = counts,
               stats = w.stats, names = w.names, spells = spells, dropped = combatlog.dropped() })
    w.file:close()
    w.file = nil
    rec.lastSummary = { file = w.path, duration = now() - w.fightStart, anomalies = counts, result = result }
    local leader = w.leader and unit(w.leader)
    if leader then
        local parts = {}
        for kind, n in pairs(counts) do parts[#parts + 1] = (review.labels[kind] or kind) .. " " .. n end
        leader:tell(string.format("[录制] 战斗结束 %.1f 秒，%s，异常：%s。记录：%s", (now() - w.fightStart) / 1000,
            result, #parts > 0 and table.concat(parts, "，") or "无", w.path))
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

    -- (also before the fight: keeps the trace armed for the pull planning)
    pollTrace(w, leader)

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
    local party = {}
    for _, m in ipairs(members) do
        local i = m:info()
        party[#party + 1] = i
        w.names[i.guid] = i.name
    end
    local enemyRows = {}
    for _, e in ipairs(enemies) do
        local i = e:info()
        i.threat = map(e:threat(), function(r) return { g = r.unit:guid(), v = r.threat } end)
        enemyRows[#enemyRows + 1] = i
        w.names[i.guid] = i.name
    end

    -- Everything of this tick is read first, so the review detectors see it before it is written:
    -- events, Commander decisions of the bots since the last tick, notes, plan, pull.
    local evs = combatlog.drain()
    local decs, gaps = {}, {}
    for _, m in ipairs(members) do
        local log, gap = m:decisions(w.lastDec[m:guid()] or 0)
        if log then
            for _, d in ipairs(log) do
                decs[#decs + 1] = { type = "dec", t = d.t, bot = m:guid(), cands = d.cands }
                w.lastDec[m:guid()] = d.t
            end
            if gap then gaps[#gaps + 1] = m:guid() end
        end
    end
    -- (u:notes / u:plan are missing on a mangosd built before 2026-11-02: skipped)
    local notes = leader.notes and leader:notes(w.lastNote) or {}
    local plan = leader.plan and leader:plan()
    local pull = leader.pull and leader:pull()

    write(w, { type = "snap", t = now(), party = party, enemies = enemyRows })
    review.observe(w, { leader = leader, members = members, party = party, enemies = enemies,
                        enemyRows = enemyRows, evs = evs, decs = decs, plan = plan, pull = pull, now = now() })

    -- anomalies found this tick (marks too): the calculations around them
    local found = w.review and w.review.list or {}
    for k = (w.anomaliesSeen or 0) + 1, #found do
        local a = found[k]
        openTraceWindow(w, a.t - rec.traceBeforeMs, a.t + rec.traceAfterMs)
    end
    w.anomaliesSeen = #found

    for _, g in ipairs(gaps) do write(w, { type = "gap", t = now(), bot = g, what = "dec" }) end
    for _, d in ipairs(decs) do write(w, d) end

    -- Commander notes to the leader since the last tick, and the plan state when it changed
    for _, n in ipairs(notes) do
        write(w, { type = "note", t = n.t, text = n.text })
        w.lastNote = n.t
    end
    if plan then
        local text = json(plan)
        if text ~= w.lastPlan then
            write(w, { type = "plan", t = now(), plan = plan })
            w.lastPlan = text
        end
    end
    -- The brain's duty offers (u:offers: candidates with their value, rejected ones too), at most
    -- every rec.offersInterval ms and only when they changed: why a control or a taunt was or
    -- was not given (points change every refresh, so not on every tick).
    if leader.offers and now() - (w.lastOffersAt or 0) >= rec.offersInterval then
        w.lastOffersAt = now()
        local offers = leader:offers()
        if #offers > 0 then
            local text = json(offers)
            if text ~= w.lastOffers then
                write(w, { type = "offers", t = now(), offers = offers })
                w.lastOffers = text
            end
        end
    end
    -- The pull as planned (u:pull): once per pull, again when the target or the expected list
    -- changed (recheck before the tank goes, another target after a failed shot spot).
    if pull then
        local key = string.format("%d/%s/%d", pull.t or 0, pull.target or "", #(pull.expected or {}))
        if key ~= w.lastPull then
            pull.type = "pull"
            pull.planned = pull.t
            pull.t = now()
            write(w, pull)
            w.lastPull = key
            writePinnedTrace(w, leader)
        end
    end
    writeTraceWindows(w)

    for _, e in ipairs(evs) do
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
    local w = { leader = leader:guid(), label = label or "fight", pre = {}, traceBuf = { list = {}, byId = {} },
                traceWindows = {}, traceDone = {}, traceInputs = {}, pinVersion = {} }
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

-- A player mark in the fight being recorded (".scenario mark [text]"): anomaly kind "mark" with
-- the live state. Returns the anomaly id, nil when no fight is being recorded.
function rec.mark(who, text)
    local w = rec.watcher
    if not w or not w.file then return nil end
    local id = review.mark(w, who, text or "")
    w.file:flush()
    return id
end

function rec.status()
    local w = rec.watcher
    if not w then return { watching = false, last = rec.last, lastSummary = rec.lastSummary } end
    local leader = unit(w.leader)
    return { watching = true, leader = leader and leader:name(), label = w.label, recording = w.file ~= nil,
             file = w.path, last = rec.last, lastSummary = rec.lastSummary }
end
