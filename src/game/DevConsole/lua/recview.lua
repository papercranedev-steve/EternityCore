-- Fight recordings for the PartyBot addon's fight log window (addons/PartyBot/Recordings.lua):
-- the newest files of rec.dir, read back and sent as addon messages with the prefix PBREC
-- (u:addonmsg, one message <= 255 bytes with prefix and tab, fields separated by ';').
--
--   .scenario rec list          the newest recview.listCount fights, newest first
--       L;<file>;<result>;<duration ms>;<alive>;<members>;<anomalies text>
--           result: ok / wipe / stopped / running (no end line yet)
--       LZ;<count>
--   .scenario rec open <file>   one fight
--       B;<file>;<lines in the file>
--       H;<result>;<reason>;<duration ms>;<alive>;<members>;<label>
--       N;<idx>;<class>;<role>;<name>        a unit; rows name units by idx (0 = none). Sent
--                                            again when a later line has its name.
--       M;<idx>;<dmgDone>;<dmgTaken>;<healDone>;<healEff>;<healTaken>;<casts>;<cancels>;<fails>;<deaths>;<fails by>
--       rows, t = ms since the fight began (negative: the 3 s before it):
--       E;<t>;<ev>;<src>;<dst>;<spell>;<amount>;<extra>;<crit 0/1>;<reason>   combat event (recorder.lua ev)
--       D;<t>;<bot>;<res>;<tier>;<score>;<text>  an arbiter candidate that ran or was set (exec,
--                                            failed, order, veto); text = module, action, reason
--       O;<t>;<text>                         a Commander note
--       A;<t>;<id>;<sev>;<kind>;<who>;<text> an anomaly (review.lua); text = cause and links
--       P;<t>;<text>                         the pull as planned
--       +;<text>                             the rest of the previous line's last field
--       G;<lines done>;<lines in the file>   progress, once per tick
--       Z;<rows>                             done
--       X;<text>                             error
-- The file is read in steps (recview.linesPerTick lines, recview.msgsPerTick messages per world
-- tick), so a big fight takes a few seconds and the server does not stall.

recview = recview or {}
recview.listCount = recview.listCount or 3
recview.linesPerTick = recview.linesPerTick or 400
recview.msgsPerTick = recview.msgsPerTick or 120
recview.jobs = recview.jobs or {}      -- player guid -> timer id

local PREFIX = "PBREC"
local MAX_BODY = 255 - #PREFIX - 1

local function clean(s)
    return (tostring(s or ""):gsub("[;\r\n]", function(c) return c == ";" and "," or " " end))
end

-- s cut to at most n bytes without splitting a UTF-8 character
local function cut(s, n)
    if #s <= n then return s, "" end
    while n > 0 do
        local b = s:byte(n + 1)
        if not b or b < 0x80 or b >= 0xC0 then break end
        n = n - 1
    end
    return s:sub(1, n), s:sub(n + 1)
end

-- head = fields without the free text; text = last field (may be long: continued in "+" lines)
local function send(p, head, text)
    local body = text and (head .. ";" .. clean(text)) or head
    local first, rest = cut(body, MAX_BODY)
    p:addonmsg(PREFIX, first)
    local n = 1
    while rest ~= "" do
        local part
        part, rest = cut(rest, MAX_BODY - 2)
        p:addonmsg(PREFIX, "+;" .. part)
        n = n + 1
    end
    return n
end

local function dirWin()
    return (rec.dir:gsub("/", "\\"))
end

