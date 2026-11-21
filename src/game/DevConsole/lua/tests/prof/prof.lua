-- Companion profession tests (design .kiro/docs/2026-11-21-companion-professions.md): one
-- button per case in the addon's 专业测试 window (/pbp). Temporary bots made temporary companions
-- (nothing is written to the database), professions set or rolled, then either automatic steps
-- with checks (offline growth, recruiting rolls) or a watch while the player plays the case
-- (town trip, Artisan trip, gathering). Everything goes to a log file the agent reads:
--   ../logs/proftest/<case>-<yyyyMMdd-HHmmss>.log  (relative to mangosd: repack/logs/proftest)
-- Lines: SETUP / CHECK OK|FAIL / STEP / CHANGE / NOTE, with proftable() snapshots.
--
--   .scenario prof [list]       -> PBPROF;L;<id>;<title>;<mode auto|watch>  PBPROF;H;<id>;<hint>  ...  PBPROF;E;<current>
--   .scenario prof <id> | reset | stop
--   .scenario prof note [text]  a mark in the log with a snapshot of every bot
--   PBPROF;S;loading|ready|error|stopped;<text>
--   PBPROF;R;<ok>;<fail>;<log file name>          after the automatic steps
--   PBPROF;U;<name>;<classId>;<money>;<professions> every 2 s while a case runs
--
-- Case (cases.lua): id, title, hint, at = "capital" | nil (stay where you are),
--   bots = { { class, role, level (0 = the leader's), money, tags = {...}, items = { {entry, n} },
--              recruit = true (PastProfessions) | profs = { {skill, value}, ... } } },
--   steps = function(t) ... end   automatic part (t: see makeT below)
--   watch = true                  logs changes until stopped

proftest = proftest or {}

local DIR = SCRIPT_DIR .. "/tests/prof"
local LOG_DIR = "../logs/proftest"
local MONITOR_MS = 2000
local AWAY_YD = 30
local HORDE_RACES = { [2] = true, [5] = true, [6] = true, [8] = true }
-- the capitals' middle (alliance: the Stormwind gate spot of the life tests, the trade district
-- within 150 yd; horde: the Valley of Strength)
local CAPITAL = {
    alliance = { map = 0, x = -8913.23, y = 554.63, z = 93.79, o = 0.64 },
    horde = { map = 1, x = 1629.36, y = -4373.39, z = 31.26, o = 3.55 },
}

do
    local chunk, err = loadfile(DIR .. "/cases.lua")
    if not chunk then error(err, 0) end
    proftest.list = chunk()
end

local function find(id)
    for _, c in ipairs(proftest.list) do
        if c.id == id then return c end
    end
end

local function say(p, text) p:tell("|cffddaa55[专业]|r " .. text) end
local function status(p, state, text) p:tell("PBPROF;S;" .. state .. ";" .. (text or "")) end
local botsOf = function(leader) return scenario.botsOf(leader) end

---------------------------------------------------------------------------
-- log file
---------------------------------------------------------------------------

local function openLog(id)
    local name = id .. "-" .. os.date("%Y%m%d-%H%M%S") .. ".log"
    for _, path in ipairs({ LOG_DIR .. "/" .. name, "../logs/proftest-" .. name }) do
        local f = io.open(path, "w")
        if f then
            f:close()
            proftest.file, proftest.fileName = path, name
            return
        end
    end
    proftest.file, proftest.fileName = nil, nil
end

local function log(text)
    if not proftest.file then return end
    local f = io.open(proftest.file, "a")
    if not f then return end
    for line in tostring(text):gmatch("[^\n]+") do f:write(os.date("%H:%M:%S "), line, "\n") end
    f:close()
end

local function snapshot(label, bots)
    for _, b in ipairs(bots) do log(label .. " " .. proftable(b)) end
end

local function fail(leader, msg)
    proftest.running = nil
    log("ERROR " .. msg)
    say(leader, "失败：" .. msg)
    status(leader, "error", msg)
end

local function guarded(leader, fn, ...)
    local ok, err = pcall(fn, ...)
    if not ok then fail(leader, tostring(err)) end
    return ok
