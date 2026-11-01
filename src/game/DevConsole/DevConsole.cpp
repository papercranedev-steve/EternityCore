#include "DevConsole/DevConsole.h"
#include "DevConsole/LuaJson.h"
#include "DevConsole/LuaUnit.h"
#include "DevConsole/CombatEvents.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "Config/Config.h"
#include "Log.h"
#include "Player.h"
#include "Policies/SingletonImp.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

INSTANTIATE_SINGLETON_1(DevConsole::DevConsoleMgr);

namespace DevConsole
{
    void LuaWorld_Register(lua_State* L); // LuaWorld.cpp

    static char const* const REG_MGR = "DevConsole.Mgr";

    DevConsoleMgr::~DevConsoleMgr()
    {
        CloseState();
    }

    void DevConsoleMgr::LoadConfig()
    {
        m_enabled = sConfig.GetBoolDefault("DevConsole.Enable", false);
        m_scriptDir = sConfig.GetStringDefault("DevConsole.ScriptDir", "lua");
        m_scenarioAccount = sConfig.GetStringDefault("DevConsole.ScenarioAccount", "TEST");
        m_instructionLimit = uint64(sConfig.GetIntDefault("DevConsole.InstructionLimit", 20000000));
        m_timeLimitMs = uint32(sConfig.GetIntDefault("DevConsole.TimeLimitMs", 2000));
        m_memoryLimit = size_t(sConfig.GetIntDefault("DevConsole.MemoryLimitMB", 256)) * 1024u * 1024u;
        while (!m_scriptDir.empty() && (m_scriptDir.back() == '/' || m_scriptDir.back() == '\\'))
            m_scriptDir.pop_back();
    }

    bool DevConsoleMgr::IsScenarioAccount(std::string const& accountName) const
    {
        if (m_scenarioAccount.empty() || accountName.size() != m_scenarioAccount.size())
            return false;
        for (size_t i = 0; i < accountName.size(); ++i)
            if (tolower((unsigned char)accountName[i]) != tolower((unsigned char)m_scenarioAccount[i]))
                return false;
        return true;
    }

    void* DevConsoleMgr::Alloc(void* ud, void* ptr, size_t osize, size_t nsize)
    {
        DevConsoleMgr* self = static_cast<DevConsoleMgr*>(ud);
        size_t const old = ptr ? osize : 0;
        if (nsize == 0)
        {
            self->m_memoryUsed -= old;
            free(ptr);
            return nullptr;
        }
        if (nsize > old && self->m_memoryUsed + (nsize - old) > self->m_memoryLimit)
            return nullptr; // Lua raises "not enough memory"
        void* p = realloc(ptr, nsize);
        if (p)
            self->m_memoryUsed = self->m_memoryUsed - old + nsize;
        return p;
    }

