/*
 * Global functions for dev console Lua scripts.
 *
 *   players()                      all players in world (bots included)
 *   player(name)                   Unit or nil
 *   unit(guid [, near])            Unit or nil; guid is the "0x..." string from u:guid()
 *   cmd(text)                      run a console GM command, returns its output
 *   spell(id)                      spell data table
 *   now()                          server ms clock
 *   log(text)                      write to Server.log
 *   json(value)                    encode as JSON string
 *   jsondecode(text)               decode JSON text (objects / arrays as tables, null = nil), or nil, error
 *   fightload(inputJson)           re-run a traced FightLoad simulation step by step (JSON text)
 *   premades([classId])            premade gear/spec templates
 *   addbot(leader, class, role, level)  temporary party bot next to the leader
 *   account(player)                account name of a player
 *   login(name)                    log a character of the scenario account in without a client (async)
 *   logout(unit)                   log a headless character out (async)
 *   combatlog.enable(bool) / track({units}) / drain() / dropped()
 */

#include "DevConsole/DevConsole.h"
#include "DevConsole/LuaUnit.h"
#include "DevConsole/LuaJson.h"
#include "DevConsole/CombatEvents.h"
#include "DevConsole/HeadlessPlayerAI.h"
#include "lua.h"
#include "lauxlib.h"

#include "Player.h"
#include "Unit.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "Chat.h"
#include "AccountMgr.h"
#include "World.h"
#include "Timer.h"
#include "PlayerBotMgr.h"
#include "PartyBotAI.h"
#include "Commander/Trace/FightLoadJson.h"
#include "Utilities/Random.h"
#include <nlohmann/json.hpp>

#include <cstring>

uint8 SelectRandomRaceForClass(uint8 playerClass, Team playerTeam); // PlayerBotMgr.cpp

namespace DevConsole
{
    bool ParseRole(char const* s, CombatBotRoles& role);   // LuaUnit.cpp
    char const* RoleName(CombatBotRoles role);

    static int W_players(lua_State* L)
    {
        lua_newtable(L);
        lua_Integer i = 0;
        for (auto const& itr : sObjectAccessor.GetPlayers())
        {
            Player* p = itr.second;
            if (!p || !p->IsInWorld())
                continue;
            LuaUnit_Push(L, p);
            lua_rawseti(L, -2, ++i);
        }
        return 1;
    }

    static int W_player(lua_State* L)
    {
        std::string name = luaL_checkstring(L, 1);
        if (!normalizePlayerName(name))
        {
            lua_pushnil(L);
            return 1;
        }
        Player* p = ObjectAccessor::FindPlayerByName(name.c_str());
        LuaUnit_Push(L, (p && p->IsInWorld()) ? p : nullptr);
        return 1;
    }

    static int W_unit(lua_State* L)
    {
        ObjectGuid guid = GuidFromString(luaL_checkstring(L, 1));
        if (guid.IsPlayer())
        {
            Player* p = ObjectAccessor::FindPlayer(guid);
            LuaUnit_Push(L, (p && p->IsInWorld()) ? p : nullptr);
            return 1;
        }
        Unit* nearUnit = LuaUnit_Opt(L, 2);
        if (nearUnit)
        {
            LuaUnit_Push(L, nearUnit->GetMap()->GetUnit(guid));
            return 1;
        }
        // search the maps that have players on them
        for (auto const& itr : sObjectAccessor.GetPlayers())
        {
            Player* p = itr.second;
            if (!p || !p->IsInWorld())
                continue;
            if (Unit* u = p->GetMap()->GetUnit(guid))
            {
                LuaUnit_Push(L, u);
                return 1;
            }
        }
        lua_pushnil(L);
        return 1;
    }

    static void CapturePrint(void* arg, char const* text)
    {
        static_cast<std::string*>(arg)->append(text);
    }

