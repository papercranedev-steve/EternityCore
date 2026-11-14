/*
 * Unit userdata for dev console Lua scripts. Method list: see LuaUnit_Register below and
 * .kiro/docs (dev console doc). All calls run in the world thread while maps are idle.
 */

#include "DevConsole/LuaUnit.h"
#include "DevConsole/LuaJson.h"
#include "DevConsole/DevConsole.h"
#include "lua.h"
#include "lauxlib.h"

#include "Unit.h"
#include "Player.h"
#include "Creature.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "SpellAuras.h"
#include "Spell.h"
#include "ThreatManager.h"
#include "Group.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "Chat.h"
#include "World.h"
#include "WorldSession.h"
#include "Timer.h"
#include "Bag.h"
#include "PathFinder.h"
#include "PlayerBotMgr.h"
#include "PartyBotAI.h"
#include "Commander/BotDirectives.h"
#include "Commander/Commands/BotSpeech.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Commands/PlayerChat.h"
#include "Commander/Coordination/Doctrine/TacticalDoctrine.h"
#include "Commander/Coordination/Doctrine/Battlefield.h"
#include "Commander/Perception/Spatial/AggroRange.h"
#include "DevConsole/HeadlessPlayerAI.h"
#include "DevConsole/CombatEvents.h"
#include "Packets/Spell.h"
#include "MotionMaster.h"
#include "MoveSpline.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <list>

namespace DevConsole
{
    static char const* UNIT_MT = "DevConsole.Unit";

