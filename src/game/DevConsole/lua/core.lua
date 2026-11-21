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

-- The first online player with a client (not a party bot, not headless), handy when only one person plays.
function me()
    for _, p in ipairs(players()) do
        if not p:isbot() and not p:headless() then return p end
    end
end

-- The first character logged in with login(name), without a client.
function puppet()
    for _, p in ipairs(players()) do
        if p:headless() then return p end
    end
end

-- Ranged pull spells a player may know, tried in this order.
PULL_SPELLS = { "Auto Shot", "Shoot Bow", "Shoot Gun", "Shoot Crossbow", "Throw", "Shoot" }

-- pull(u, target): open on a mob like a player would. Uses the first ranged pull spell that
-- starts (needs a ranged weapon / wand), otherwise runs in and auto attacks.
-- Returns the spell name used, or "melee".
function pull(u, target)
    local known = {}
    for _, s in ipairs(u:spells()) do known[s.name] = true end
    for _, name in ipairs(PULL_SPELLS) do
        if known[name] and u:use(name, target) then return name end
    end
    if not u:attack(target) then error("cannot attack " .. target:name()) end
    return "melee"
end

-- Enemies in combat with anyone of the player's group (within radius, default 60).
function engaged(leader, radius)
    leader = leader or me()
    local members = leader:group()
    local isMember = {}
    for _, m in ipairs(members) do isMember[m:guid()] = true end
    local out = {}
    -- 'alive', not 'enemy': a neutral (yellow) creature stays neutral while it fights us
    -- (strat-20261114-183556 recorded no enemies for a Ghostly Citizen the player attacked).
    for _, e in ipairs(leader:nearby(radius or 60, "alive")) do
        if e:incombat() and not isMember[e:guid()] and not leader:friendly(e) then
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

-- Every heal rank of a healer as text rows (u:healranks, the bots' own estimate), e.g.
-- healtable(player('Fa'), player('Tank')). heal = average with crits (x1.5 on the direct part),
-- mana = real cost (talents), busy = max(cast, 1.5) for direct heals, the duration for HoTs.
-- hps = heal / busy, hpm = heal / mana.
function healtable(u, target)
    local r = u:healranks(target)
    local crit = math.min(100, math.max(0, r.crit)) / 100
    local rows = { string.format("+heal %d  crit %.1f%%  mana %d/%d", r.bonus, r.crit, u:power(), u:maxpower()),
                   string.format("%-20s %3s %6s %6s %5s %5s %6s %5s", "spell", "rk", "direct", "hot", "mana", "cast", "hps", "hpm") }
    for _, s in ipairs(r.ranks) do
        local heal = s.direct * (1 + 0.5 * crit) + s.periodic
        local busy = s.direct > 0 and math.max(1.5, s.cast) or math.max(1.5, s.periodicSec)
        local mana = s.manaMod > 0 and s.manaMod or s.mana
        rows[#rows + 1] = string.format("%-20s %3d %6d %6d %5d %5.1f %6.1f %5.2f", s.name, s.rank,
            math.floor(s.direct), math.floor(s.periodic), mana, s.cast, heal / busy, mana > 0 and heal / mana or 0)
    end
    return table.concat(rows, "\n")
end

-- Copper as "1g 2s 3c".
function gold(c)
    c = math.floor(c or 0)
    local g, s = c // 10000, (c // 100) % 100
    local out = {}
    if g > 0 then out[#out + 1] = g .. "g" end
    if s > 0 then out[#out + 1] = s .. "s" end
    if c % 100 > 0 or #out == 0 then out[#out + 1] = (c % 100) .. "c" end
    return table.concat(out, " ")
end

-- A companion's professions as text rows (u:prof), e.g. proftable(player('Fa')).
-- value/max, today = offline points today, next = price of the next point (crafting),
-- rec = trainer recipes known / at or below the value, miss = recent ones (within 25) not known,
-- art = Artisan rank green (*), trip record state and asks left.
function proftable(u)
    if type(u) == "string" then u = player(u) end
    if not u then return "no such player" end
    local p = u:prof()
    local head = string.format("%s lv%d %s  money %s", u:name(), u:level(), u:class(), gold(p.money))
    if p.companion then
        head = head .. string.format("  %s%s  away %s  tags %s", p.temporary and "temporary " or "",
            p.decided and "decided" or "NOT decided",
            p.away and string.format("%.1fh", p.away / 3600) or "never",
            #p.tags > 0 and table.concat(p.tags, ",") or "-")
    else
        head = head .. "  (not a companion)"
    end
    local rows = { head }
    for _, s in ipairs(p.list) do
        local extra = {}
        if s.nextcost then extra[#extra + 1] = "next " .. gold(s.nextcost) end
        if s.tool and s.tool > 0 then extra[#extra + 1] = "tool " .. s.tool .. (s.hastool and " ok" or " MISSING") end
        if s.artisan then
            local a = s.artisan
            extra[#extra + 1] = "art " .. (a.green and "*" or "-") .. (a.state and (" " .. a.state .. "/" .. a.asks) or "")
        end
        rows[#rows + 1] = string.format("  %-15s %-9s %3d/%3d today %2d rec %3d/%3d miss %2d  %s", s.name, s.kind,
            s.value, s.max, s.today, s.recipes.known, s.recipes.below, s.recipes.recentmissing, table.concat(extra, "  "))
    end
    return table.concat(rows, "\n")
end

-- proftable of every companion in the leader's group (default me()).
function profs(leader)
    leader = leader or me()
    if not leader then return "no player online" end
    local out = {}
    for _, m in ipairs(leader:group()) do
        if m:isbot() and m:prof().companion then out[#out + 1] = proftable(m) end
    end
    return #out > 0 and table.concat(out, "\n") or "no companion in the group"
end

-- u:offline(hours) as one text line, then the table after it.
function offlinetest(u, hours)
    if type(u) == "string" then u = player(u) end
    if not u then return "no such player" end
    local money = u:money()
    local r = u:offline(hours)
    local gains = map(r.gains, function(g) return g.skill .. " +" .. g.points end)
    return string.format("away %.1fh: %s  spent %s (money %s -> %s)%s\n%s", hours,
        #gains > 0 and table.concat(gains, ", ") or "no gain", gold(r.spent), gold(money), gold(u:money()),
        r.short and "  SHORT OF MONEY" or "", proftable(u))
end