    static int W_cmd(lua_State* L)
    {
        char const* text = luaL_checkstring(L, 1);
        if (!*text)
            return luaL_error(L, "empty command");
        std::string out;
        CliHandler handler(0, SEC_CONSOLE, &out, &CapturePrint);
        handler.ParseCommands(text);
        lua_pushstring(L, out.c_str());
        return 1;
    }

    static int W_spell(lua_State* L)
    {
        SpellEntry const* e = sSpellMgr.GetSpellEntry(uint32(luaL_checkinteger(L, 1)));
        if (!e)
        {
            lua_pushnil(L);
            return 1;
        }
        lua_createtable(L, 0, 16);
        auto set = [L](char const* k, lua_Integer v) { lua_pushinteger(L, v); lua_setfield(L, -2, k); };
        set("id", e->Id);
        lua_pushstring(L, e->SpellName[0].c_str()); lua_setfield(L, -2, "name");
        lua_pushstring(L, e->Rank[0].c_str()); lua_setfield(L, -2, "rank");
        set("school", e->School);
        set("mechanic", e->Mechanic);
        set("dispel", e->Dispel);
        set("category", e->Category);
        set("castTimeIndex", e->CastingTimeIndex);
        set("durationIndex", e->DurationIndex);
        set("rangeIndex", e->rangeIndex);
        set("recovery", e->RecoveryTime);
        set("categoryRecovery", e->CategoryRecoveryTime);
        set("powerType", e->powerType);
        set("manaCost", e->manaCost);
        set("attributes", e->Attributes);
        lua_newtable(L);
        lua_Integer n = 0;
        for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            if (!e->Effect[i])
                continue;
            lua_createtable(L, 0, 6);
            set("effect", e->Effect[i]);
            set("aura", e->EffectApplyAuraName[i]);
            set("base", e->EffectBasePoints[i] + 1);
            set("mechanic", e->EffectMechanic[i]);
            set("targetA", e->EffectImplicitTargetA[i]);
            set("radiusIndex", e->EffectRadiusIndex[i]);
            lua_rawseti(L, -2, ++n);
        }
        lua_setfield(L, -2, "effects");
        return 1;
    }

    static int W_now(lua_State* L)
    {
        lua_pushinteger(L, WorldTimer::getMSTime());
        return 1;
    }

    static int W_log(lua_State* L)
    {
        sDevConsole.WriteLog(luaL_checkstring(L, 1));
        return 0;
    }

    static int W_json(lua_State* L)
    {
        std::string out;
        LuaToJson(L, 1, out);
        lua_pushlstring(L, out.data(), out.size());
        return 1;
    }

    // JSON value -> Lua: objects are tables with string keys, arrays 1-based tables, null nil.
    static void PushJson(lua_State* L, nlohmann::json const& j, int depth)
    {
        if (depth > 32 || !lua_checkstack(L, 3))
        {
            lua_pushnil(L);
            return;
        }
        switch (j.type())
        {
            case nlohmann::json::value_t::object:
                lua_createtable(L, 0, int(j.size()));
                for (auto it = j.begin(); it != j.end(); ++it)
                {
                    PushJson(L, it.value(), depth + 1);
                    lua_setfield(L, -2, it.key().c_str());
                }
                return;
            case nlohmann::json::value_t::array:
            {
                lua_createtable(L, int(j.size()), 0);
                lua_Integer i = 0;
                for (nlohmann::json const& v : j)
                {
                    PushJson(L, v, depth + 1);
                    lua_rawseti(L, -2, ++i);
                }
                return;
            }
            case nlohmann::json::value_t::string:
            {
                std::string const& s = j.get_ref<std::string const&>();
                lua_pushlstring(L, s.data(), s.size());
                return;
            }
            case nlohmann::json::value_t::boolean:
                lua_pushboolean(L, j.get<bool>());
                return;
            case nlohmann::json::value_t::number_integer:
                lua_pushinteger(L, lua_Integer(j.get<int64_t>()));
                return;
            case nlohmann::json::value_t::number_unsigned:
                lua_pushinteger(L, lua_Integer(j.get<uint64_t>()));
                return;
            case nlohmann::json::value_t::number_float:
                lua_pushnumber(L, j.get<double>());
                return;
            default:
                lua_pushnil(L);
                return;
        }
    }

    // jsondecode(text): Lua value, or nil and an error.
    static int W_jsondecode(lua_State* L)
    {
        size_t len = 0;
        char const* text = luaL_checklstring(L, 1, &len);
        nlohmann::json const j = nlohmann::json::parse(text, text + len, nullptr, false);
        if (j.is_discarded())
        {
            lua_pushnil(L);
            lua_pushstring(L, "invalid JSON");
            return 2;
        }
        PushJson(L, j, 0);
        return 1;
    }

    // fightload(inputJson): re-runs a FightLoad simulation recorded by the calculation trace
    // (the `input` of a `trace` line of kind "fight") with the current code, step by step.
    // Returns JSON text {"result":{...}, "steps":[{t, in, heal, tank, mana, hp=[...]}]}, or
    // nil and an error.
    static int W_fightload(lua_State* L)
    {
        size_t len = 0;
        char const* text = luaL_checklstring(L, 1, &len);
        Commander::FightLoad::Input input;
        std::string error;
        if (!Commander::FightLoadJson::ParseInput(std::string(text, len), input, error))
        {
            lua_pushnil(L);
            lua_pushstring(L, error.c_str());
            return 2;
        }
        std::vector<Commander::FightLoad::Step> steps;
        Commander::FightLoad::Result const r = Commander::FightLoad::Run(input, &steps);
        std::string const out = "{\"result\":" + Commander::FightLoadJson::ResultJson(r) +
            ",\"steps\":" + Commander::FightLoadJson::StepsJson(steps) + "}";
        lua_pushlstring(L, out.data(), out.size());
        return 1;
    }

    static int W_premades(lua_State* L)
    {
        lua_Integer cls = luaL_optinteger(L, 1, 0);
        lua_newtable(L);
        lua_Integer i = 0;
        auto add = [&](char const* kind, uint32 entry, uint8 level, uint8 reqClass, CombatBotRoles role, std::string const& name)
        {
            if (cls && reqClass != cls)
                return;
            lua_createtable(L, 0, 6);
            lua_pushstring(L, kind); lua_setfield(L, -2, "kind");
            lua_pushinteger(L, entry); lua_setfield(L, -2, "entry");
            lua_pushinteger(L, level); lua_setfield(L, -2, "level");
            lua_pushinteger(L, reqClass); lua_setfield(L, -2, "class");
            lua_pushstring(L, RoleName(role)); lua_setfield(L, -2, "role");
            lua_pushstring(L, name.c_str()); lua_setfield(L, -2, "name");
            lua_rawseti(L, -2, ++i);
        };
        for (auto const& itr : sObjectMgr.GetPlayerPremadeGearTemplates())
            add("gear", itr.second.entry, itr.second.level, itr.second.requiredClass, itr.second.role, itr.second.name);
        for (auto const& itr : sObjectMgr.GetPlayerPremadeSpecTemplates())
            add("spec", itr.second.entry, itr.second.level, itr.second.requiredClass, itr.second.role, itr.second.name);
        return 1;
    }

    static uint8 ParseClass(lua_State* L, int idx)
    {
        if (lua_isinteger(L, idx))
            return uint8(lua_tointeger(L, idx));
        char const* s = luaL_checkstring(L, idx);
        static struct { char const* name; uint8 id; } const classes[] = {
            { "warrior", CLASS_WARRIOR }, { "paladin", CLASS_PALADIN }, { "hunter", CLASS_HUNTER },
            { "rogue", CLASS_ROGUE }, { "priest", CLASS_PRIEST }, { "shaman", CLASS_SHAMAN },
            { "mage", CLASS_MAGE }, { "warlock", CLASS_WARLOCK }, { "druid", CLASS_DRUID },
        };
        for (auto const& c : classes)
            if (!strcmp(s, c.name))
                return c.id;
        return 0;
    }

    // addbot(leader, class, role, level) -> true; the bot logs in over the next ticks and joins the group
    static int W_addbot(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        Player* leader = u->ToPlayer();
        if (!leader)
            return luaL_error(L, "leader must be a player");
        uint8 botClass = ParseClass(L, 2);
        if (!botClass)
            return luaL_error(L, "unknown class");
        CombatBotRoles role = ROLE_INVALID;
        if (!lua_isnoneornil(L, 3) && !ParseRole(luaL_checkstring(L, 3), role))
            return luaL_error(L, "role must be tank, healer, meleedps, rangedps or auto");
        uint32 level = uint32(luaL_optinteger(L, 4, leader->GetLevel()));

        if (leader->GetGroup() && leader->GetGroup()->IsFull())
            return luaL_error(L, "group is full");

        uint8 botRace = SelectRandomRaceForClass(botClass, leader->GetTeam());
        if (!botRace)
            return luaL_error(L, "class not available for this faction");

        float x, y, z;
        leader->GetNearPoint(leader, x, y, z, 0, 3.0f, frand(0.0f, 6.28f));
        PartyBotAI* ai = new PartyBotAI(leader, nullptr, role, botRace, botClass, uint8(level), leader->GetMapId(),
            leader->GetMap()->GetInstanceId(), x, y, z, leader->GetOrientation());
        if (!sPlayerBotMgr.AddBot(ai))
        {
            delete ai;
            return luaL_error(L, "AddBot failed");
        }
        lua_pushboolean(L, 1);
        return 1;
    }

    // login(name) -> true; logs a character of the scenario account in without a client.
    // Asynchronous: poll player(name) until it returns the unit (about one PlayerBot.UpdateMs).
    static int W_login(lua_State* L)
    {
        std::string name = luaL_checkstring(L, 1);
        if (!normalizePlayerName(name))
            return luaL_error(L, "bad character name");
        ObjectGuid guid = sObjectMgr.GetPlayerGuidByName(name);
        if (guid.IsEmpty())
            return luaL_error(L, "no character %s", name.c_str());

        uint32 accountId = sObjectMgr.GetPlayerAccountIdByGUID(guid);
        std::string accountName;
        if (!accountId || !sAccountMgr.GetName(accountId, accountName) || !sDevConsole.IsScenarioAccount(accountName))
            return luaL_error(L, "only characters of account %s can log in headless", sDevConsole.GetScenarioAccount().c_str());

        if (sObjectAccessor.FindPlayerNotInWorld(guid))
            return luaL_error(L, "%s is already online", name.c_str());
        // the entry of an earlier headless login is removed on the next PlayerBotMgr update
        if (sPlayerBotMgr.IsPermanentBot(guid.GetCounter()))
            return luaL_error(L, "%s is still logging out, try again in a few seconds", name.c_str());
        if (sWorld.FindSession(accountId))
            return luaL_error(L, "account %s is in use (a client is logged in)", accountName.c_str());

        HeadlessPlayerAI* ai = new HeadlessPlayerAI();
        if (!sPlayerBotMgr.AddBot(guid.GetCounter(), false, ai, false))
        {
            delete ai;
            return luaL_error(L, "AddBot failed");
        }
        lua_pushboolean(L, 1);
        return 1;
    }

    // logout(unit) -> true; only for headless characters. Done on the next PlayerBotMgr update.
    static int W_logout(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        HeadlessPlayerAI* ai = GetHeadlessAI(u->ToPlayer());
        if (!ai || !ai->botEntry)
            return luaL_error(L, "%s is not logged in headless", u->GetName());
        ai->botEntry->requestRemoval = true;
        lua_pushboolean(L, 1);
        return 1;
    }

    static int W_account(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        Player* p = u->ToPlayer();
        std::string name;
        if (p && p->GetSession())
            name = p->GetSession()->GetUsername();
        lua_pushstring(L, name.c_str());
        return 1;
    }

    // ---- combatlog ----

    static int C_enable(lua_State* L)
    {
        CombatEvents::SetActive(lua_toboolean(L, 1));
        return 0;
    }

    static int C_track(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TTABLE);
        std::vector<ObjectGuid> guids;
        lua_Integer n = luaL_len(L, 1);
        for (lua_Integer i = 1; i <= n; ++i)
        {
            lua_geti(L, 1, i);
            if (LuaUnitRef const* ref = LuaUnit_TestRef(L, -1))
                guids.push_back(ref->guid);
            else if (lua_isstring(L, -1))
                guids.push_back(GuidFromString(lua_tostring(L, -1)));
            lua_pop(L, 1);
        }
        CombatEvents::SetTracked(guids);
        return 0;
    }

    static int C_drain(lua_State* L)
    {
        std::vector<CombatEvent> events = CombatEvents::Drain();
        lua_createtable(L, int(events.size()), 0);
        lua_Integer i = 0;
        for (CombatEvent const& e : events)
        {
            lua_createtable(L, 0, 9);
            lua_pushinteger(L, e.timeMs); lua_setfield(L, -2, "t");
            lua_pushstring(L, CombatEventTypeName(e.type)); lua_setfield(L, -2, "ev");
            if (!e.source.IsEmpty()) { lua_pushstring(L, GuidToString(e.source).c_str()); lua_setfield(L, -2, "src"); }
            if (!e.target.IsEmpty()) { lua_pushstring(L, GuidToString(e.target).c_str()); lua_setfield(L, -2, "dst"); }
            if (e.spellId) { lua_pushinteger(L, e.spellId); lua_setfield(L, -2, "spell"); }
            if (e.amount) { lua_pushinteger(L, e.amount); lua_setfield(L, -2, "amount"); }
            if (e.extra) { lua_pushinteger(L, e.extra); lua_setfield(L, -2, "extra"); }
            if (e.school) { lua_pushinteger(L, e.school); lua_setfield(L, -2, "school"); }
            if (e.critical) { lua_pushboolean(L, 1); lua_setfield(L, -2, "crit"); }
            if (e.type == CombatEventType::CastFail)
                if (char const* reason = CastFailReasonName(e.extra)) { lua_pushstring(L, reason); lua_setfield(L, -2, "reason"); }
            lua_rawseti(L, -2, ++i);
        }
        return 1;
    }

    static int C_dropped(lua_State* L)
    {
        lua_pushinteger(L, CombatEvents::DroppedCount());
        return 1;
    }

    void LuaWorld_Register(lua_State* L)
    {
        static luaL_Reg const globals[] = {
            { "players", W_players }, { "player", W_player }, { "unit", W_unit }, { "cmd", W_cmd },
            { "spell", W_spell }, { "now", W_now }, { "log", W_log }, { "json", W_json }, { "jsondecode", W_jsondecode },
            { "fightload", W_fightload },
            { "premades", W_premades }, { "addbot", W_addbot }, { "account", W_account },
        { "login", W_login }, { "logout", W_logout },
            { nullptr, nullptr }
        };
        lua_pushglobaltable(L);
        luaL_setfuncs(L, globals, 0);
        lua_pop(L, 1);

        static luaL_Reg const combatlog[] = {
            { "enable", C_enable }, { "track", C_track }, { "drain", C_drain }, { "dropped", C_dropped },
            { nullptr, nullptr }
        };
        luaL_newlib(L, combatlog);
        lua_setglobal(L, "combatlog");
    }
}
