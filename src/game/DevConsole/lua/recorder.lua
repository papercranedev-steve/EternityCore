-- Combat recorder. rec.watch(leader, label) follows one party; every fight becomes one JSONL file
-- in rec.dir (relative to the mangosd working directory, i.e. repack\server).
--
-- Line types:
--   begin   {type, t, label, file, level (rec.level), members=[{guid,name,class,role,level}]}
--   snap    {type, t, party=[info], enemies=[info + threat=[{g,v}]]}   every rec.interval ms;
--           info without the unit line's fields (name, class, level, maxhp, ...)
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
--           focus, pull, clear, retreat, patrol{phase, why, avoid, lures, kill}, interrupts,
--           intercepts, controls[{enemy, bot, spell, area}], threat, area,
--           doctrine{phase, tactic, tank, outliers, why, enemies=[{g, tags}]}; {} = nothing running
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
--           A subtree equal to the last one at the same place: {type, t, id, parent, kind, same}
--           (same = id of the written one).
--   unit    {type, t, guid, g, name, class, level, entry, map, maxhp, maxpower, ptype, player, role}
--           the fields of a snap row that rarely change, before the snap that first has the unit
--           and again when they changed (snap rows don't repeat them); g = full guid without 0x
--   end     {type, t, duration, result, stats={id={...}}, names={id=name}, guids={id=guid without 0x},
--           spells={id=name} (every spell in the ev lines), anomalies={kind=count}, dropped}
--
-- Timestamps are server ms (now()). Events from the 3 s before the fight are written after begin.
-- Since 2026-11-14 the lines are compacted (see "compact output" below): units are short ids of
-- the file (a party member's name, "#134BB" for a creature), false / "" / empty-guid fields are
-- left out, numbers have at most 2 decimals, and a dec line equal to the bot's previous one is
-- folded into {type="dec", t, bot, rep=n}.

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

-- Recording level (2026-11-14): how much of the calculation trace goes in the file. snap / ev /
-- dec / plan / anomaly lines are written at every level (the review needs them).
--   verbose   the trace of the whole fight; repeated decisions written one by one
--   normal    trace around every anomaly
--   warning   trace around mid and high anomalies (low ones: idle, stuck, ccmove get their
--             anomaly line only)
--   danger    trace around high anomalies only (deaths, heal gaps, player marks, ...)
-- The fight's start and the pull planning are written at every level. Set with rec.level = "..."
-- (wowdbg.ps1); it is read when a fight begins and written in the begin line.
rec.level = rec.level or "warning"
local LEVEL_MIN_SEV = { verbose = 1, normal = 1, warning = 2, danger = 3 }
local SEV_RANK = { low = 1, mid = 2, high = 3 }

local function ensureDir(dir)
    local win = dir:gsub("/", "\\")
    os.execute('if not exist "' .. win .. '" mkdir "' .. win .. '"')
end

---------------------------------------------------------------------------
-- compact output (2026-11-14: the files were read by the AI and cost too many tokens)
---------------------------------------------------------------------------
-- Applied to the JSON text of every line, so the server-side objects (review.lua works on them)
-- keep their full guids:
--   * unit ids: a guid "0x0000000000000005" / "0xF1300006AB0134BB" is written as a short id of
--     this file: a player by name ("Ki"), a creature "#" + its spawn counter ("#134BB"), a pet
--     "pet#..", anything else "x#.."; a clash gets "_2", "_3". The empty guid is left out.
--     unit lines and end.guids give the full guid of an id (without "0x").
--   * fields whose value is false, "" or the empty guid are left out (missing = false / none).
--   * numbers: at most 2 decimals, "12.0" -> "12" (not in a trace's `in`: re-run exactly).
--   * " spell0" (no spell) in the decision texts is left out.
local EMPTY_GUID = "0x0000000000000000"
local GUID_PATTERN = "0x(" .. string.rep("%x", 16) .. ")%f[^%w]"

local function shortId(w, hex)
    hex = hex:upper()
    local id = w.ids[hex]
    if id then return id end
    local high = hex:sub(1, 4)
    local low = hex:sub(-6):gsub("^0+", "")
    if low == "" then low = "0" end
    local name = w.names["0x" .. hex]
    if high == "0000" and name and not name:find("[^%w]") and not w.idGuid[name] then id = name
    elseif high == "0000" then id = "p#" .. (hex:sub(5):gsub("^0+", ""))
    elseif high == "F130" then id = "#" .. low
    elseif high == "F140" then id = "pet#" .. low
    else id = "x#" .. (hex:sub(5):gsub("^0+", "")) end
    if w.idGuid[id] then
        local n = 2
        while w.idGuid[id .. "_" .. n] do n = n + 1 end
        id = id .. "_" .. n
    end
    w.ids[hex] = id
    w.idGuid[id] = hex
    return id
end

local DEFAULT_VALUES = { "false", '""', '"' .. EMPTY_GUID .. '"' }

local function dropDefaults(text)
    for _, v in ipairs(DEFAULT_VALUES) do
        text = text:gsub(',"[%w_]+":' .. v .. "%f[,}]", "")
                   :gsub('{"[%w_]+":' .. v .. ",", "{")
                   :gsub('{"[%w_]+":' .. v .. "}", "{}")
    end
    return text
end

local function roundNumber(num, exp)
    if exp ~= "" then return num .. exp end      -- 1.23457e+06: %.6g already
    local s = string.format("%.2f", tonumber(num)):gsub("0+$", ""):gsub("%.$", "")
    if s == "-0" then s = "0" end
    return s
end

local function compact(w, text, exact)
    text = dropDefaults(text)
    text = text:gsub(GUID_PATTERN, function(hex) return shortId(w, hex) end)
    if not exact then text = text:gsub("(%-?%d+%.%d%d%d+)([eE]?)", roundNumber) end
    text = text:gsub("(%d)%.0+([,}%]])", "%1%2")
    return (text:gsub(" spell0([\" ])", "%1"))
end

local function writeText(w, text)
    if w.file then w.file:write(compact(w, text), "\n") end
end

local function write(w, obj)
    if w.file then writeText(w, json(obj)) end
end
rec.writeLine = write

-- Snap rows without the fields that rarely change (name, class, level, maxhp, ...): those go in
-- a unit line {type, t, guid, g, <static fields>} before the snap, when first seen or changed.
local STATIC_FIELDS = { name = true, class = true, level = true, entry = true, map = true, maxhp = true,
                        maxpower = true, ptype = true, player = true, role = true }

local function snapRows(w, rows)
    local out = {}
    for _, i in ipairs(rows) do
        local static, live = {}, {}
        for k, v in pairs(i) do
            if STATIC_FIELDS[k] then static[k] = v else live[k] = v end
        end
        local key = json(static)
        if i.guid and w.unitStatic[i.guid] ~= key then
            w.unitStatic[i.guid] = key
            static.type = "unit"
            static.t = now()
            static.guid = i.guid
            static.g = i.guid:sub(3)
            write(w, static)
        end
        out[#out + 1] = live
    end
    return out
end

-- Decision lines: the same decision of a bot again (same candidates, scores and results) is not
-- written; when it changes (or the fight ends) one {type="dec", t, bot, rep=n} line says the
-- previous one was made n more times, the last at t.
local function flushDecRun(w, bot)
    local run = w.decRuns[bot]
    if run and run.n > 0 then
        write(w, { type = "dec", t = run.t, bot = bot, rep = run.n })
        run.n = 0
    end
end

local function writeDec(w, d)
    local key = json(d.cands)
    local run = w.decRuns[d.bot]
    if run and run.key == key and w.level ~= "verbose" then
        run.n = run.n + 1
        run.t = d.t
        return
    end
    flushDecRun(w, d.bot)
    write(w, d)
    w.decRuns[d.bot] = { key = key, n = 0, t = d.t }
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

-- One trace line: the JSON texts of data and input go in as they are (compacted; the input
-- without rounding, fightload() re-runs it).
local function writeTrace(w, r, pin)
    if not w.file then return end
    local head = { type = "trace", t = r.t, id = r.id, parent = r.parent, kind = r.kind, label = r.label, pin = pin }
    local text = compact(w, json(head))
    local extra = {}
    if r.data then extra[#extra + 1] = '"data":' .. compact(w, r.data) end
    if r.input then
        local ref = w.traceInputs[r.input]
        if ref then
            extra[#extra + 1] = '"inRef":' .. ref
        else
            w.traceInputs[r.input] = r.id
            extra[#extra + 1] = '"in":' .. compact(w, r.input, true)
        end
    end
    if #extra > 0 then text = text:sub(1, -2) .. "," .. table.concat(extra, ",") .. "}" end
    w.file:write(text, "\n")
end

-- Values that differ every time without changing what was decided: left out of the comparison
-- below (the position field's build time).
local function sigText(kind, text)
    if kind == "field" then text = text:gsub(",? %d+ us$", ""):gsub('"micros":%d+,?', "") end
    return text
end

-- Signature of a subtree: kinds, labels, data and inputs of it and its children (ids and times
-- left out).
local function treeSig(kids, r)
    local parts = { r.kind or "", sigText(r.kind, r.label or ""), sigText(r.kind, r.data or ""), r.input or "" }
    for _, c in ipairs(kids[r.id] or {}) do parts[#parts + 1] = "(" .. treeSig(kids, c) .. ")" end
    return table.concat(parts, "\1")
end

-- A tree, parents first. A subtree equal to the last one written at the same place (same path of
-- kinds and labels) is one line {type, t, id, parent, kind, same = id of that one}: most of a
-- brain refresh is the same as 200 ms before.
local function writeTree(w, kids, r, path)
    local key = path .. "/" .. (r.kind or "") .. "|" .. sigText(r.kind, r.label or "")
    local sig = treeSig(kids, r)
    local prev = w.traceSigs[key]
    if prev and prev.sig == sig then
        writeText(w, json({ type = "trace", t = r.t, id = r.id, parent = r.parent, kind = r.kind, same = prev.id }))
        return
    end
    w.traceSigs[key] = { sig = sig, id = r.id }
    writeTrace(w, r)
    for _, c in ipairs(kids[r.id] or {}) do writeTree(w, kids, c, key) end
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
    -- the chosen trees, parents first, children in id order
    local kids, rootList = {}, {}
    for _, r in ipairs(buf.list) do
        local root = rootOf(buf, r)
        if roots[root.id] then
            if r == root then
                rootList[#rootList + 1] = r
            else
                local list = kids[r.parent]
                if not list then list = {} kids[r.parent] = list end
                list[#list + 1] = r
            end
        end
    end
    for _, list in pairs(kids) do table.sort(list, function(a, b) return a.id < b.id end) end
    table.sort(rootList, function(a, b) return a.id < b.id end)
    for _, r in ipairs(rootList) do writeTree(w, kids, r, "") end
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
    w.traceSigs = {}
    w.ids = {}
    w.idGuid = {}
    w.unitStatic = {}
    w.decRuns = {}
    w.anomaliesSeen = 0
    w.level = LEVEL_MIN_SEV[rec.level] and rec.level or "warning"
    if w.level == "verbose" then
        openTraceWindow(w, w.fightStart - 1000, math.huge)
    else
        openTraceWindow(w, w.fightStart - 1000, w.fightStart + rec.traceStartMs)
    end
    for _, m in ipairs(members) do w.names[m:guid()] = m:name() end
    review.begin(w, members)
    write(w, { type = "begin", t = w.fightStart, label = w.label, file = name, level = w.level,
               members = memberRows(members) })
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
    for bot in pairs(w.decRuns) do flushDecRun(w, bot) end
    -- every unit of the end line has its id before end.guids is written
    for _, list in ipairs({ w.names, w.stats }) do
        for g in pairs(list) do
            local hex = type(g) == "string" and g:match("^0x(%x+)$")
            if hex and #hex == 16 then shortId(w, hex) end
        end
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
               stats = w.stats, names = w.names, spells = spells, dropped = combatlog.dropped(), guids = w.idGuid })
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

    -- (copies: review.observe below reads the full rows)
    local partyLive = snapRows(w, party)
    local enemyLive = snapRows(w, enemyRows)
    write(w, { type = "snap", t = now(), party = partyLive, enemies = enemyLive })
    review.observe(w, { leader = leader, members = members, party = party, enemies = enemies,
                        enemyRows = enemyRows, evs = evs, decs = decs, plan = plan, pull = pull, now = now() })

    -- anomalies found this tick (marks too): the calculations around them, from the level's
    -- severity up
    local found = w.review and w.review.list or {}
    for k = (w.anomaliesSeen or 0) + 1, #found do
        local a = found[k]
        if (SEV_RANK[a.sev] or 2) >= LEVEL_MIN_SEV[w.level] then
            openTraceWindow(w, a.t - rec.traceBeforeMs, a.t + rec.traceAfterMs)
        end
    end
    w.anomaliesSeen = #found

    for _, g in ipairs(gaps) do write(w, { type = "gap", t = now(), bot = g, what = "dec" }) end
    for _, d in ipairs(decs) do writeDec(w, d) end

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
