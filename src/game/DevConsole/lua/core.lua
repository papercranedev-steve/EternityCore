-- Core helpers: timers, polling, quick inspection.
-- Every function here runs in the world thread; one call must stay short (instruction/time limit).

dev = dev or {}
dev.errors = dev.errors or {}

local function pushError(where, err)
    local e = { t = now(), where = where, err = tostring(err) }
    table.insert(dev.errors, e)
    if #dev.errors > 50 then table.remove(dev.errors, 1) end
    log(where .. ": " .. tostring(err))
end
dev.pushError = pushError

---------------------------------------------------------------------------
-- timers
---------------------------------------------------------------------------

local timers = {}
local nextId = 0

-- after(ms, fn) runs fn once; every(ms, fn) repeats until fn returns false or cancel(id).
function after(ms, fn)
    nextId = nextId + 1
    timers[nextId] = { due = now() + ms, fn = fn }
    return nextId
end

function every(ms, fn)
    nextId = nextId + 1
    timers[nextId] = { due = now() + ms, fn = fn, interval = ms }
    return nextId
end

function cancel(id)
    if id then timers[id] = nil end
end

function dev.timerCount()
    local n = 0
    for _ in pairs(timers) do n = n + 1 end
    return n
end

-- Called by the server every world tick.
function __tick()
    if next(timers) == nil then return end
    local t = now()
    local due = {}
    for id, tm in pairs(timers) do
        if t >= tm.due then due[#due + 1] = id end
    end
    table.sort(due)
    for _, id in ipairs(due) do
        local tm = timers[id]
        if tm then
            if tm.interval then tm.due = t + tm.interval else timers[id] = nil end
            local ok, res = pcall(tm.fn)
            if not ok then
                pushError("timer " .. id, res)
                timers[id] = nil
            elseif tm.interval and res == false then
                timers[id] = nil
            end
        end
    end
end

-- waitfor(cond, fn, timeoutMs, stepMs): polls cond(); calls fn(true) when it holds, fn(false) on timeout.
function waitfor(cond, fn, timeoutMs, stepMs)
    local deadline = now() + (timeoutMs or 10000)
    return every(stepMs or 250, function()
        local ok, res = pcall(cond)
        if ok and res then fn(true) return false end
        if now() >= deadline then fn(false) return false end
    end)
end

---------------------------------------------------------------------------
-- inspection helpers (for the console)
---------------------------------------------------------------------------

function map(list, fn)
    local out = {}
    for i, v in ipairs(list) do out[i] = fn(v) end
    return out
end

function filter(list, fn)
    local out = {}
    for _, v in ipairs(list) do if fn(v) then out[#out + 1] = v end end
    return out
end

-- The first online non-bot player, handy when only one person plays.
function me()
    for _, p in ipairs(players()) do
        if not p:isbot() then return p end
    end
end

-- Enemies in combat with anyone of the player's group (within radius, default 60).
function engaged(leader, radius)
    leader = leader or me()
    local members = leader:group()
    local isMember = {}
    for _, m in ipairs(members) do isMember[m:guid()] = true end
    local out = {}
    for _, e in ipairs(leader:nearby(radius or 60, "enemy")) do
        if e:incombat() then
            local hit = false
            for _, row in ipairs(e:threat()) do
                if isMember[row.unit:guid()] then hit = true break end
            end
            local v = e:victim()
            if hit or (v and isMember[v:guid()]) then out[#out + 1] = e end
        end
    end
    return out
end

-- Party and engaged enemies with threat tables. snap() from the console is the quickest overview.
function snap(leader)
    leader = leader or me()
    if not leader then return "no player online" end
    local out = { t = now(), party = {}, enemies = {} }
    for _, m in ipairs(leader:group()) do
        local i = m:info()
        i.casting = m:casting()
        local ai = m:ai()
        if ai then i.ai = { role = ai.role, lastDecision = ai.lastDecision } end
        out.party[#out.party + 1] = i
    end
    for _, e in ipairs(engaged(leader)) do
        local i = e:info()
        i.casting = e:casting()
        i.threat = map(e:threat(), function(r) return { name = r.unit:name(), threat = r.threat, pct = r.pct } end)
        out.enemies[#out.enemies + 1] = i
    end
    return out
end

-- Short aura list: { "Name(id) 12s x2", ... }
function auralist(u)
    return map(u:auras(), function(a)
        local s = a.name .. "(" .. a.spell .. ")"
        if a.left and a.left > 0 then s = s .. " " .. math.floor(a.left / 1000) .. "s" end
        if a.stacks and a.stacks > 1 then s = s .. " x" .. a.stacks end
        return s
    end)
end

-- Nearby units as compact rows, e.g. around(me(), 40, "enemy")
function around(u, radius, what)
    u = u or me()
    return map(u:nearby(radius or 30, what or "all"), function(n)
        return { name = n:name(), guid = n:guid(), entry = n:entry(), level = n:level(), dist = u:dist(n),
                 hp = n:hppct(), alive = n:alive(), combat = n:incombat() }
    end)
end
