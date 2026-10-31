#ifndef MANGOS_DEVCONSOLE_LUAJSON_H
#define MANGOS_DEVCONSOLE_LUAJSON_H

#include <string>

struct lua_State;

namespace DevConsole
{
    // Appends the Lua value at idx as JSON.
    // Tables with keys 1..n are arrays, other tables objects, empty tables []. Unit userdata become
    // {"unit":guid,"name":...}. Functions and other userdata become strings. Nesting is cut at depth 16.
    void LuaToJson(lua_State* L, int idx, std::string& out, int depth = 0);
    void JsonEscape(char const* s, size_t len, std::string& out);
}

#endif
