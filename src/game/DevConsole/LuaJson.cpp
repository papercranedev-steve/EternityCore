#include "DevConsole/LuaJson.h"
#include "DevConsole/LuaUnit.h"
#include "lua.h"
#include "lauxlib.h"
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>

namespace DevConsole
{
    void JsonEscape(char const* s, size_t len, std::string& out)
    {
        out += '"';
        for (size_t i = 0; i < len; ++i)
        {
            unsigned char c = static_cast<unsigned char>(s[i]);
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20)
                    {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    }
                    else
                        out += char(c);
            }
        }
        out += '"';
    }

    static void NumberToJson(lua_State* L, int idx, std::string& out)
    {
        char buf[64];
        if (lua_isinteger(L, idx))
            snprintf(buf, sizeof(buf), "%lld", (long long)lua_tointeger(L, idx));
        else
        {
            double d = lua_tonumber(L, idx);
            if (!std::isfinite(d))
            {
                out += "null";
                return;
            }
            snprintf(buf, sizeof(buf), "%.6g", d);
        }
        out += buf;
    }

    // Returns n > 0 if the table is a sequence 1..n, 0 if empty, -1 otherwise.
    static lua_Integer ArrayLength(lua_State* L, int idx)
    {
        lua_Integer count = 0;
        lua_Integer maxKey = 0;
        lua_pushnil(L);
        while (lua_next(L, idx))
        {
            lua_pop(L, 1);
            if (lua_type(L, -1) != LUA_TNUMBER || !lua_isinteger(L, -1))
            {
                lua_pop(L, 1);
                return -1;
            }
            lua_Integer k = lua_tointeger(L, -1);
            if (k < 1)
            {
                lua_pop(L, 1);
                return -1;
            }
            maxKey = std::max(maxKey, k);
            ++count;
        }
        return (count == maxKey) ? count : -1;
    }

    void LuaToJson(lua_State* L, int idx, std::string& out, int depth)
    {
        idx = lua_absindex(L, idx);
        switch (lua_type(L, idx))
        {
            case LUA_TNIL:
            case LUA_TNONE:
                out += "null";
                return;
            case LUA_TBOOLEAN:
                out += lua_toboolean(L, idx) ? "true" : "false";
                return;
            case LUA_TNUMBER:
                NumberToJson(L, idx, out);
                return;
            case LUA_TSTRING:
            {
                size_t len;
                char const* s = lua_tolstring(L, idx, &len);
                JsonEscape(s, len, out);
                return;
            }
            case LUA_TUSERDATA:
            {
                if (LuaUnitRef const* ref = LuaUnit_TestRef(L, idx))
                {
                    LuaUnit_RefToJson(*ref, out);
                    return;
                }
                break;
            }
            case LUA_TTABLE:
            {
                if (depth >= 16)
                {
                    out += "\"<max depth>\"";
                    return;
                }
                if (!lua_checkstack(L, 4))
                {
                    out += "null";
                    return;
                }
                lua_Integer n = ArrayLength(L, idx);
                if (n >= 0)
                {
                    out += '[';
                    for (lua_Integer i = 1; i <= n; ++i)
                    {
                        if (i > 1)
                            out += ',';
                        lua_geti(L, idx, i);
                        LuaToJson(L, -1, out, depth + 1);
                        lua_pop(L, 1);
                    }
                    out += ']';
                    return;
                }

                // object: sort keys so output is stable and easy to diff
                std::vector<std::string> keys;
                lua_pushnil(L);
                while (lua_next(L, idx))
                {
                    lua_pop(L, 1);
                    lua_pushvalue(L, -1);
                    size_t len;
                    char const* s = lua_tolstring(L, -1, &len); // copy, so lua_next keeps its key
                    keys.emplace_back(s ? std::string(s, len) : std::string("?"));
                    lua_pop(L, 1);
                }
                std::sort(keys.begin(), keys.end());
                keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

                out += '{';
                bool first = true;
                for (std::string const& key : keys)
                {
                    // look the value up by string key first, then as integer key
                    lua_pushlstring(L, key.data(), key.size());
                    lua_rawget(L, idx);
                    if (lua_isnil(L, -1))
                    {
                        lua_pop(L, 1);
                        char* end = nullptr;
                        long long k = strtoll(key.c_str(), &end, 10);
                        if (end && !*end)
                            lua_rawgeti(L, idx, k);
                        else
                            lua_pushnil(L);
                    }
                    if (!first)
                        out += ',';
                    first = false;
                    JsonEscape(key.data(), key.size(), out);
                    out += ':';
                    LuaToJson(L, -1, out, depth + 1);
                    lua_pop(L, 1);
                }
                out += '}';
                return;
            }
            default:
                break;
        }

        char const* name = luaL_typename(L, idx);
        std::string s = "<";
        s += name;
        s += ">";
        JsonEscape(s.data(), s.size(), out);
    }
}