    std::string GuidToString(ObjectGuid guid)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "0x%016" PRIX64, uint64(guid.GetRawValue()));
        return buf;
    }

    ObjectGuid GuidFromString(char const* s)
    {
        if (!s)
            return ObjectGuid();
        return ObjectGuid(uint64(strtoull(s, nullptr, 0)));
    }

    Unit* LuaUnit_Resolve(LuaUnitRef const& ref)
    {
        if (ref.guid.IsEmpty())
            return nullptr;
        if (ref.guid.IsPlayer())
        {
            Player* p = ObjectAccessor::FindPlayer(ref.guid);
            return (p && p->IsInWorld()) ? p : nullptr;
        }
        Map* map = sMapMgr.FindMap(ref.mapId, ref.instanceId);
        if (!map)
            return nullptr;
        Unit* u = map->GetUnit(ref.guid);
        return (u && u->IsInWorld()) ? u : nullptr;
    }

    LuaUnitRef const* LuaUnit_TestRef(lua_State* L, int idx)
    {
        return static_cast<LuaUnitRef const*>(luaL_testudata(L, idx, UNIT_MT));
    }

    void LuaUnit_Push(lua_State* L, Unit* unit)
    {
        if (!unit)
        {
            lua_pushnil(L);
            return;
        }
        void* mem = lua_newuserdatauv(L, sizeof(LuaUnitRef), 0);
        LuaUnitRef* ref = new (mem) LuaUnitRef();
        ref->guid = unit->GetObjectGuid();
        ref->mapId = unit->GetMapId();
        ref->instanceId = unit->GetInstanceId();
        luaL_setmetatable(L, UNIT_MT);
    }

    Unit* LuaUnit_Check(lua_State* L, int idx)
    {
        LuaUnitRef const* ref = static_cast<LuaUnitRef const*>(luaL_checkudata(L, idx, UNIT_MT));
        Unit* u = LuaUnit_Resolve(*ref);
        if (!u)
            luaL_error(L, "unit %s is gone", GuidToString(ref->guid).c_str());
        return u;
    }

    Unit* LuaUnit_Opt(lua_State* L, int idx)
    {
        if (lua_isnoneornil(L, idx))
            return nullptr;
        return LuaUnit_Check(L, idx);
    }

    void LuaUnit_RefToJson(LuaUnitRef const& ref, std::string& out)
    {
        std::string guid = GuidToString(ref.guid);
        out += "{\"unit\":";
        JsonEscape(guid.data(), guid.size(), out);
        if (Unit* u = LuaUnit_Resolve(ref))
        {
            out += ",\"name\":";
            std::string name = u->GetName();
            JsonEscape(name.data(), name.size(), out);
        }
        else
            out += ",\"gone\":true";
        out += '}';
    }

    // ---------------------------------------------------------------------------------------
    // helpers
    // ---------------------------------------------------------------------------------------

    static Player* CheckPlayer(lua_State* L, int idx)
    {
        Unit* u = LuaUnit_Check(L, idx);
        Player* p = u->ToPlayer();
        if (!p)
            luaL_error(L, "%s is not a player", u->GetName());
        return p;
    }

    static Creature* CheckCreature(lua_State* L, int idx)
    {
        Unit* u = LuaUnit_Check(L, idx);
        Creature* c = u->ToCreature();
        if (!c)
            luaL_error(L, "%s is not a creature", u->GetName());
        return c;
    }

    PartyBotAI* GetPartyBotAI(Unit* u)
    {
        if (!u || !u->IsPlayer())
            return nullptr;
        Player* p = static_cast<Player*>(u);
        return p->AI() ? dynamic_cast<PartyBotAI*>(p->AI()) : nullptr;
    }

    char const* RoleName(CombatBotRoles role)
    {
        switch (role)
        {
            case ROLE_MELEE_DPS: return "meleedps";
            case ROLE_RANGE_DPS: return "rangedps";
            case ROLE_TANK:      return "tank";
            case ROLE_HEALER:    return "healer";
            default:             return "auto";
        }
    }

    bool ParseRole(char const* s, CombatBotRoles& role)
    {
        if (!s) return false;
        if (!strcmp(s, "tank"))     { role = ROLE_TANK; return true; }
        if (!strcmp(s, "healer"))   { role = ROLE_HEALER; return true; }
        if (!strcmp(s, "meleedps") || !strcmp(s, "melee")) { role = ROLE_MELEE_DPS; return true; }
        if (!strcmp(s, "rangedps") || !strcmp(s, "ranged") || !strcmp(s, "caster")) { role = ROLE_RANGE_DPS; return true; }
        if (!strcmp(s, "auto"))     { role = ROLE_INVALID; return true; }
        return false;
    }

    static void SetField(lua_State* L, char const* key, lua_Integer v) { lua_pushinteger(L, v); lua_setfield(L, -2, key); }
    static void SetFieldN(lua_State* L, char const* key, double v) { lua_pushnumber(L, v); lua_setfield(L, -2, key); }
    static void SetFieldB(lua_State* L, char const* key, bool v) { lua_pushboolean(L, v); lua_setfield(L, -2, key); }
    static void SetFieldS(lua_State* L, char const* key, char const* v) { lua_pushstring(L, v); lua_setfield(L, -2, key); }
    static void SetFieldU(lua_State* L, char const* key, Unit* u) { LuaUnit_Push(L, u); lua_setfield(L, -2, key); }

    static float Round2(float v) { return float(int(v * 100.0f + (v >= 0 ? 0.5f : -0.5f))) / 100.0f; }

    static void PushCasting(lua_State* L, Unit* u)
    {
        for (uint32 type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
        {
            Spell* s = u->GetCurrentSpell(CurrentSpellTypes(type));
            if (s && s->m_spellInfo)
            {
                lua_createtable(L, 0, 6);
                SetField(L, "spell", s->m_spellInfo->Id);
                SetFieldS(L, "name", s->m_spellInfo->SpellName[0].c_str());
                SetField(L, "castTime", s->GetCastTime());
                SetField(L, "left", s->GetCastedTime());
                SetFieldB(L, "channel", type == CURRENT_CHANNELED_SPELL);
                Unit* t = s->m_targets.getUnitTarget();
                if (t && t->IsInWorld())
                    SetFieldU(L, "target", t);
                return;
            }
        }
        lua_pushnil(L);
    }

    static Unit* SelectionOf(Unit* u)
    {
        ObjectGuid guid = u->IsPlayer() ? static_cast<Player*>(u)->GetSelectionGuid() : u->GetTargetGuid();
        if (guid.IsEmpty())
            return nullptr;
        return u->GetMap()->GetUnit(guid);
    }

    // ---------------------------------------------------------------------------------------
    // read methods
    // ---------------------------------------------------------------------------------------

    static int U_exists(lua_State* L)
    {
        LuaUnitRef const* ref = static_cast<LuaUnitRef const*>(luaL_checkudata(L, 1, UNIT_MT));
        lua_pushboolean(L, LuaUnit_Resolve(*ref) != nullptr);
        return 1;
    }
    static int U_guid(lua_State* L)
    {
        LuaUnitRef const* ref = static_cast<LuaUnitRef const*>(luaL_checkudata(L, 1, UNIT_MT));
        lua_pushstring(L, GuidToString(ref->guid).c_str());
        return 1;
    }
    static int U_name(lua_State* L) { lua_pushstring(L, LuaUnit_Check(L, 1)->GetName()); return 1; }
    static int U_entry(lua_State* L) { lua_pushinteger(L, LuaUnit_Check(L, 1)->GetEntry()); return 1; }
    static int U_level(lua_State* L) { lua_pushinteger(L, LuaUnit_Check(L, 1)->GetLevel()); return 1; }
    static int U_class(lua_State* L) { lua_pushinteger(L, LuaUnit_Check(L, 1)->GetClass()); return 1; }
    static int U_race(lua_State* L) { lua_pushinteger(L, LuaUnit_Check(L, 1)->GetRace()); return 1; }
    static int U_isplayer(lua_State* L) { lua_pushboolean(L, LuaUnit_Check(L, 1)->IsPlayer()); return 1; }
    static int U_isbot(lua_State* L) { lua_pushboolean(L, GetPartyBotAI(LuaUnit_Check(L, 1)) != nullptr); return 1; }
    static int U_headless(lua_State* L) { lua_pushboolean(L, GetHeadlessAI(LuaUnit_Check(L, 1)->ToPlayer()) != nullptr); return 1; }
    static int U_alive(lua_State* L) { lua_pushboolean(L, LuaUnit_Check(L, 1)->IsAlive()); return 1; }
    static int U_incombat(lua_State* L) { lua_pushboolean(L, LuaUnit_Check(L, 1)->IsInCombat()); return 1; }
    static int U_hp(lua_State* L) { lua_pushinteger(L, LuaUnit_Check(L, 1)->GetHealth()); return 1; }
    static int U_maxhp(lua_State* L) { lua_pushinteger(L, LuaUnit_Check(L, 1)->GetMaxHealth()); return 1; }
    static int U_hppct(lua_State* L) { lua_pushnumber(L, Round2(LuaUnit_Check(L, 1)->GetHealthPercent())); return 1; }
    static int U_powertype(lua_State* L) { lua_pushinteger(L, LuaUnit_Check(L, 1)->GetPowerType()); return 1; }
    static int U_power(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        Powers p = Powers(luaL_optinteger(L, 2, u->GetPowerType()));
        lua_pushinteger(L, u->GetPower(p));
        return 1;
    }
    static int U_maxpower(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        Powers p = Powers(luaL_optinteger(L, 2, u->GetPowerType()));
        lua_pushinteger(L, u->GetMaxPower(p));
        return 1;
    }
    static int U_faction(lua_State* L) { lua_pushinteger(L, LuaUnit_Check(L, 1)->GetFactionTemplateId()); return 1; }
    static int U_teleporting(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        lua_pushboolean(L, u->IsPlayer() && static_cast<Player*>(u)->IsBeingTeleported());
        return 1;
    }
    // movement flags (client or AI) or a server spline still running (moveto, chase)
    static int U_moving(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        lua_pushboolean(L, u->IsMoving() || !u->movespline->Finalized());
        return 1;
    }
    static int U_pos(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        lua_createtable(L, 0, 6);
        SetFieldN(L, "x", Round2(u->GetPositionX()));
        SetFieldN(L, "y", Round2(u->GetPositionY()));
        SetFieldN(L, "z", Round2(u->GetPositionZ()));
        SetFieldN(L, "o", Round2(u->GetOrientation()));
        SetField(L, "map", u->GetMapId());
        SetField(L, "instance", u->GetInstanceId());
        return 1;
    }
    static int U_states(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        lua_createtable(L, 0, 8);
        SetFieldB(L, "stunned", u->HasUnitState(UNIT_STATE_STUNNED));
        SetFieldB(L, "rooted", u->HasUnitState(UNIT_STATE_ROOT));
        SetFieldB(L, "confused", u->HasUnitState(UNIT_STATE_CONFUSED));
        SetFieldB(L, "fleeing", u->HasUnitState(UNIT_STATE_FLEEING));
        SetFieldB(L, "feign", u->HasUnitState(UNIT_STATE_FEIGN_DEATH));
        SetFieldB(L, "chase", u->HasUnitState(UNIT_STATE_CHASE));
        SetFieldB(L, "follow", u->HasUnitState(UNIT_STATE_FOLLOW));
        SetFieldB(L, "moving", u->IsMoving());
        if (Creature* c = u->ToCreature())
            SetFieldB(L, "evade", c->IsInEvadeMode());
        SetField(L, "raw", u->GetUnitState());
        return 1;
    }
    static int U_gm(lua_State* L)
    {
        Player* p = CheckPlayer(L, 1);
        if (!lua_isnoneornil(L, 2))
            p->SetGameMaster(lua_toboolean(L, 2), true);
        lua_pushboolean(L, p->IsGameMaster());
        return 1;
    }
    static int U_target(lua_State* L) { LuaUnit_Push(L, SelectionOf(LuaUnit_Check(L, 1))); return 1; }
    static int U_victim(lua_State* L) { LuaUnit_Push(L, LuaUnit_Check(L, 1)->GetVictim()); return 1; }
    static int U_attackers(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        lua_newtable(L);
        lua_Integer i = 0;
        for (Unit* a : u->GetAttackers())
        {
            if (!a || !a->IsInWorld())
                continue;
            LuaUnit_Push(L, a);
            lua_rawseti(L, -2, ++i);
        }
        return 1;
    }
    // Creature threat list, highest first: { {unit=, threat=, pct= (of top)}, ... }
    static int U_threat(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        struct Row { Unit* unit; float threat; };
        std::vector<Row> rows;
        for (HostileReference* ref : u->GetThreatManager().getThreatList())
        {
            Unit* t = ref->getTarget();
            if (t && t->IsInWorld())
                rows.push_back({ t, ref->getThreat() });
        }
        std::sort(rows.begin(), rows.end(), [](Row const& a, Row const& b) { return a.threat > b.threat; });
        float top = rows.empty() ? 0.0f : rows.front().threat;
        lua_createtable(L, int(rows.size()), 0);
        lua_Integer i = 0;
        for (Row const& r : rows)
        {
            lua_createtable(L, 0, 3);
            SetFieldU(L, "unit", r.unit);
            SetFieldN(L, "threat", Round2(r.threat));
            SetFieldN(L, "pct", top > 0.0f ? Round2(r.threat * 100.0f / top) : 0.0);
            lua_rawseti(L, -2, ++i);
        }
        return 1;
    }
    static int U_dist(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        Unit* o = LuaUnit_Check(L, 2);
        if (u->GetMapId() != o->GetMapId())
            lua_pushnil(L);
        else
            lua_pushnumber(L, Round2(u->GetDistance(o)));
        return 1;
    }
    static int U_los(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        Unit* o = LuaUnit_Check(L, 2);
        lua_pushboolean(L, u->GetMap() == o->GetMap() && u->IsWithinLOSInMap(o));
        return 1;
    }
    static int U_hostile(lua_State* L) { Unit* u = LuaUnit_Check(L, 1); lua_pushboolean(L, u->IsHostileTo(LuaUnit_Check(L, 2))); return 1; }
    static int U_friendly(lua_State* L) { Unit* u = LuaUnit_Check(L, 1); lua_pushboolean(L, u->IsFriendlyTo(LuaUnit_Check(L, 2))); return 1; }

    class AnyUnitInRangeCheck
    {
        public:
            AnyUnitInRangeCheck(WorldObject const* obj, float range) : i_obj(obj), i_range(range) {}
            WorldObject const& GetFocusObject() const { return *i_obj; }
            bool operator()(Unit* u) { return u != i_obj && i_obj->IsWithinDistInMap(u, i_range); }
        private:
            WorldObject const* i_obj;
            float i_range;
    };

    // u:nearby(radius [, filter])  filter: "all" (default), "enemy", "friend", "creature", "player", "dead", "alive"
    static int U_nearby(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        float radius = float(luaL_optnumber(L, 2, 30.0));
        char const* filter = luaL_optstring(L, 3, "all");
        radius = std::min(radius, 200.0f);

        std::list<Unit*> found;
        AnyUnitInRangeCheck check(u, radius);
        MaNGOS::UnitListSearcher<AnyUnitInRangeCheck> searcher(found, check);
        Cell::VisitAllObjects(u, searcher, radius);

        std::vector<Unit*> list(found.begin(), found.end());
        std::sort(list.begin(), list.end(), [u](Unit* a, Unit* b) { return u->GetDistance(a) < u->GetDistance(b); });

        lua_newtable(L);
        lua_Integer i = 0;
        for (Unit* t : list)
        {
            bool keep = true;
            if (!strcmp(filter, "enemy"))         keep = t->IsAlive() && u->IsHostileTo(t);
            else if (!strcmp(filter, "friend"))   keep = u->IsFriendlyTo(t);
            else if (!strcmp(filter, "creature")) keep = t->IsCreature();
            else if (!strcmp(filter, "player"))   keep = t->IsPlayer();
            else if (!strcmp(filter, "dead"))     keep = !t->IsAlive();
            else if (!strcmp(filter, "alive"))    keep = t->IsAlive();
            if (!keep)
                continue;
            LuaUnit_Push(L, t);
            lua_rawseti(L, -2, ++i);
        }
        return 1;
    }
    // u:path(x, y, z) -> {type = PathType flags, normal = bool, length = yards, points = {{x,y,z}...}}
    static int U_path(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        float x = float(luaL_checknumber(L, 2));
        float y = float(luaL_checknumber(L, 3));
        float z = float(luaL_checknumber(L, 4));
        PathInfo path(u);
        path.calculate(x, y, z);
        auto const& pts = path.getPath();
        lua_createtable(L, 0, 4);
        SetField(L, "type", path.getPathType());
        SetFieldB(L, "normal", (path.getPathType() & PATHFIND_NORMAL) != 0);
        float len = 0.0f;
        lua_createtable(L, int(pts.size()), 0);
        for (size_t i = 0; i < pts.size(); ++i)
        {
            if (i > 0)
                len += (pts[i] - pts[i - 1]).length();
            lua_createtable(L, 0, 3);
            SetFieldN(L, "x", Round2(pts[i].x));
            SetFieldN(L, "y", Round2(pts[i].y));
            SetFieldN(L, "z", Round2(pts[i].z));
            lua_rawseti(L, -2, lua_Integer(i + 1));
        }
        lua_setfield(L, -2, "points");
        SetFieldN(L, "length", Round2(len));
        return 1;
    }
    // u:height([x, y, z]) ground height (vmaps + terrain) at a point of the unit's map
    static int U_height(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        float x = float(luaL_optnumber(L, 2, u->GetPositionX()));
        float y = float(luaL_optnumber(L, 3, u->GetPositionY()));
        float z = float(luaL_optnumber(L, 4, u->GetPositionZ() + 2.0f));
        lua_pushnumber(L, Round2(u->GetMap()->GetHeight(x, y, z)));
        return 1;
    }
    // u:lospoint(x, y, z) line of sight from the unit's eyes to a point
    static int U_lospoint(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        float x = float(luaL_checknumber(L, 2));
        float y = float(luaL_checknumber(L, 3));
        float z = float(luaL_checknumber(L, 4));
        lua_pushboolean(L, u->GetMap()->isInLineOfSight(u->GetPositionX(), u->GetPositionY(), u->GetPositionZ() + 2.0f, x, y, z + 2.0f));
        return 1;
    }
    // u:aggroat(x, y, z [, margin]) -> idle creatures that would attack u standing at the point
    // (Commander AggroRange rule: proximity aggro range + margin, height, line of sight)
    static int U_aggroat(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        Commander::Position3 at;
        at.x = float(luaL_checknumber(L, 2));
        at.y = float(luaL_checknumber(L, 3));
        at.z = float(luaL_checknumber(L, 4));
        float const margin = float(luaL_optnumber(L, 5, 0.0));
        float const dx = at.x - u->GetPositionX(), dy = at.y - u->GetPositionY();
        float const radius = std::min(200.0f, std::sqrt(dx * dx + dy * dy) + Commander::AggroRange::MAX_ATTACK_DISTANCE + margin);

        std::list<Unit*> found;
        AnyUnitInRangeCheck check(u, radius);
        MaNGOS::UnitListSearcher<AnyUnitInRangeCheck> searcher(found, check);
        Cell::VisitAllObjects(u, searcher, radius);

        lua_newtable(L);
        lua_Integer i = 0;
        for (Unit* t : found)
        {
            Creature const* c = t->ToCreature();
            if (!c || !Commander::AggroRange::IsIdleAggressor(c, u) ||
                !Commander::AggroRange::Covers(c, Commander::AggroRange::Reach(c, u, margin), at))
                continue;
            LuaUnit_Push(L, t);
            lua_rawseti(L, -2, ++i);
        }
        return 1;
    }
    static int U_casting(lua_State* L) { PushCasting(L, LuaUnit_Check(L, 1)); return 1; }
    static int U_auras(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        lua_newtable(L);
        lua_Integer i = 0;
        for (auto const& itr : u->GetSpellAuraHolderMap())
        {
            SpellAuraHolder* h = itr.second;
            if (!h || h->IsDeleted())
                continue;
            lua_createtable(L, 0, 7);
            SetField(L, "spell", h->GetId());
            SetFieldS(L, "name", h->GetSpellProto()->SpellName[0].c_str());
            lua_pushstring(L, GuidToString(h->GetCasterGuid()).c_str());
            lua_setfield(L, -2, "caster");
            SetField(L, "left", h->GetAuraDuration());
            SetField(L, "max", h->GetAuraMaxDuration());
            SetField(L, "stacks", h->GetStackAmount());
            SetFieldB(L, "positive", h->IsPositive());
            SetFieldB(L, "passive", h->IsPassive());
            lua_rawseti(L, -2, ++i);
        }
        return 1;
    }
    static int U_hasaura(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        lua_pushboolean(L, u->HasAura(uint32(luaL_checkinteger(L, 2))));
        return 1;
    }
    static int U_ready(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        SpellEntry const* e = sSpellMgr.GetSpellEntry(uint32(luaL_checkinteger(L, 2)));
        if (!e)
            return luaL_error(L, "unknown spell");
        lua_pushboolean(L, u->IsSpellReady(e));
        return 1;
    }
    // u:cooldown(spellId) -> ms left on the spell's own / category / lockout cooldown (0 = ready,
    // -1 = permanent), and whether its GCD category is running
    static int U_cooldown(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        SpellEntry const* e = sSpellMgr.GetSpellEntry(uint32(luaL_checkinteger(L, 2)));
        if (!e)
            return luaL_error(L, "unknown spell");
        lua_pushinteger(L, u->GetSpellCooldownLeft(e));
        lua_pushboolean(L, u->HasGCD(e));
        return 2;
    }
    static int U_group(lua_State* L)
    {
        Player* p = CheckPlayer(L, 1);
        lua_newtable(L);
        lua_Integer i = 0;
        if (Group* g = p->GetGroup())
        {
            for (GroupReference* ref = g->GetFirstMember(); ref; ref = ref->next())
            {
                Player* m = ref->getSource();
                if (!m || !m->IsInWorld())
                    continue;
                LuaUnit_Push(L, m);
                lua_rawseti(L, -2, ++i);
            }
        }
        else
        {
            LuaUnit_Push(L, p);
            lua_rawseti(L, -2, ++i);
        }
        return 1;
    }
    static int U_ai(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        PartyBotAI* ai = GetPartyBotAI(u);
        if (!ai)
        {
            lua_pushnil(L);
            return 1;
        }
        uint32 now = WorldTimer::getMSTime();
        lua_createtable(L, 0, 10);
        SetFieldS(L, "role", RoleName(ai->GetRole()));
        SetFieldB(L, "initialized", ai->m_initialized);
        SetFieldB(L, "temporary", ai->m_race && ai->m_class);
        SetFieldB(L, "preventCasting", ai->m_preventCasting);
        SetFieldU(L, "leader", ai->GetPartyLeader());
        lua_newtable(L);
        for (size_t i = 0; i < ai->m_marksToFocus.size(); ++i) { lua_pushinteger(L, ai->m_marksToFocus[i]); lua_rawseti(L, -2, lua_Integer(i + 1)); }
        lua_setfield(L, -2, "focusMarks");
        Commander::BotDirectives const& d = ai->Directives();
        SetField(L, "movementHoldMs", d.GetMovementHoldRemaining(now));
        // follow offset given to MoveFollow (FollowSpot) and whether its safety check is current
        lua_createtable(L, 0, 4);
        SetFieldN(L, "dist", Round2(ai->m_followOffset.dist));
        SetFieldN(L, "angle", Round2(ai->m_followOffset.angle));
        SetFieldB(L, "checked", ai->m_followChecked);
        SetField(L, "motion", u->GetMotionMaster()->GetCurrentMovementGeneratorType());
        lua_setfield(L, -2, "follow");
        if (!d.GetLastDecision().empty())
        {
            SetFieldS(L, "lastDecision", d.GetLastDecision().c_str());
            SetField(L, "lastDecisionAgoMs", WorldTimer::getMSTimeDiff(d.GetLastDecisionMs(), now));
        }
        return 1;
    }

    static char const* DecisionResultName(Commander::CandidateRecord::Result r)
    {
        using R = Commander::CandidateRecord::Result;
        switch (r)
        {
            case R::NotTried:  return "nottried";
            case R::Executed:  return "exec";
            case R::Failed:    return "failed";
            case R::LowerTier: return "lowertier";
            case R::Dropped:   return "dropped";
            case R::Forbidden: return "forbidden";
            case R::VetoSet:   return "veto";
            case R::OrderSet:  return "order";
            case R::Kept:      return "kept";
            case R::Ended:     return "ended";
        }
        return "?";
    }

    // u:decisions([sinceMs]): the Commander arbiter's decision log of a party bot (last
    // BotDirectives::LOG_SIZE ticks with proposals, and an empty cands for the first tick
    // without one), newer than sinceMs (server ms, now()).
    // [{t, cands=[{src, what, tier, score, b, c, valued, reason, res}]}]; nil if not a bot.
    // Second value: true when records newer than sinceMs were already pushed out of the log
    // (the caller polled too slowly; the combat recorder writes a `gap` line).
    static int U_decisions(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        PartyBotAI* ai = GetPartyBotAI(u);
        if (!ai)
        {
            lua_pushnil(L);
            return 1;
        }
        uint32 const since = uint32(luaL_optinteger(L, 2, 0));
        auto const& log = ai->Directives().GetDecisionLog();
        lua_createtable(L, int(log.size()), 0);
        int i = 0;
        for (Commander::DecisionRecord const& d : log)
        {
            if (since && uint32(since - d.timeMs) < 0x80000000u)
                continue;   // not newer than since (getMSTime wraps after 49 days)
            lua_createtable(L, 0, 2);
            SetField(L, "t", d.timeMs);
            lua_createtable(L, int(d.candidates.size()), 0);
            int j = 0;
            for (Commander::CandidateRecord const& c : d.candidates)
            {
                lua_createtable(L, 0, 9);
                SetFieldS(L, "src", c.source.c_str());
                SetFieldS(L, "what", c.what.c_str());
                SetField(L, "tier", c.tier);
                SetFieldN(L, "score", Round2(c.score));
                if (c.valued)
                {
                    SetFieldN(L, "b", Round2(c.benefit));
                    SetFieldN(L, "c", Round2(c.cost));
                }
                if (!c.reason.empty())
                    SetFieldS(L, "reason", c.reason.c_str());
                SetFieldS(L, "res", DecisionResultName(c.result));
                lua_rawseti(L, -2, ++j);
            }
            lua_setfield(L, -2, "cands");
            lua_rawseti(L, -2, ++i);
        }
        bool const gap = since && log.size() >= Commander::BotDirectives::LOG_SIZE &&
                         uint32(log.front().timeMs - since) - 1 < 0x7FFFFFFFu;  // oldest is newer than since
        lua_pushboolean(L, gap);
        return 2;
    }

    // Commander of a player's party on the player's map (bot or human member), null if none.
    static std::shared_ptr<Commander::PartyCommander> CommanderOf(Unit* u)
    {
        if (!u || !u->IsPlayer())
            return nullptr;
        return sCommanderMgr.FindCommander(static_cast<Player*>(u));
    }

    static void SetFieldG(lua_State* L, char const* key, ObjectGuid guid)
    {
        if (guid.IsEmpty())
            return;
        SetFieldS(L, key, GuidToString(guid).c_str());
    }

    static void PushGuidList(lua_State* L, std::vector<ObjectGuid> const& guids)
    {
        lua_createtable(L, int(guids.size()), 0);
        int i = 0;
        for (ObjectGuid g : guids)
        {
            lua_pushstring(L, GuidToString(g).c_str());
            lua_rawseti(L, -2, ++i);
        }
    }

    // u:notes([sinceMs]): notes the party's Commander sent to the leader (TeamPlan::notes,
    // last PartyCommander::NOTE_LOG_SIZE), newer than sinceMs. [{t, text}]; nil without one.
    static int U_notes(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        std::shared_ptr<Commander::PartyCommander> commander = CommanderOf(u);
        if (!commander)
        {
            lua_pushnil(L);
            return 1;
        }
        uint32 const since = uint32(luaL_optinteger(L, 2, 0));
        auto const& log = commander->GetNoteLog();
        lua_createtable(L, int(log.size()), 0);
        int i = 0;
        for (Commander::PartyCommander::NoteRecord const& n : log)
        {
            if (since && uint32(since - n.timeMs) < 0x80000000u)
                continue;   // not newer than since (getMSTime wraps after 49 days)
            lua_createtable(L, 0, 2);
            SetField(L, "t", n.timeMs);
            SetFieldS(L, "text", n.text.c_str());
            lua_rawseti(L, -2, ++i);
        }
        return 1;
    }

    static char const* PullPhaseName(Commander::PullPhase p)
    {
        using P = Commander::PullPhase;
        switch (p)
        {
            case P::None:      return "none";
            case P::WaitReady: return "waitready";
            case P::Approach:  return "approach";
            case P::Return:    return "return";
            case P::Settle:    return "settle";
        }
        return "?";
    }

    static char const* LureStateName(Commander::PatrolLureState s)
    {
        using S = Commander::PatrolLureState;
        switch (s)
        {
            case S::Search:  return "search";
            case S::Active:  return "active";
            case S::NoPoint: return "nopoint";
            case S::Done:    return "done";
            case S::Failed:  return "failed";
        }
        return "?";
    }

    // u:plan(): compact state of the party's TeamPlan (what the recorder writes as `plan`
    // lines). Only plans that are running appear: focus, pull, clear, retreat, patrol,
    // interrupts, intercepts, controls. nil without a Commander.
    static int U_plan(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        std::shared_ptr<Commander::PartyCommander> commander = CommanderOf(u);
        if (!commander)
        {
            lua_pushnil(L);
            return 1;
        }
        Commander::TeamPlan const& plan = commander->GetPlan();
        lua_createtable(L, 0, 8);

        if (!plan.focus.enemy.IsEmpty())
        {
            lua_createtable(L, 0, 2);
            SetFieldG(L, "enemy", plan.focus.enemy);
            SetFieldS(L, "reason", plan.focus.reason.c_str());
            lua_setfield(L, -2, "focus");
        }
        if (plan.pull.phase != Commander::PullPhase::None)
        {
            lua_createtable(L, 0, 5);
            SetFieldS(L, "phase", PullPhaseName(plan.pull.phase));
            SetFieldG(L, "target", plan.pull.target);
            SetFieldG(L, "tank", plan.pull.tank);
            // Ranged pull: the shot spot {x, y, z} and whether the tank reported it failed.
            if (plan.pull.rangedPullSpell)
            {
                lua_createtable(L, 0, 3);
                SetFieldN(L, "x", Round2(plan.pull.rangedSpot.x));
                SetFieldN(L, "y", Round2(plan.pull.rangedSpot.y));
                SetFieldN(L, "z", Round2(plan.pull.rangedSpot.z));
                lua_setfield(L, -2, "spot");
                if (plan.pull.rangedShot.spotFailed)
                    SetFieldS(L, "spotFailed", plan.pull.rangedShot.spotFailWhy);
            }
            // Face pull: the spot {x, y, z}. Corner fight, the opener out (ms), unexpected joiners.
            if (plan.pull.facePull)
            {
                lua_createtable(L, 0, 3);
                SetFieldN(L, "x", Round2(plan.pull.faceSpot.x));
                SetFieldN(L, "y", Round2(plan.pull.faceSpot.y));
                SetFieldN(L, "z", Round2(plan.pull.faceSpot.z));
                lua_setfield(L, -2, "face");
            }
            if (plan.pull.corner)
                SetFieldB(L, "corner", true);
            if (plan.pull.rangedShot.openedAtMs && plan.pull.rangedShot.approachSinceMs == plan.pull.phaseSinceMs)
                SetField(L, "openedAt", plan.pull.rangedShot.openedAtMs);
            if (!plan.pull.extra.empty())
                SetField(L, "extra", uint32(plan.pull.extra.size()));
            // Settle: the tank waits for the pack, who still holds (PullOpening.h), and the line
            // of sight pull {state, x, y, z, hiders = [{g, x, y, z}]}.
            if (plan.pull.awaitArrival)
                SetFieldB(L, "awaitArrival", true);
            if (plan.pull.phase == Commander::PullPhase::Settle)
            {
                SetFieldB(L, "packComing", plan.pull.packComing);
                SetFieldB(L, "holdHealing", plan.pull.holdHealing);
            }
            if (plan.pull.losBreak != Commander::PullLosBreak::None)
            {
                using B = Commander::PullLosBreak;
                B const state = plan.pull.losBreak;
                lua_createtable(L, 0, 5);
                SetFieldS(L, "state", state == B::Active ? "active" : state == B::NoPoint ? "nopoint" : "ended");
                if (state != B::NoPoint)
                {
                    SetFieldN(L, "x", Round2(plan.pull.losPoint.x));
                    SetFieldN(L, "y", Round2(plan.pull.losPoint.y));
                    SetFieldN(L, "z", Round2(plan.pull.losPoint.z));
                }
                lua_createtable(L, int(plan.pull.losHiders.size()), 0);
                int i = 0;
                for (Commander::PullHider const& h : plan.pull.losHiders)
                {
                    lua_createtable(L, 0, 4);
                    SetFieldG(L, "g", h.bot);
                    SetFieldN(L, "x", Round2(h.point.x));
                    SetFieldN(L, "y", Round2(h.point.y));
                    SetFieldN(L, "z", Round2(h.point.z));
                    lua_rawseti(L, -2, ++i);
                }
                lua_setfield(L, -2, "hiders");
                lua_setfield(L, -2, "los");
            }
            lua_setfield(L, -2, "pull");
        }
        if (plan.clear.active)
        {
            lua_createtable(L, 0, 2);
            SetField(L, "cleared", plan.clear.cleared);
            SetFieldB(L, "paused", plan.clear.paused);
            lua_setfield(L, -2, "clear");
        }
        if (plan.retreat.active)
        {
            lua_createtable(L, 0, 3);
            SetFieldN(L, "x", Round2(plan.retreat.point.x));
            SetFieldN(L, "y", Round2(plan.retreat.point.y));
            SetFieldN(L, "z", Round2(plan.retreat.point.z));
            lua_setfield(L, -2, "retreat");
        }
        Commander::FightSpotPlan const& f = plan.fightSpot;
        if (f.planned && !f.plannedSpots.empty())
        {
            lua_createtable(L, 0, 6);
            SetFieldN(L, "x", Round2(f.plannedSpots.front().x));
            SetFieldN(L, "y", Round2(f.plannedSpots.front().y));
            SetFieldN(L, "z", Round2(f.plannedSpots.front().z));
            SetFieldN(L, "benefit", Round2(f.plannedBenefit));
            SetFieldN(L, "cost", Round2(f.plannedCost));
            SetFieldS(L, "why", f.plannedWhy.c_str());
            lua_setfield(L, -2, "fightSpotPlanned");
        }
        if (f.moving)
        {
            lua_createtable(L, 0, 10);
            SetFieldS(L, "phase", "move");
            SetFieldG(L, "patrol", f.patrol);
            SetFieldG(L, "tank", f.tank);
            SetFieldN(L, "startX", Round2(f.from.x));
            SetFieldN(L, "startY", Round2(f.from.y));
            SetField(L, "chosen", f.chosen);
            if (f.chosen >= 0)
            {
                SetFieldN(L, "spotX", Round2(f.chosenSpot.x));
                SetFieldN(L, "spotY", Round2(f.chosenSpot.y));
            }
            SetFieldN(L, "benefit", Round2(f.benefit));
            SetFieldN(L, "cost", Round2(f.cost));
            SetFieldS(L, "why", f.why.c_str());
            lua_setfield(L, -2, "fightSpot");
        }
        Commander::PatrolGuardPlan const& m = plan.patrolGuard;
        if (m.active)
        {
            lua_createtable(L, 0, 6);
            SetFieldS(L, "phase", "guard");
            SetFieldG(L, "patrol", m.patrol);
            SetFieldG(L, "tank", m.tank);
            SetFieldN(L, "points", Round2(m.points));
            if (!m.avoid.empty())
            {
                PushGuidList(L, m.avoid);
                lua_setfield(L, -2, "avoid");
            }
            if (!m.kill.empty())
            {
                PushGuidList(L, m.kill);
                lua_setfield(L, -2, "kill");
            }
            if (!m.lures.empty())
            {
                lua_createtable(L, int(m.lures.size()), 0);
                int j = 0;
                for (Commander::PatrolLure const& l : m.lures)
                {
                    lua_createtable(L, 0, 3);
                    SetFieldG(L, "hider", l.hider);
                    SetFieldS(L, "state", LureStateName(l.state));
                    PushGuidList(L, l.casters);
                    lua_setfield(L, -2, "casters");
                    lua_rawseti(L, -2, ++j);
                }
                lua_setfield(L, -2, "lures");
            }
            lua_setfield(L, -2, "patrol");
        }
        if (!plan.interrupts.empty())
        {
            lua_createtable(L, int(plan.interrupts.size()), 0);
            int i = 0;
            for (Commander::InterruptDuty const& d : plan.interrupts)
            {
                lua_createtable(L, 0, 3);
                SetFieldG(L, "enemy", d.enemy);
                SetFieldG(L, "primary", d.primary);
                SetFieldG(L, "backup", d.backup);
                lua_rawseti(L, -2, ++i);
            }
            lua_setfield(L, -2, "interrupts");
        }
        if (!plan.intercepts.empty())
        {
            lua_createtable(L, int(plan.intercepts.size()), 0);
            int i = 0;
            for (Commander::InterceptDuty const& d : plan.intercepts)
            {
                lua_createtable(L, 0, 3);
                SetFieldG(L, "enemy", d.enemy);
                SetFieldG(L, "bot", d.bot);
                SetFieldS(L, "reason", d.reason);
                lua_rawseti(L, -2, ++i);
            }
            lua_setfield(L, -2, "intercepts");
        }
        // Crowd control duties (CrowdControlAssigner). No points: they change every refresh
        // and the recorder writes a plan line on each change.
        if (!plan.controls.empty())
        {
            lua_createtable(L, int(plan.controls.size()), 0);
            int i = 0;
            for (Commander::ControlDuty const& d : plan.controls)
            {
                lua_createtable(L, 0, 4);
                SetFieldG(L, "enemy", d.enemy);
                SetFieldG(L, "bot", d.bot);
                SetField(L, "spell", d.spellId);
                if (d.area)
                    SetFieldB(L, "area", true);
                lua_rawseti(L, -2, ++i);
            }
            lua_setfield(L, -2, "controls");
        }
        // Area phase (Coordination/Doctrine/AreaPhase.h). No points (they change every refresh).
        if (plan.area.active)
        {
            Commander::AreaPlan const& a = plan.area;
            static char const* const steps[] = { "move", "shield", "cast", "channel" };
            lua_createtable(L, 0, 6);
            SetFieldG(L, "caster", a.caster);
            SetField(L, "spell", a.spellId);
            SetFieldS(L, "step", steps[uint8(a.step)]);
            if (!a.centre.IsEmpty())
                SetFieldG(L, "centre", a.centre);
            if (a.shieldSpellId)
                SetField(L, "shield", a.shieldSpellId);
            if (!a.allyShieldBot.IsEmpty())
                SetFieldG(L, "allyShield", a.allyShieldBot);
            lua_setfield(L, -2, "area");
        }
        // Not started yet: the phase waits (AreaPlan::wait), or the caster's shield goes up
        // while the party holds (AreaPlan::prep*).
        else if (plan.area.wait != Commander::AreaWait::None || !plan.area.prepCaster.IsEmpty())
        {
            Commander::AreaPlan const& a = plan.area;
            lua_createtable(L, 0, 3);
            if (a.wait != Commander::AreaWait::None)
            {
                SetFieldS(L, "wait", a.wait == Commander::AreaWait::Tank ? "tank" : "shield");
                SetFieldG(L, "caster", a.waitCaster);
            }
            if (!a.prepCaster.IsEmpty())
            {
                SetFieldG(L, "prep", a.prepCaster);
                if (a.prepShieldId)
                    SetField(L, "shield", a.prepShieldId);
                if (!a.prepSpots.empty())
                    SetFieldB(L, "spot", true);
            }
            lua_setfield(L, -2, "areaWait");
        }
        // The caster the area phase reserves (no cast or channel time but its area spells).
        if (!plan.area.reserved.IsEmpty())
            SetFieldG(L, "areaReserved", plan.area.reserved);
        // Doctrine (Coordination/Doctrine/TacticalDoctrine.h): how the fight is fought and every
        // enemy's tags and stamps as text (TacticalDoctrine::Describe), so the review shows each
        // enemy's role. A plan line is written when any of it changes.
        if (plan.doctrine.phase != Commander::FightPhase::None)
        {
            Commander::DoctrinePlan const& d = plan.doctrine;
            lua_createtable(L, 0, 6);
            SetFieldS(L, "phase", Commander::TacticalDoctrine::Name(d.phase));
            SetFieldS(L, "tactic", Commander::TacticalDoctrine::Name(d.tactic));
            SetFieldS(L, "tank", Commander::TacticalDoctrine::Name(d.tankJob));
            SetFieldS(L, "outliers", Commander::TacticalDoctrine::Name(d.outliers));
            SetFieldS(L, "why", d.why.c_str());
            lua_createtable(L, int(d.enemies.size()), 0);
            int i = 0;
            for (Commander::DoctrineEnemy const& x : d.enemies)
            {
                lua_createtable(L, 0, 2);
                SetFieldG(L, "g", x.guid);
                std::string const tags = Commander::TacticalDoctrine::Describe(x);
                SetFieldS(L, "tags", tags.c_str());
                lua_rawseti(L, -2, ++i);
            }
            lua_setfield(L, -2, "enemies");
            // The battlefield split (Battlefield.h): {text = "pile (boss + adds), 4: area pile", members = {guids}}.
            lua_createtable(L, int(d.groups.size()), 0);
            i = 0;
            for (Commander::DoctrineGroup const& g : d.groups)
            {
                lua_createtable(L, 0, 2);
                std::string const text = Commander::Battlefield::Describe(d, g);
                SetFieldS(L, "text", text.c_str());
                PushGuidList(L, g.members);
                lua_setfield(L, -2, "members");
                lua_rawseti(L, -2, ++i);
            }
            lua_setfield(L, -2, "groups");
            lua_setfield(L, -2, "doctrine");
        }
        return 1;
    }

    static void PushDutyCandidates(lua_State* L, std::vector<Commander::DutyCandidate> const& list, char const* field)
    {
        if (list.empty())
            return;
        lua_createtable(L, int(list.size()), 0);
        int i = 0;
        for (Commander::DutyCandidate const& c : list)
        {
            lua_createtable(L, 0, 4);
            SetFieldG(L, "bot", c.bot);
            if (c.spellId)
                SetField(L, "spell", c.spellId);
            SetFieldN(L, "rank", Round2(c.rank));
            if (!c.why.empty())
                SetFieldS(L, "why", c.why.c_str());
            lua_rawseti(L, -2, ++i);
        }
        lua_setfield(L, -2, field);
    }

    static void PushTraceRecord(lua_State* L, Commander::Trace::Record const& r)
    {
        lua_createtable(L, 0, 7);
        SetField(L, "id", r.id);
        if (r.parent)
            SetField(L, "parent", r.parent);
        SetField(L, "t", r.timeMs);
        SetFieldS(L, "kind", r.kind.c_str());
        if (!r.label.empty())
            SetFieldS(L, "label", r.label.c_str());
        // JSON text: the recorder writes it into the line as it is.
        if (!r.data.empty())
            SetFieldS(L, "data", r.data.c_str());
        if (!r.input.empty())
            SetFieldS(L, "input", r.input.c_str());
    }

    // u:trace([sinceId]): the calculation trace of the unit's party (Commander/Trace/Trace.h),
    // records with id > sinceId, oldest first: [{id, parent, t, kind, label, data, input}]
    // (data / input: JSON text). Second value: true when some newer than sinceId were already
    // dropped. Every call arms the trace for Trace::ARM_MS: without polling nothing is recorded.
    // nil without a Commander.
    static int U_trace(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        uint32 const since = uint32(luaL_optinteger(L, 2, 0));
        std::shared_ptr<Commander::PartyCommander> commander = CommanderOf(u);
        if (!commander)
        {
            lua_pushnil(L);
            return 1;
        }
        Commander::Trace::Sink& sink = commander->GetTrace();
        sink.Arm(WorldTimer::getMSTime());
        bool gap = false;
        std::vector<Commander::Trace::Record const*> const records = sink.Since(since, gap);
        lua_createtable(L, int(records.size()), 0);
        int i = 0;
        for (Commander::Trace::Record const* r : records)
        {
            PushTraceRecord(L, *r);
            lua_rawseti(L, -2, ++i);
        }
        lua_pushboolean(L, gap);
        return 2;
    }

    // u:tracepinned(tag): the records last pinned with `tag` ("pull": the pull planning,
    // "pullcheck": the recheck before the tank goes), same rows as u:trace, and the set's
    // version (changes with every new set). nil when there is none. u:tracepinned(tag, version):
    // nil, version when the set is still that version.
    static int U_tracepinned(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        char const* tag = luaL_checkstring(L, 2);
        std::shared_ptr<Commander::PartyCommander> commander = CommanderOf(u);
        uint32 version = 0;
        std::vector<Commander::Trace::Record> const* records =
            commander ? commander->GetTrace().Pinned(tag, &version) : nullptr;
        if (!records)
        {
            lua_pushnil(L);
            return 1;
        }
        if (lua_isinteger(L, 3) && uint32(lua_tointeger(L, 3)) == version)
        {
            lua_pushnil(L);         // unchanged since the caller's version: no rows built
            lua_pushinteger(L, version);
            return 2;
        }
        lua_createtable(L, int(records->size()), 0);
        int i = 0;
        for (Commander::Trace::Record const& r : *records)
        {
            PushTraceRecord(L, r);
            lua_rawseti(L, -2, ++i);
        }
        lua_pushinteger(L, version);
        return 2;
    }

    // u:offers(): the brain's duty needs of the last refresh as offered, before the allocation
    // (TeamPlan::offers), with what the allocation made of them:
    // [{kind, target, points, now, reason, cands=[{bot, spell, rank, why}], rejected=[same, rank = net <= 0],
    //   primary, backup}]. Empty table without a Commander. The recorder writes `offers` lines.
    static int U_offers(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        std::shared_ptr<Commander::PartyCommander> commander = CommanderOf(u);
        if (!commander)
        {
            lua_newtable(L);
            return 1;
        }
        Commander::TeamPlan const& plan = commander->GetPlan();
        lua_createtable(L, int(plan.offers.size()), 0);
        int i = 0;
        for (Commander::DutyNeed const& n : plan.offers)
        {
            lua_createtable(L, 0, 9);
            SetFieldS(L, "kind", Commander::DutyAllocator::KindName(n.kind));
            SetFieldG(L, "target", n.target);
            SetFieldN(L, "points", Round2(n.points));
            SetFieldB(L, "now", n.now);
            if (n.reason && *n.reason)
                SetFieldS(L, "reason", n.reason);
            PushDutyCandidates(L, n.candidates, "cands");
            PushDutyCandidates(L, n.rejected, "rejected");
            for (Commander::DutyResult const& r : plan.duties)
            {
                if (r.kind != n.kind || r.target != n.target || r.auraSpellId != n.auraSpellId)
                    continue;
                if (!r.primary.IsEmpty())
                    SetFieldG(L, "primary", r.primary);
                if (!r.backup.IsEmpty())
                    SetFieldG(L, "backup", r.backup);
                break;
            }
            lua_rawseti(L, -2, ++i);
        }
        return 1;
    }

    // u:pull(): the party's running pull as planned (the recorder writes it as a `pull` line):
    // {t = planned at, phase, target, targetName, ranged, face, corner, returnToSpot,
    //  estimate = {sec, incomingDps, partyDps, dpsSource, manaStart, manaLeft, tankLowPct} (FightLoad),
    //  expected = [{g, name, aggro (joins by aggro range)}], report = [what the player was told]}.
    // nil without a Commander or a pull.
    static int U_pull(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        std::shared_ptr<Commander::PartyCommander> commander = CommanderOf(u);
        if (!commander || commander->GetPlan().pull.phase == Commander::PullPhase::None)
        {
            lua_pushnil(L);
            return 1;
        }
        Commander::PullPlan const& pull = commander->GetPlan().pull;
        Map* map = u->GetMap();
        lua_createtable(L, 0, 8);
        SetField(L, "t", pull.plannedAtMs);
        SetFieldS(L, "phase", PullPhaseName(pull.phase));
        SetFieldG(L, "target", pull.target);
        if (Creature const* target = map->GetCreature(pull.target))
            SetFieldS(L, "targetName", target->GetName());
        SetFieldB(L, "ranged", pull.rangedPullSpell != 0);
        SetFieldB(L, "face", pull.facePull);
        SetFieldB(L, "corner", pull.corner);
        SetFieldB(L, "returnToSpot", pull.returnToSpot);
        if (pull.expectedFightSec > 0.0f)
        {
            // The fight estimate the pull was planned with (review.lua compares it at the end).
            lua_createtable(L, 0, 7);
            SetFieldN(L, "sec", Round2(pull.expectedFightSec));
            SetFieldN(L, "incomingDps", Round2(pull.expectedIncomingDps));
            SetFieldN(L, "partyDps", Round2(pull.expectedPartyDps));
            SetFieldS(L, "dpsSource", pull.expectedDpsSource);
            SetFieldN(L, "manaStart", Round2(pull.expectedManaStart));
            SetFieldN(L, "manaLeft", Round2(pull.expectedManaLeft));
            SetFieldN(L, "tankLowPct", Round2(pull.expectedLowestTankPct));
            lua_setfield(L, -2, "estimate");
        }
        lua_createtable(L, int(pull.expected.size()), 0);
        int i = 0;
        for (ObjectGuid g : pull.expected)
        {
            lua_createtable(L, 0, 3);
            SetFieldG(L, "g", g);
            Creature const* c = map->GetCreature(g);
            SetFieldS(L, "name", c ? c->GetName() : "?");
            SetFieldB(L, "aggro", std::find(pull.aggroRangeAdds.begin(), pull.aggroRangeAdds.end(), g) !=
                pull.aggroRangeAdds.end());
            lua_rawseti(L, -2, ++i);
        }
        lua_setfield(L, -2, "expected");
        lua_createtable(L, int(pull.report.size()), 0);
        i = 0;
        for (std::string const& line : pull.report)
        {
            lua_pushstring(L, line.c_str());
            lua_rawseti(L, -2, ++i);
        }
        lua_setfield(L, -2, "report");
        return 1;
    }

    // Compact snapshot used by the recorder and for quick looks.
    static int U_info(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        lua_createtable(L, 0, 24);
        lua_pushstring(L, GuidToString(u->GetObjectGuid()).c_str());
        lua_setfield(L, -2, "guid");
        SetFieldS(L, "name", u->GetName());
        SetField(L, "entry", u->GetEntry());
        SetField(L, "level", u->GetLevel());
        SetFieldB(L, "player", u->IsPlayer());
        if (u->IsPlayer())
            SetField(L, "class", u->GetClass());
        if (PartyBotAI* ai = GetPartyBotAI(u))
            SetFieldS(L, "role", RoleName(ai->GetRole()));
        SetField(L, "hp", u->GetHealth());
        SetField(L, "maxhp", u->GetMaxHealth());
        Powers pt = u->GetPowerType();
        SetField(L, "ptype", pt);
        SetField(L, "power", u->GetPower(pt));
        SetField(L, "maxpower", u->GetMaxPower(pt));
        SetFieldN(L, "x", Round2(u->GetPositionX()));
        SetFieldN(L, "y", Round2(u->GetPositionY()));
        SetFieldN(L, "z", Round2(u->GetPositionZ()));
        SetFieldN(L, "o", Round2(u->GetOrientation()));
        SetField(L, "map", u->GetMapId());
        SetFieldB(L, "alive", u->IsAlive());
        SetFieldB(L, "combat", u->IsInCombat());
        if (Unit* t = SelectionOf(u))
        {
            lua_pushstring(L, GuidToString(t->GetObjectGuid()).c_str());
            lua_setfield(L, -2, "target");
        }
        if (Unit* v = u->GetVictim())
        {
            lua_pushstring(L, GuidToString(v->GetObjectGuid()).c_str());
            lua_setfield(L, -2, "victim");
        }
        for (uint32 type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
        {
            Spell* s = u->GetCurrentSpell(CurrentSpellTypes(type));
            if (s && s->m_spellInfo)
            {
                SetField(L, "cast", s->m_spellInfo->Id);
                SetField(L, "castLeft", s->GetCastedTime());
                break;
            }
        }
        uint32 st = u->GetUnitState();
        if (st & (UNIT_STATE_STUNNED | UNIT_STATE_ROOT | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING))
        {
            std::string s;
            if (st & UNIT_STATE_STUNNED) s += "stun,";
            if (st & UNIT_STATE_ROOT) s += "root,";
            if (st & UNIT_STATE_CONFUSED) s += "confused,";
            if (st & UNIT_STATE_FLEEING) s += "flee,";
            s.pop_back();
            SetFieldS(L, "cc", s.c_str());
        }
        return 1;
    }

    // ---------------------------------------------------------------------------------------
    // write methods
    // ---------------------------------------------------------------------------------------

    static int U_sethp(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        u->SetHealth(uint32(std::max<lua_Integer>(1, luaL_checkinteger(L, 2))));
        return 0;
    }
    static int U_sethppct(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        u->SetHealthPercent(float(luaL_checknumber(L, 2)));
        return 0;
    }
    static int U_setpower(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        Powers p = Powers(luaL_optinteger(L, 3, u->GetPowerType()));
        u->SetPower(p, uint32(luaL_checkinteger(L, 2)));
        return 0;
    }
    static int U_setpowerpct(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        Powers p = Powers(luaL_optinteger(L, 3, u->GetPowerType()));
        u->SetPowerPercent(p, float(luaL_checknumber(L, 2)));
        return 0;
    }
    // full health and mana, energy/rage untouched unless asked
    static int U_refill(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        if (!u->IsAlive())
            return 0;
        u->SetHealthPercent(100.0f);
        if (u->GetPowerType() == POWER_MANA)
            u->SetPowerPercent(POWER_MANA, 100.0f);
        return 0;
    }
    static int U_kill(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        Unit* killer = LuaUnit_Opt(L, 2);
        (killer ? killer : u)->DoKillUnit(u);
        return 0;
    }
    static int U_revive(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        if (Player* p = u->ToPlayer())
        {
            if (!p->IsAlive())
            {
                p->ResurrectPlayer(1.0f);
                p->SpawnCorpseBones();
            }
        }
        else if (Creature* c = u->ToCreature())
        {
            if (!c->IsAlive())
                c->Respawn();
        }
        return 0;
    }
    // u:tp(x, y, z [, o])  same map;  u:tp(map, x, y, z, o)  players only
    static int U_tp(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        int n = lua_gettop(L);
        bool ok;
        if (n >= 6)
        {
            Player* p = CheckPlayer(L, 1);
            ok = p->TeleportTo(uint32(luaL_checkinteger(L, 2)), float(luaL_checknumber(L, 3)), float(luaL_checknumber(L, 4)),
                float(luaL_checknumber(L, 5)), float(luaL_checknumber(L, 6)));
        }
        else
        {
            float o = float(luaL_optnumber(L, 5, u->GetOrientation()));
            ok = u->NearTeleportTo(float(luaL_checknumber(L, 2)), float(luaL_checknumber(L, 3)), float(luaL_checknumber(L, 4)), o);
        }
        lua_pushboolean(L, ok);
        return 1;
    }
    // u:cast(spellId [, target [, triggered]]) -> SpellCastResult (0 = SPELL_CAST_OK)
    static int U_cast(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        uint32 spellId = uint32(luaL_checkinteger(L, 2));
        Unit* target = LuaUnit_Opt(L, 3);
        bool triggered = lua_toboolean(L, 4);
        if (!sSpellMgr.GetSpellEntry(spellId))
            return luaL_error(L, "unknown spell %d", int(spellId));
        lua_pushinteger(L, u->CastSpell(target ? target : u, spellId, triggered));
        return 1;
    }
    static int U_addaura(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        uint32 spellId = uint32(luaL_checkinteger(L, 2));
        if (!sSpellMgr.GetSpellEntry(spellId))
            return luaL_error(L, "unknown spell %d", int(spellId));
        lua_pushboolean(L, u->AddAura(spellId) != nullptr);
        return 1;
    }
    static int U_removeaura(lua_State* L)
    {
        LuaUnit_Check(L, 1)->RemoveAurasDueToSpell(uint32(luaL_checkinteger(L, 2)));
        return 0;
    }
    static int U_setlevel(lua_State* L)
    {
        Player* p = CheckPlayer(L, 1);
        lua_Integer lvl = luaL_checkinteger(L, 2);
        if (lvl < 1 || lvl > 63)
            return luaL_error(L, "level out of range");
        if (uint32(lvl) != p->GetLevel())
        {
            p->GiveLevel(uint32(lvl));
            p->InitTalentForLevel();
            p->SetUInt32Value(PLAYER_XP, 0);
        }
        return 0;
    }
    static int U_resetcd(lua_State* L)
    {
        LuaUnit_Check(L, 1)->RemoveAllCooldowns();
        return 0;
    }
    static int U_respawn(lua_State* L)
    {
        Creature* c = CheckCreature(L, 1);
        if (!c->IsAlive())
            c->Respawn();
        return 0;
    }
    static int U_combatstop(lua_State* L)
    {
        LuaUnit_Check(L, 1)->CombatStop(true);
        return 0;
    }
    static int U_say(lua_State* L)
    {
        LuaUnit_Check(L, 1)->MonsterSay(luaL_checkstring(L, 2));
        return 0;
    }
    // System message to a player's chat window.
    static int U_tell(lua_State* L)
    {
        Player* p = CheckPlayer(L, 1);
        if (WorldSession* s = p->GetSession())
            ChatHandler(s).SendSysMessage(luaL_checkstring(L, 2));
        return 0;
    }
    // u:addonmsg(prefix, body): addon message to a player's PartyBot addon (CHAT_MSG_ADDON).
    // prefix + tab + body must fit PlayerChat::ADDON_MESSAGE_MAX bytes (error otherwise).
    static int U_addonmsg(lua_State* L)
    {
        Player* p = CheckPlayer(L, 1);
        char const* prefix = luaL_checkstring(L, 2);
        size_t len = 0;
        char const* body = luaL_checklstring(L, 3, &len);
        if (strlen(prefix) + 1 + len > Commander::PlayerChat::ADDON_MESSAGE_MAX)
            return luaL_error(L, "addon message too long (%d bytes)", int(strlen(prefix) + 1 + len));
        Commander::PlayerChat::SendAddonMessage(p, prefix, std::string(body, len));
        return 0;
    }
    // u:partysay(text): the character says it in party chat when bot speech is on
    // (Commander/Commands/BotSpeech.h). Returns false when off or not in a group.
    static int U_partysay(lua_State* L)
    {
        Player* p = CheckPlayer(L, 1);
        lua_pushboolean(L, Commander::BotSpeech::Say(p, luaL_checkstring(L, 2)));
        return 1;
    }
    static int U_select(lua_State* L)
    {
        Player* p = CheckPlayer(L, 1);
        Unit* t = LuaUnit_Opt(L, 2);
        p->SetSelectionGuid(t ? t->GetObjectGuid() : ObjectGuid());
        return 0;
    }
    // Movement and melee for headless characters (login()). Clients and bot AIs move their own
    // characters, so these refuse other units.
    static Player* CheckHeadless(lua_State* L, int idx)
    {
        Player* p = CheckPlayer(L, idx);
        if (!GetHeadlessAI(p))
            luaL_error(L, "%s is not logged in headless", p->GetName());
        return p;
    }
    // u:moveto(x, y, z [, walk]) runs there along a path. Poll u:moving() / u:pos().
    static int U_moveto(lua_State* L)
    {
        Player* p = CheckHeadless(L, 1);
        float x = float(luaL_checknumber(L, 2));
        float y = float(luaL_checknumber(L, 3));
        float z = float(luaL_checknumber(L, 4));
        uint32 options = MOVE_PATHFINDING | (lua_toboolean(L, 5) ? MOVE_WALK_MODE : MOVE_RUN_MODE);
        p->GetMotionMaster()->Clear(false, true);
        p->GetMotionMaster()->MovePoint(0, x, y, z, options);
        return 0;
    }
    // u:attack(target) -> bool: select, start auto attack and chase into melee range
    static int U_attack(lua_State* L)
    {
        Player* p = CheckHeadless(L, 1);
        Unit* t = LuaUnit_Check(L, 2);
        p->SetSelectionGuid(t->GetObjectGuid());
        if (!p->Attack(t, true))
        {
            lua_pushboolean(L, 0);
            return 1;
        }
        p->GetMotionMaster()->Clear(false, true);
        p->GetMotionMaster()->MoveChase(t);
        lua_pushboolean(L, 1);
        return 1;
    }
    // u:stop() stops attacking and moving
    static int U_stop(lua_State* L)
    {
        Player* p = CheckHeadless(L, 1);
        p->AttackStop();
        p->GetMotionMaster()->Clear(false, true);
        p->GetMotionMaster()->MoveIdle();
        p->StopMoving();
        return 0;
    }
    // Spell id from an id or a spellbook name ("Heroic Strike" -> highest known rank), 0 if not known.
    static uint32 FindActiveSpell(Player* p, lua_State* L, int idx)
    {
        if (lua_type(L, idx) == LUA_TNUMBER)
        {
            uint32 id = uint32(lua_tointeger(L, idx));
            return p->HasActiveSpell(id) ? id : 0;
        }
        std::string name = luaL_checkstring(L, idx);
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        uint32 best = 0;
        uint8 bestRank = 0;
        for (auto const& itr : p->GetSpellMap())
        {
            if (itr.second.state == PLAYERSPELL_REMOVED || itr.second.disabled || !itr.second.active)
                continue;
            SpellEntry const* e = sSpellMgr.GetSpellEntry(itr.first);
            if (!e || e->IsPassiveSpell())
                continue;
            std::string n = e->SpellName[0];
            std::transform(n.begin(), n.end(), n.begin(), ::tolower);
            if (n != name)
                continue;
            uint8 rank = sSpellMgr.GetSpellRank(itr.first);
            if (!best || rank > bestRank)
            {
                best = itr.first;
                bestRank = rank;
            }
        }
        return best;
    }
    // u:use(spell [, target]) -> ok, reason. spell: id or spellbook name (highest rank).
    // Casts like the client does (CMSG_CAST_SPELL handler): spellbook, cost, cooldown, GCD,
    // range, LOS and facing are checked. The caster turns to the target first.
    // Returns true when the cast started; false plus the SpellCastResult name (or code) when refused.
    static int U_use(lua_State* L)
    {
        Player* p = CheckHeadless(L, 1);
        HeadlessPlayerAI* ai = GetHeadlessAI(p);
        uint32 spellId = FindActiveSpell(p, L, 2);
        if (!spellId)
            return luaL_error(L, "%s does not know spell %s", p->GetName(), luaL_tolstring(L, 2, nullptr));
        Unit* target = LuaUnit_Opt(L, 3);

        WorldPackets::Spell::CastSpell packet;
        packet.spellId = spellId;
        if (target)
        {
            packet.targets.setUnitTarget(target);
            p->SetSelectionGuid(target->GetObjectGuid());
            if (target != p)
                p->SetFacingToObject(target);
        }
        else
            packet.targets.setUnitTarget(p);

        ai->ClearLastCast();
        p->GetSession()->HandleCastSpellOpcode(packet);
        HeadlessCastResult const& r = ai->GetLastCast();
        if (r.timeMs && r.spellId == spellId && !r.ok)
        {
            lua_pushboolean(L, 0);
            if (char const* name = CastFailReasonName(r.reason))
                lua_pushstring(L, name);
            else
                lua_pushinteger(L, r.reason);
            return 2;
        }
        lua_pushboolean(L, 1);
        return 1;
    }
    // u:lastcast() -> {spell, ok, reason, age} last SMSG_CAST_RESULT the server sent, or nil
    static int U_lastcast(lua_State* L)
    {
        HeadlessPlayerAI* ai = GetHeadlessAI(CheckHeadless(L, 1));
        HeadlessCastResult const& r = ai->GetLastCast();
        if (!r.timeMs)
        {
            lua_pushnil(L);
            return 1;
        }
        lua_createtable(L, 0, 4);
        lua_pushinteger(L, r.spellId); lua_setfield(L, -2, "spell");
        lua_pushboolean(L, r.ok); lua_setfield(L, -2, "ok");
        if (!r.ok)
        {
            if (char const* name = CastFailReasonName(r.reason))
                lua_pushstring(L, name);
            else
                lua_pushinteger(L, r.reason);
            lua_setfield(L, -2, "reason");
        }
        lua_pushinteger(L, WorldTimer::getMSTimeDiffToNow(r.timeMs)); lua_setfield(L, -2, "age");
        return 1;
    }
    // u:cancelcast() interrupts the current cast or channel, like pressing Escape
    static int U_cancelcast(lua_State* L)
    {
        Player* p = CheckHeadless(L, 1);
        p->InterruptNonMeleeSpells(false);
        return 0;
    }
    // u:face(target | orientation)
    static int U_face(lua_State* L)
    {
        Player* p = CheckHeadless(L, 1);
        if (lua_type(L, 2) == LUA_TNUMBER)
            p->SetFacingTo(float(lua_tonumber(L, 2)));
        else
            p->SetFacingToObject(LuaUnit_Check(L, 2));
        return 0;
    }
    // u:spells([namePart]) -> {{id, name, rank}, ...} castable spellbook spells, highest rank first
    static int U_spells(lua_State* L)
    {
        Player* p = CheckPlayer(L, 1);
        std::string part = luaL_optstring(L, 2, "");
        std::transform(part.begin(), part.end(), part.begin(), ::tolower);
        struct Row { uint32 id; std::string name; uint8 rank; };
        std::vector<Row> rows;
        for (auto const& itr : p->GetSpellMap())
        {
            if (itr.second.state == PLAYERSPELL_REMOVED || itr.second.disabled || !itr.second.active)
                continue;
            SpellEntry const* e = sSpellMgr.GetSpellEntry(itr.first);
            if (!e || e->IsPassiveSpell())
                continue;
            std::string lower = e->SpellName[0];
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (!part.empty() && lower.find(part) == std::string::npos)
                continue;
            rows.push_back({ itr.first, e->SpellName[0], sSpellMgr.GetSpellRank(itr.first) });
        }
        std::sort(rows.begin(), rows.end(), [](Row const& a, Row const& b)
        {
            return a.name != b.name ? a.name < b.name : a.rank > b.rank;
        });
        lua_createtable(L, int(rows.size()), 0);
        lua_Integer i = 0;
        for (Row const& r : rows)
        {
            lua_createtable(L, 0, 3);
            lua_pushinteger(L, r.id); lua_setfield(L, -2, "id");
            lua_pushstring(L, r.name.c_str()); lua_setfield(L, -2, "name");
            lua_pushinteger(L, r.rank); lua_setfield(L, -2, "rank");
            lua_rawseti(L, -2, ++i);
        }
        return 1;
    }
    static int U_setrole(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        PartyBotAI* ai = GetPartyBotAI(u);
        if (!ai)
            return luaL_error(L, "%s is not a party bot", u->GetName());
        CombatBotRoles role;
        if (!ParseRole(luaL_checkstring(L, 2), role))
            return luaL_error(L, "role must be tank, healer, meleedps, rangedps or auto");
        ai->SetRole(role);      // auto: unlocked, the Commander party assignment may change it
        return 0;
    }
    static int U_removebot(lua_State* L)
    {
        Unit* u = LuaUnit_Check(L, 1);
        PartyBotAI* ai = GetPartyBotAI(u);
        if (!ai || !ai->botEntry)
            return luaL_error(L, "%s is not a party bot", u->GetName());
        ai->botEntry->requestRemoval = true;
        return 0;
    }
    // u:premade("gear"|"spec", entry)
    static int U_premade(lua_State* L)
    {
        Player* p = CheckPlayer(L, 1);
        char const* kind = luaL_checkstring(L, 2);
        uint32 entry = uint32(luaL_checkinteger(L, 3));
        if (!strcmp(kind, "gear"))
        {
            auto const& m = sObjectMgr.GetPlayerPremadeGearTemplates();
            auto itr = m.find(entry);
            if (itr == m.end() || itr->second.requiredClass != p->GetClass())
                return luaL_error(L, "no gear template %d for this class", int(entry));
            sObjectMgr.ApplyPremadeGearTemplateToPlayer(entry, p);
        }
        else if (!strcmp(kind, "spec"))
        {
            auto const& m = sObjectMgr.GetPlayerPremadeSpecTemplates();
            auto itr = m.find(entry);
            if (itr == m.end() || itr->second.requiredClass != p->GetClass())
                return luaL_error(L, "no spec template %d for this class", int(entry));
            sObjectMgr.ApplyPremadeSpecTemplateToPlayer(entry, p);
        }
        else
            return luaL_error(L, "kind must be gear or spec");
        if (PartyBotAI* ai = GetPartyBotAI(p))
            ai->m_resetSpellData = true;
        return 0;
    }
    // Destroys everything in the backpack and bags (not equipped items, not the bags themselves).
    static int U_clearbags(lua_State* L)
    {
        Player* p = CheckPlayer(L, 1);
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            p->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
        for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
            if (Bag* pBag = static_cast<Bag*>(p->GetItemByPos(INVENTORY_SLOT_BAG_0, bag)))
                for (uint32 slot = 0; slot < pBag->GetBagSize(); ++slot)
                    p->DestroyItem(bag, uint8(slot), true);
        return 0;
    }

    class CaptureChatHandler : public ChatHandler
    {
        public:
            explicit CaptureChatHandler(WorldSession* s) : ChatHandler(s) {}
            void SendSysMessage(char const* str) override { m_out += str; m_out += '\n'; }
            void Run(char const* text) { ExecuteCommand(text); }
            std::string m_out;
    };

    // u:cmd(".command")  run a GM command as this player (uses the player's selection).
    // Returns the command's text output instead of sending it to the client.
    static int U_cmd(lua_State* L)
    {
        Player* p = CheckPlayer(L, 1);
        char const* text = luaL_checkstring(L, 2);
        if (*text == '.' || *text == '!')
            ++text;
        WorldSession* s = p->GetSession();
        if (!s)
            return luaL_error(L, "no session");
        CaptureChatHandler h(s);
        h.Run(text);
        lua_pushstring(L, h.m_out.c_str());
        return 1;
    }

    static int U_tostring(lua_State* L)
    {
        LuaUnitRef const* ref = static_cast<LuaUnitRef const*>(luaL_checkudata(L, 1, UNIT_MT));
        Unit* u = LuaUnit_Resolve(*ref);
        lua_pushfstring(L, "Unit(%s %s)", u ? u->GetName() : "<gone>", GuidToString(ref->guid).c_str());
        return 1;
    }
    static int U_eq(lua_State* L)
    {
        LuaUnitRef const* a = LuaUnit_TestRef(L, 1);
        LuaUnitRef const* b = LuaUnit_TestRef(L, 2);
        lua_pushboolean(L, a && b && a->guid == b->guid);
        return 1;
    }

    void LuaUnit_Register(lua_State* L)
    {
        static luaL_Reg const methods[] = {
            { "exists", U_exists }, { "guid", U_guid }, { "name", U_name }, { "entry", U_entry },
            { "level", U_level }, { "class", U_class }, { "race", U_race }, { "isplayer", U_isplayer },
            { "isbot", U_isbot }, { "headless", U_headless }, { "alive", U_alive }, { "incombat", U_incombat }, { "hp", U_hp },
            { "maxhp", U_maxhp }, { "hppct", U_hppct }, { "power", U_power }, { "maxpower", U_maxpower },
            { "powertype", U_powertype }, { "faction", U_faction }, { "moving", U_moving }, { "pos", U_pos },
            { "teleporting", U_teleporting },
            { "states", U_states }, { "gm", U_gm }, { "target", U_target }, { "victim", U_victim },
            { "attackers", U_attackers }, { "threat", U_threat }, { "dist", U_dist }, { "los", U_los },
            { "hostile", U_hostile }, { "friendly", U_friendly }, { "nearby", U_nearby },
            { "path", U_path }, { "height", U_height }, { "lospoint", U_lospoint }, { "aggroat", U_aggroat },
            { "casting", U_casting }, { "auras", U_auras }, { "hasaura", U_hasaura }, { "ready", U_ready },
            { "cooldown", U_cooldown },
            { "group", U_group }, { "ai", U_ai }, { "decisions", U_decisions },
            { "notes", U_notes }, { "plan", U_plan }, { "offers", U_offers }, { "trace", U_trace }, { "tracepinned", U_tracepinned }, { "pull", U_pull }, { "info", U_info },
            { "sethp", U_sethp }, { "sethppct", U_sethppct }, { "setpower", U_setpower },
            { "setpowerpct", U_setpowerpct }, { "refill", U_refill }, { "kill", U_kill },
            { "revive", U_revive }, { "tp", U_tp }, { "cast", U_cast }, { "addaura", U_addaura },
            { "removeaura", U_removeaura }, { "setlevel", U_setlevel }, { "resetcd", U_resetcd },
            { "respawn", U_respawn }, { "combatstop", U_combatstop }, { "say", U_say }, { "tell", U_tell },
            { "addonmsg", U_addonmsg }, { "partysay", U_partysay },
            { "select", U_select }, { "setrole", U_setrole }, { "removebot", U_removebot },
            { "premade", U_premade }, { "clearbags", U_clearbags }, { "cmd", U_cmd },
            { "moveto", U_moveto }, { "attack", U_attack }, { "stop", U_stop },
            { "use", U_use }, { "lastcast", U_lastcast }, { "cancelcast", U_cancelcast },
            { "face", U_face }, { "spells", U_spells },
            { nullptr, nullptr }
        };

        luaL_newmetatable(L, UNIT_MT);
        luaL_newlib(L, methods);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, U_tostring);
        lua_setfield(L, -2, "__tostring");
        lua_pushcfunction(L, U_eq);
        lua_setfield(L, -2, "__eq");
        lua_pop(L, 1);
    }
}
