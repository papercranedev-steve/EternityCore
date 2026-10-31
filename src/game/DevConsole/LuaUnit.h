#ifndef MANGOS_DEVCONSOLE_LUAUNIT_H
#define MANGOS_DEVCONSOLE_LUAUNIT_H

#include "Common.h"
#include "ObjectGuid.h"
#include <string>

struct lua_State;
class Unit;

namespace DevConsole
{
    // Lua scripts keep units across calls (globals, timers), so the userdata stores the guid and
    // map, never a pointer. Each method call resolves the unit again.
    struct LuaUnitRef
    {
        ObjectGuid guid;
        uint32 mapId = 0;
        uint32 instanceId = 0;
    };

    void LuaUnit_Register(lua_State* L);
    void LuaUnit_Push(lua_State* L, Unit* unit);       // pushes nil for nullptr
    Unit* LuaUnit_Resolve(LuaUnitRef const& ref);      // nullptr if gone
    LuaUnitRef const* LuaUnit_TestRef(lua_State* L, int idx);
    Unit* LuaUnit_Check(lua_State* L, int idx);        // raises a Lua error if not a unit or gone
    Unit* LuaUnit_Opt(lua_State* L, int idx);          // nil/none -> nullptr, else LuaUnit_Check
    void LuaUnit_RefToJson(LuaUnitRef const& ref, std::string& out);
    std::string GuidToString(ObjectGuid guid);
    ObjectGuid GuidFromString(char const* s);
}

#endif