end

---------------------------------------------------------------------------
-- monitor: the addon's live lines, and every change to the log
---------------------------------------------------------------------------

local function stopMonitor()
    cancel(proftest.monitor)
    proftest.monitor = nil
end

local function skillText(s)
    local t = string.format("%d/%d r%d", s.value, s.max, s.recipes.known)
    if s.tool and s.tool > 0 then t = t .. (s.hastool and " tool" or " notool") end
    if s.artisan and s.artisan.state then t = t .. " art " .. s.artisan.state .. "/" .. s.artisan.asks end
    return t
end

local function liveLine(b, p)
    local parts = {}
    for _, s in ipairs(p.list) do
        if s.kind ~= "secondary" then parts[#parts + 1] = s.name .. " " .. s.value .. "/" .. s.max end
    end
    return string.format("PBPROF;U;%s;%d;%d;%s", b:name(), b:class(), b:money(), table.concat(parts, ", "))
end

local function monitor(leader, c)
    stopMonitor()
    proftest.monitor = every(MONITOR_MS, function()
        if not leader:exists() then stopMonitor() return false end
        local bots = botsOf(leader)
        local ours = 0
        for _, b in ipairs(bots) do
            local st = proftest.state[b:name()]
            if st then
                ours = ours + 1
                local p = b:prof()
                leader:tell(liveLine(b, p))
                if c.watch then
                    for _, s in ipairs(p.list) do
                        local now = skillText(s)
                        if st.skills[s.name] and st.skills[s.name] ~= now then
                            log(string.format("CHANGE %s %s: %s -> %s", b:name(), s.name, st.skills[s.name], now))
                        end
                        st.skills[s.name] = now
                    end
                    local m = b:money()
                    if m ~= st.money then
                        log(string.format("CHANGE %s money %s -> %s (%s%s)", b:name(), gold(st.money), gold(m),
                            m < st.money and "-" or "+", gold(math.abs(m - st.money))))
                        st.money = m
                    end
                    local d = b:dist(leader)
                    local away = d == nil or d > AWAY_YD
                    if away ~= st.away then
                        log(string.format("CHANGE %s %s (%s)", b:name(), away and "left you" or "back with you",
                            d and string.format("%.0f yd", d) or "other map"))
                        st.away = away
                    end
                end
            end
        end
        if c.watch then
            local resting = leader:resting()
            if resting ~= proftest.leaderResting then
                log("CHANGE leader " .. (resting and "resting (city/inn)" or "not resting"))
                proftest.leaderResting = resting
            end
        end
        if ours == 0 then
            stopMonitor()
            log("END the bots are gone")
            proftest.current = nil
            status(leader, "stopped")
            return false
        end
    end)
end

---------------------------------------------------------------------------
-- automatic steps: the t table a case's steps(t) gets
---------------------------------------------------------------------------

local function makeT(leader, bots, c)
    local t = { ok = 0, fail = 0, leader = leader }
    local byClass = {}
    for _, b in ipairs(bots) do
        for name, id in pairs(scenario.CLASS_ID) do
            if id == b:class() then byClass[name] = b end
        end
    end
    function t.bot(class) return byClass[class] or error("no " .. class .. " bot") end
    t.bots = bots
    t.log = log
    function t.check(cond, text)
        if cond then t.ok = t.ok + 1 else t.fail = t.fail + 1 end
        log((cond and "CHECK OK   " or "CHECK FAIL ") .. text)
        return cond
    end
    -- the profession entry of u:prof() (nil = it does not have it)
    function t.skill(b, skill, p)
        for _, s in ipairs((p or b:prof()).list) do
            if s.skill == skill then return s end
        end
    end
    -- as if away `hours`: logs the result and the table; returns { gains = {skill = n}, spent, short, before }
    function t.away(b, hours)
        local before = b:money()
        local r = b:offline(hours)
        local gains = {}
        local text = {}
        for _, g in ipairs(r.gains) do
            gains[g.skill] = g.points
            text[#text + 1] = g.skill .. " +" .. g.points
        end
        log(string.format("STEP %s away %gh: %s, spent %s (money %s -> %s)%s", b:name(), hours,
            #text > 0 and table.concat(text, ", ") or "no gain", gold(r.spent), gold(before), gold(b:money()),
            r.short and ", SHORT OF MONEY" or ""))
        log(proftable(b))
        return { gains = gains, spent = r.spent, short = r.short, before = before }
    end
    function t.give(b, entry, count)
        leader:select(b)
        leader:cmd(string.format(".additem %d %d", entry, count or 1))
        log(string.format("STEP %s gets item %d x%d", b:name(), entry, count or 1))
    end
    return t
end

---------------------------------------------------------------------------
-- loading: leader to the spot -> bots -> professions -> steps / watch
---------------------------------------------------------------------------

local function levelOf(w, leader) return (w.level and w.level > 0) and w.level or leader:level() end

local function setupBot(leader, b, w, index, count)
    local level = levelOf(w, leader)
    scenario.applyPremades(b, level, w.role or "auto", leader)
    b:clearbags()
    b:setmoney(w.money or 0)
    for _, it in ipairs(w.items or {}) do
        leader:select(b)
        leader:cmd(string.format(".additem %d %d", it[1], it[2] or 1))
    end
    if not b:setcompanion(true, leader, w.tags or {}) then
        say(leader, "|cffffcc00注意|r：" .. b:name() .. " 没能设成同伴")
        log("WARN " .. b:name() .. " is not a temporary companion")
    end
    b:forgetprofs()
    if w.recruit then
        local r = b:reprof()
        log(string.format("SETUP %s recruit roll: %d (%d) + %d (%d), %d recipes", b:name(), r.first, r.firstvalue,
            r.second, r.secondvalue, r.recipes))
    else
        for _, p in ipairs(w.profs or {}) do b:setprof(p[1], p[2]) end
    end
    b:revive()
    b:refill()
    local at = leader:pos()
    local ang = at.o + (index - (count + 1) / 2) * 0.5
    b:tp(at.x + math.cos(ang) * 4.0, at.y + math.sin(ang) * 4.0, at.z, at.o + math.pi)
end

local function finish(leader, c)
    local bots = botsOf(leader)
    local used = {}
    proftest.state = {}
    local ours = {}
    for i, b in ipairs(bots) do
        local w
        for j, cand in ipairs(c.bots) do
            if not used[j] and scenario.CLASS_ID[cand.class] == b:class() then w = cand used[j] = true break end
        end
        if w then
            setupBot(leader, b, w, i, #bots)
            log(string.format("SETUP %s = %s level %d, tags %s, %s", b:name(), w.class, b:level(),
                w.tags and table.concat(w.tags, ",") or "-", w.note or ""))
            ours[#ours + 1] = b
        end
    end
    snapshot("SETUP", ours)
    for _, b in ipairs(ours) do
        local st = { skills = {}, money = b:money(), away = false }
        for _, s in ipairs(b:prof().list) do st.skills[s.name] = skillText(s) end
        proftest.state[b:name()] = st
    end
    proftest.running = nil
    proftest.current = c.id
    proftest.leaderResting = leader:resting()
    log("SETUP leader " .. leader:name() .. " level " .. leader:level() .. (proftest.leaderResting and ", resting" or ", not resting"))

    if c.steps then
        local t = makeT(leader, ours, c)
        local ok = guarded(leader, c.steps, t)
        log(string.format("RESULT %d ok, %d fail%s", t.ok, t.fail, ok and "" or ", steps stopped by an error"))
        leader:tell(string.format("PBPROF;R;%d;%d;%s", t.ok, t.fail, proftest.fileName or "-"))
        say(leader, string.format("自动检查：|cff20ff20%d 通过|r，%s", t.ok,
            t.fail > 0 and ("|cffff4040" .. t.fail .. " 失败|r") or "0 失败"))
    end
    if c.watch then
        log("WATCH started")
        if c.at == "capital" and not leader:resting() then
            say(leader, "|cffffcc00注意|r：你不在休息区，队友不会请求进城")
        end
        say(leader, c.title .. " 就绪：" .. (c.hint or ""))
    else
        say(leader, c.title .. " 完成。")
    end
    say(leader, "记录：" .. (proftest.fileName or "（日志文件打不开）"))
    status(leader, "ready", c.id)
    monitor(leader, c)
end

local function spawnBots(leader, c)
    for _, w in ipairs(c.bots) do addbot(leader, w.class, w.role or "auto", levelOf(w, leader)) end
    waitfor(function()
        local bots = botsOf(leader)
        if #bots < #c.bots then return false end
        for _, b in ipairs(bots) do
            local ai = b:ai()
            if not ai or not ai.initialized then return false end
        end
        return true
    end, function(ok)
        if not ok then return fail(leader, "队友 30 秒内没有就绪") end
        guarded(leader, finish, leader, c)
    end, 30000, 500)
end

function proftest.load(leader, id)
    local c = find(id)
    if not c then return fail(leader, "没有这个专业测试：" .. tostring(id)) end
    if proftest.running then return say(leader, "上一个测试还在准备中，请稍等") end
    if leader:incombat() then return fail(leader, "战斗中不能加载") end
    proftest.running = c.id
    stopMonitor()
    openLog(c.id)
    log("CASE " .. c.id .. "  " .. c.title)
    if c.hint then log("HINT " .. c.hint) end
    status(leader, "loading", c.id)
    say(leader, "准备 " .. c.title .. " ...")

    for _, b in ipairs(botsOf(leader)) do b:removebot() end
    waitfor(function() return #botsOf(leader) == 0 end, function(ok)
        if not ok then return fail(leader, "旧队友没有移除") end
        guarded(leader, function()
            leader:revive()
            leader:combatstop()
            local at = c.at == "capital" and CAPITAL[HORDE_RACES[leader:race()] and "horde" or "alliance"] or nil
            if not at then
                after(500, function() guarded(leader, spawnBots, leader, c) end)
                return
            end
            if leader:pos().map ~= at.map then leader:tp(at.map, at.x, at.y, at.z, at.o) else leader:tp(at.x, at.y, at.z, at.o) end
            waitfor(function() return leader:pos().map == at.map and not leader:teleporting() end, function(ok2)
                if not ok2 then return fail(leader, "传送超时") end
                after(1500, function() guarded(leader, spawnBots, leader, c) end)
            end, 30000, 250)
        end)
    end, 10000, 250)
end

function proftest.stop(leader)
    stopMonitor()
    log("END stopped")
    for _, b in ipairs(botsOf(leader)) do b:removebot() end
    proftest.current = nil
    proftest.running = nil
    status(leader, "stopped")
    say(leader, "专业测试已结束")
end

function proftest.command(leader, args)
    args = (args or ""):match("^%s*(.-)%s*$")
    if args == "" or args == "list" then
        for _, c in ipairs(proftest.list) do
            leader:tell(string.format("PBPROF;L;%s;%s;%s", c.id, c.title, c.watch and "watch" or "auto"))
            leader:tell(string.format("PBPROF;H;%s;%s", c.id, ((c.hint or ""):gsub(";", ","))))
        end
        leader:tell("PBPROF;E;" .. (proftest.current or ""))
        if args == "" then
            for _, c in ipairs(proftest.list) do say(leader, ".scenario prof " .. c.id .. "  " .. c.title) end
            say(leader, ".scenario prof reset | stop | note [说明]    窗口：/pbp")
        end
    elseif args == "stop" then
        proftest.stop(leader)
    elseif args == "reset" then
        if not proftest.current then return say(leader, "还没有加载过专业测试") end
        proftest.load(leader, proftest.current)
    elseif args:match("^note") then
        if not proftest.current then return say(leader, "现在没有专业测试在运行") end
        local text = args:match("^note%s+(.+)$") or ""
        log("NOTE " .. text)
        local ours = filter(botsOf(leader), function(b) return proftest.state[b:name()] ~= nil end)
        snapshot("NOTE", ours)
        say(leader, "已记一笔" .. (text ~= "" and ("：" .. text) or ""))
    else
        proftest.load(leader, args)
    end
end
