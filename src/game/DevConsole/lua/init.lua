-- Dev console entry point. Loaded by ".lua reload" and on the first .lua / .scenario call.
-- SCRIPT_DIR is set by the server (DevConsole.ScriptDir).

local function load(name)
    local chunk, err = loadfile(SCRIPT_DIR .. "/" .. name)
    if not chunk then error(err, 0) end
    chunk()
end

load("core.lua")
load("review.lua")
load("recorder.lua")
load("scenarios.lua")
load("bosses.lua")      -- boss tests (kind = "boss"), added to scenario.list
load("recview.lua")
-- test scenarios by kind (tests/<kind>/); combat scenarios are still scenarios.lua
load("tests/life/life.lua")
load("tests/prof/prof.lua")
load("tests/affinity/affinity.lua")
