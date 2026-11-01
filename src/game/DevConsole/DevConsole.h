/*
 * Dev console: a Lua state inside mangosd for debugging, test scenarios and combat recording.
 *
 * Everything runs in the world thread at a point where map updates are finished
 * (World::ProcessCliCommands and right after it), so scripts can read and change game objects.
 *
 * Entry points:
 *   .lua <code>      console only (stdin console / SOAP). Prints one JSON line.
 *   .lua reload      recreate the Lua state and run <ScriptDir>/init.lua
 *   .scenario ...    player command, forwarded to the Lua function scenario.command(player, args)
 * World::Update calls Update() every tick, which runs the Lua timers (__tick).
 *
 * Config (mangosd.conf):
 *   DevConsole.Enable            0/1, default 0
 *   DevConsole.ScriptDir         folder containing init.lua
 *   DevConsole.InstructionLimit  max Lua instructions per call, default 20000000
 *   DevConsole.TimeLimitMs       max wall time per call, default 2000
 *   DevConsole.MemoryLimitMB     default 256
 *   DevConsole.ScenarioAccount   only this account may use .scenario and login(), default "TEST"
 */

#ifndef MANGOS_DEVCONSOLE_H
#define MANGOS_DEVCONSOLE_H

#include "Common.h"
#include "Policies/Singleton.h"
#include <chrono>
#include <string>

struct lua_State;
struct lua_Debug;
class Player;

namespace DevConsole
{
    class DevConsoleMgr
    {
        public:
            DevConsoleMgr() = default;
            ~DevConsoleMgr();

            void LoadConfig();
            bool IsEnabled() const { return m_enabled; }

            // Runs a chunk and returns a JSON object: {"ok":true,"result":...,"print":"..."} or
            // {"ok":false,"error":"...","print":"..."}. Expressions are accepted ("return" is optional).
            std::string Run(std::string const& code);

            // Calls a global Lua function path ("scenario.command") with a player and a string.
            // Returns the error text, empty on success.
            std::string CallPlayerHook(char const* path, Player* player, char const* args);

            // Recreates the Lua state and runs init.lua. Returns the error text, empty on success.
            std::string Reload();

            void Update(uint32 diff);

            std::string const& GetScriptDir() const { return m_scriptDir; }
            std::string const& GetScenarioAccount() const { return m_scenarioAccount; }
            // Case-insensitive; false when ScenarioAccount is empty. Scenarios and headless logins
            // change the character, so they are limited to this account.
            bool IsScenarioAccount(std::string const& accountName) const;

            // used by Lua bindings
            void AppendPrint(std::string const& s);
            void WriteLog(std::string const& s);

        private:
            bool EnsureState(std::string& error);
            void CloseState();
            void BeginCall();
            int PCall(int nargs, int nresults);   // with traceback and limits

            static void InstructionHook(lua_State* L, lua_Debug* ar);
            static void* Alloc(void* ud, void* ptr, size_t osize, size_t nsize);

            lua_State* m_L = nullptr;
            bool m_enabled = false;
            std::string m_scriptDir;
            std::string m_scenarioAccount = "TEST";
            uint64 m_instructionLimit = 20000000;
            uint32 m_timeLimitMs = 2000;
            size_t m_memoryLimit = 256u * 1024u * 1024u;
            size_t m_memoryUsed = 0;

            // per call
            uint64 m_instructions = 0;
            std::chrono::steady_clock::time_point m_callStart;
            std::string m_print;
            bool m_inCall = false;
    };
}

#define sDevConsole MaNGOS::Singleton<DevConsole::DevConsoleMgr>::Instance()

#endif