-- newest first
local function listFiles(n)
    local out = {}
    local pipe = io.popen('dir /b /o-d "' .. dirWin() .. '\\*.jsonl" 2>nul')
    if not pipe then return out end
    for name in pipe:lines() do
        if #out >= n then break end
        out[#out + 1] = name
    end
    pipe:close()
    return out
end

local function firstLine(path)
    local f = io.open(path, "r")
    if not f then return nil end
    local line = f:read("l")
    f:close()
    return line
end

-- the last line of the file (the end line is at most a few KB)
local function lastLine(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local size = f:seek("end")
    local from = math.max(0, size - 65536)
    f:seek("set", from)
    local tail = f:read("a") or ""
    f:close()
    tail = tail:gsub("[\r\n]+$", "")
    return tail:match("([^\n]*)$")
end

local function decodeIf(line, kind)
    if line and line:find('"type":"' .. kind .. '"', 1, true) then return jsondecode(line) end
end

local function anomalyText(counts)
    local parts = {}
    if type(counts) == "table" then
        for kind, n in pairs(counts) do parts[#parts + 1] = (review.labels[kind] or kind) .. " " .. n end
    end
    table.sort(parts)
    return #parts > 0 and table.concat(parts, "，") or "无异常"
end

function recview.list(p)
    local files = listFiles(recview.listCount)
    for _, name in ipairs(files) do
        local path = rec.dir .. "/" .. name
        local b = decodeIf(firstLine(path), "begin")
        local e = decodeIf(lastLine(path), "end")
        local members = b and b.members and #b.members or 0
        if e then
            send(p, string.format("L;%s;%s;%d;%d;%d", name, clean(e.result), e.duration or 0, e.aliveMembers or 0, members),
                anomalyText(e.anomalies))
        else
            send(p, string.format("L;%s;running;0;0;%d", name, members), "")
        end
    end
    p:addonmsg(PREFIX, "LZ;" .. #files)
end

---------------------------------------------------------------------------
-- open: one fight, in steps
---------------------------------------------------------------------------

local SHOWN_RESULTS = { exec = true, failed = true, order = true, veto = true }

local function newJob(p, name, lines)
    return { player = p:guid(), name = name, lines = lines, pos = 1, rows = 0, idx = {}, names = {}, nextIdx = 0, t0 = 0 }
end

-- index of a unit, announcing it (N line) the first time or when the name became known
local function unitIdx(job, p, guid, name, class, role)
    if not guid or guid == "" then return 0 end
    local i = job.idx[guid]
    local known = job.names[guid]
    if i and (known or not name) then return i end
    if not i then
        job.nextIdx = job.nextIdx + 1
        i = job.nextIdx
        job.idx[guid] = i
    end
    if name then job.names[guid] = name end
    job.msgs = job.msgs + send(p, string.format("N;%d;%d;%s", i, class or 0, clean(role)), name or "?")
    return i
end

local function rel(job, t) return math.floor((t or job.t0) - job.t0) end

local function row(job, p, head, text)
    job.rows = job.rows + 1
    job.msgs = job.msgs + send(p, head, text)
end

local function sendHeader(job, p, b, e)
    job.t0 = b and b.t or 0
    if e then
        send(p, string.format("H;%s;%s;%d;%d;%d", clean(e.result), clean(e.reason), e.duration or 0, e.aliveMembers or 0,
            b and #b.members or 0), b and b.label or "")
    else
        send(p, string.format("H;running;;0;0;%d", b and #b.members or 0), b and b.label or "")
    end
    for _, m in ipairs(b and b.members or {}) do unitIdx(job, p, m.guid, m.name, m.class, m.role) end
    if e and type(e.names) == "table" then
        for guid, name in pairs(e.names) do unitIdx(job, p, guid, name) end
    end
    if e and type(e.stats) == "table" then
        for _, m in ipairs(b and b.members or {}) do
            local s = e.stats[m.guid]
            if s then
                local fails = {}
                if type(s.failsBy) == "table" then
                    for why, n in pairs(s.failsBy) do fails[#fails + 1] = why .. " " .. n end
                end
                send(p, string.format("M;%d;%d;%d;%d;%d;%d;%d;%d;%d;%d", job.idx[m.guid], s.dmgDone or 0, s.dmgTaken or 0,
                    s.healDone or 0, s.healEff or 0, s.healTaken or 0, s.casts or 0, s.cancels or 0, s.fails or 0,
                    s.deaths or 0), table.concat(fails, ", "))
            end
        end
    end
end

local handlers = {}

handlers.ev = function(job, p, r)
    local src = unitIdx(job, p, r.src)
    local dst = unitIdx(job, p, r.dst)
    row(job, p, string.format("E;%d;%s;%d;%d;%d;%d;%d;%d", rel(job, r.t), clean(r.ev), src, dst, r.spell or 0,
        math.floor(r.amount or 0), math.floor(r.extra or 0), r.crit and 1 or 0), r.reason or "")
end

handlers.dec = function(job, p, r)
    local bot = unitIdx(job, p, r.bot)
    for _, c in ipairs(r.cands or {}) do
        if SHOWN_RESULTS[c.res] then
            local text = (c.src or "") .. " " .. (c.what or "") .. ((c.reason and c.reason ~= "") and ("（" .. c.reason .. "）") or "")
            row(job, p, string.format("D;%d;%d;%s;%d;%.1f", rel(job, r.t), bot, clean(c.res), c.tier or 0, c.score or 0), text)
        end
    end
end

handlers.note = function(job, p, r)
    row(job, p, string.format("O;%d", rel(job, r.t)), r.text)
end

handlers.anomaly = function(job, p, r)
    local text = r.cause or ""
    if type(r.links) == "table" and #r.links > 0 then
        text = text .. "（← #" .. table.concat(map(r.links, tostring), " #") .. "）"
    end
    local who = r.whoG and unitIdx(job, p, r.whoG, r.who) or 0
    row(job, p, string.format("A;%d;%d;%s;%s;%d", rel(job, r.t), r.id or 0, clean(r.sev), clean(r.kind), who), text)
end

handlers.pull = function(job, p, r)
    local est = r.estimate or {}
    local text = string.format("开怪计划：%s，%s，预计 %d 只%s", r.targetName or "?", r.ranged and "远程开" or "走过去开",
        type(r.expected) == "table" and #r.expected or 0, est.sec and string.format("，估计打 %.0f 秒", est.sec) or "")
    row(job, p, string.format("P;%d", rel(job, r.t)), text)
end

local TYPES = { "ev", "dec", "note", "anomaly", "pull" }

-- A recording without its end line (still running, or the server stopped): names from the snaps.
local function snapNames(job, p, line)
    local r = jsondecode(line)
    if not r then return end
    for _, list in ipairs({ r.party or {}, r.enemies or {} }) do
        for _, u in ipairs(list) do
            if u.guid and not job.names[u.guid] then unitIdx(job, p, u.guid, u.name, u.class, u.role) end
        end
    end
end

local function step(job)
    local p = unit(job.player)
    if not p then return false end
    job.msgs = 0
    local stop = math.min(#job.lines, job.pos + recview.linesPerTick - 1)
    while job.pos <= stop and job.msgs < recview.msgsPerTick do
        local line = job.lines[job.pos]
        job.pos = job.pos + 1
        local kind
        for _, k in ipairs(TYPES) do
            if line:find('"type":"' .. k .. '"', 1, true) then kind = k break end
        end
        if kind then
            local r = jsondecode(line)
            if r and r.type == kind then handlers[kind](job, p, r) end
        elseif not job.ended and line:find('"type":"snap"', 1, true) then
            snapNames(job, p, line)
        end
    end
    p:addonmsg(PREFIX, string.format("G;%d;%d", job.pos - 1, #job.lines))
    if job.pos > #job.lines then
        p:addonmsg(PREFIX, "Z;" .. job.rows)
        recview.jobs[job.player] = nil
        return false
    end
end

function recview.open(p, name)
    if not name or not name:match("^[%w_%-]+%.jsonl$") then
        return p:addonmsg(PREFIX, "X;文件名不对")
    end
    local path = rec.dir .. "/" .. name
    local f = io.open(path, "r")
    if not f then return p:addonmsg(PREFIX, "X;找不到记录 " .. name) end
    local lines = {}
    for line in f:lines() do lines[#lines + 1] = line end
    f:close()

    cancel(recview.jobs[p:guid()])
    local job = newJob(p, name, lines)
    job.msgs = 0
    p:addonmsg(PREFIX, string.format("B;%s;%d", name, #lines))
    local b = decodeIf(lines[1], "begin")
    local e = decodeIf(lines[#lines], "end")
    job.ended = e ~= nil
    sendHeader(job, p, b, e)
    job.pos = 2
    recview.jobs[job.player] = every(1, function() return step(job) end)
end

-- .scenario rec [list | open <file>]
function recview.command(p, args)
    args = (args or ""):match("^%s*(.-)%s*$")
    if args == "" or args == "list" then
        recview.list(p)
    elseif args:match("^open%s") then
        recview.open(p, args:match("^open%s+(%S+)"))
    else
        p:addonmsg(PREFIX, "X;用法：.scenario rec list | open <文件>")
    end
end