    void DevConsoleMgr::InstructionHook(lua_State* L, lua_Debug* /*ar*/)
    {
        lua_getfield(L, LUA_REGISTRYINDEX, REG_MGR);
        DevConsoleMgr* self = static_cast<DevConsoleMgr*>(lua_touserdata(L, -1));
        lua_pop(L, 1);
        if (!self)
            return;
        self->m_instructions += 1000;
        if (self->m_instructions > self->m_instructionLimit)
            luaL_error(L, "instruction limit reached (%d)", int(self->m_instructionLimit));
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - self->m_callStart).count();
        if (ms > self->m_timeLimitMs)
            luaL_error(L, "time limit reached (%d ms)", int(self->m_timeLimitMs));
    }

    static int Traceback(lua_State* L)
    {
        char const* msg = lua_tostring(L, 1);
        if (!msg)
            msg = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
        luaL_traceback(L, L, msg, 1);
        return 1;
    }

    static int LuaPrint(lua_State* L)
    {
        int n = lua_gettop(L);
        std::string line;
        for (int i = 1; i <= n; ++i)
        {
            if (i > 1)
                line += '\t';
            line += luaL_tolstring(L, i, nullptr);
            lua_pop(L, 1);
        }
        lua_getfield(L, LUA_REGISTRYINDEX, REG_MGR);
        DevConsoleMgr* self = static_cast<DevConsoleMgr*>(lua_touserdata(L, -1));
        lua_pop(L, 1);
        if (self)
            self->AppendPrint(line);
        return 0;
    }

    void DevConsoleMgr::AppendPrint(std::string const& s)
    {
        if (m_print.size() > 1024 * 1024)
            return;
        m_print += s;
        m_print += '\n';
    }

    void DevConsoleMgr::WriteLog(std::string const& s)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[DevConsole] %s", s.c_str());
    }

    void DevConsoleMgr::CloseState()
    {
        if (m_L)
        {
            lua_close(m_L);
            m_L = nullptr;
        }
        CombatEvents::SetActive(false);
        m_memoryUsed = 0;
    }

    bool DevConsoleMgr::EnsureState(std::string& error)
    {
        if (m_L)
            return true;
        error = Reload();
        return m_L != nullptr;
    }

    std::string DevConsoleMgr::Reload()
    {
        CloseState();

        lua_State* L = lua_newstate(&DevConsoleMgr::Alloc, this);
        if (!L)
            return "lua_newstate failed";
        m_L = L;

        lua_pushlightuserdata(L, this);
        lua_setfield(L, LUA_REGISTRYINDEX, REG_MGR);

        luaL_openlibs(L);
        lua_pushcfunction(L, LuaPrint);
        lua_setglobal(L, "print");
        lua_pushstring(L, m_scriptDir.c_str());
        lua_setglobal(L, "SCRIPT_DIR");

        LuaUnit_Register(L);
        LuaWorld_Register(L);

        lua_sethook(L, &DevConsoleMgr::InstructionHook, LUA_MASKCOUNT, 1000);

        std::string path = m_scriptDir + "/init.lua";
        BeginCall();
        if (luaL_loadfile(L, path.c_str()) != LUA_OK)
        {
            std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "load error";
            lua_pop(L, 1);
            WriteLog("init.lua: " + err);
            return err; // state stays usable without the scripts
        }
        if (PCall(0, 0) != LUA_OK)
        {
            std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "error";
            lua_pop(L, 1);
            WriteLog("init.lua: " + err);
            return err;
        }
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[DevConsole] Lua state ready, scripts from %s", m_scriptDir.c_str());
        return std::string();
    }

    void DevConsoleMgr::BeginCall()
    {
        if (m_inCall)
            return;
        m_instructions = 0;
        m_callStart = std::chrono::steady_clock::now();
    }

    int DevConsoleMgr::PCall(int nargs, int nresults)
    {
        lua_State* L = m_L;
        int base = lua_gettop(L) - nargs;
        lua_pushcfunction(L, Traceback);
        lua_insert(L, base);
        bool const outer = !m_inCall;
        m_inCall = true;
        int rc = lua_pcall(L, nargs, nresults, base);
        if (outer)
            m_inCall = false;
        lua_remove(L, base);
        return rc;
    }

    std::string DevConsoleMgr::Run(std::string const& code)
    {
        std::string error;
        if (!EnsureState(error) && !m_L)
            return std::string("{\"ok\":false,\"error\":") + [&] { std::string s; JsonEscape(error.data(), error.size(), s); return s; }() + "}";

        lua_State* L = m_L;
        bool const outer = !m_inCall;
        if (outer)
            m_print.clear();
        BeginCall();

        int top = lua_gettop(L);
        // try as an expression first, like a browser console
        std::string expr = "return " + code;
        if (luaL_loadbuffer(L, expr.data(), expr.size(), "=console") != LUA_OK)
        {
            lua_pop(L, 1);
            if (luaL_loadbuffer(L, code.data(), code.size(), "=console") != LUA_OK)
            {
                std::string out = "{\"ok\":false,\"error\":";
                size_t len;
                char const* msg = lua_tolstring(L, -1, &len);
                JsonEscape(msg ? msg : "syntax error", msg ? len : 12, out);
                out += "}";
                lua_settop(L, top);
                return out;
            }
        }

        int rc = PCall(0, LUA_MULTRET);
        std::string out;
        if (rc != LUA_OK)
        {
            out = "{\"ok\":false,\"error\":";
            size_t len;
            char const* msg = lua_tolstring(L, -1, &len);
            JsonEscape(msg ? msg : "error", msg ? len : 5, out);
        }
        else
        {
            int n = lua_gettop(L) - top;
            out = "{\"ok\":true,\"result\":";
            if (n == 0)
                out += "null";
            else if (n == 1)
                LuaToJson(L, -1, out);
            else
            {
                out += '[';
                for (int i = 1; i <= n; ++i)
                {
                    if (i > 1)
                        out += ',';
                    LuaToJson(L, top + i, out);
                }
                out += ']';
            }
        }
        if (outer && !m_print.empty())
        {
            out += ",\"print\":";
            JsonEscape(m_print.data(), m_print.size(), out);
        }
        out += '}';
        lua_settop(L, top);
        return out;
    }

    // Pushes the value at a dotted global path ("a.b.c"), nil if missing.
    static void PushPath(lua_State* L, char const* path)
    {
        lua_pushglobaltable(L);
        std::string p = path;
        size_t start = 0;
        while (true)
        {
            size_t dot = p.find('.', start);
            std::string key = p.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            if (!lua_istable(L, -1))
            {
                lua_pop(L, 1);
                lua_pushnil(L);
                return;
            }
            lua_getfield(L, -1, key.c_str());
            lua_remove(L, -2);
            if (dot == std::string::npos)
                return;
            start = dot + 1;
        }
    }

    std::string DevConsoleMgr::CallPlayerHook(char const* path, Player* player, char const* args)
    {
        std::string error;
        if (!EnsureState(error) && !m_L)
            return error;
        lua_State* L = m_L;
        int top = lua_gettop(L);
        PushPath(L, path);
        if (!lua_isfunction(L, -1))
        {
            lua_settop(L, top);
            return std::string(path) + " is not defined (check init.lua, then .lua reload)";
        }
        LuaUnit_Push(L, player);
        lua_pushstring(L, args ? args : "");
        BeginCall();
        int rc = PCall(2, 0);
        if (rc != LUA_OK)
        {
            error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "error";
            WriteLog(std::string(path) + ": " + error);
        }
        lua_settop(L, top);
        return error;
    }

    void DevConsoleMgr::Update(uint32 /*diff*/)
    {
        if (!m_enabled || !m_L)
            return;
        lua_State* L = m_L;
        int top = lua_gettop(L);
        lua_getglobal(L, "__tick");
        if (!lua_isfunction(L, -1))
        {
            lua_settop(L, top);
            return;
        }
        BeginCall();
        if (PCall(0, 0) != LUA_OK)
            WriteLog(std::string("__tick: ") + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "error"));
        lua_settop(L, top);
    }
}
