/*
 * PartyBot Commander entry point. Called from PartyBotAI (see the official-code
 * change table in .kiro/docs/2026-11-01-ai-teammate-code-structure.md section 4).
 *
 * Threads: OnBotUpdate / OnBotCombatUpdate run on map update threads. The commander
 * table is guarded by a mutex; each PartyCommander belongs to one map and is only
 * used by that map's thread. Settings and stats are atomics.
 */

#ifndef MANGOS_COMMANDER_MGR_H
#define MANGOS_COMMANDER_MGR_H

#include "Common.h"
#include "Commander/Registry.h"
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>

class PartyBotAI;
class Player;

namespace Commander
{
    class PartyCommander;

    struct TickStats
    {
        uint64 ticks = 0;
        uint64 totalMicros = 0;
        uint64 maxMicros = 0;
        uint64 elapsedMs = 0;     // since the last reset
    };

    class CommanderMgr
    {
    public:
        static CommanderMgr& Instance();

        // Reads PartyBot.Commander.* from the already loaded mangosd.conf.
        void LoadConfig();

        bool IsEnabled() const { return m_enabled.load(std::memory_order_relaxed); }
        void SetEnabled(bool on) { m_enabled.store(on, std::memory_order_relaxed); }

        // Replaces PB_UPDATE_INTERVAL in PartyBotAI::UpdateAI.
        uint32 GetBotUpdateInterval() const { return m_botUpdateMs.load(std::memory_order_relaxed); }
        void SetBotUpdateInterval(uint32 ms);
        uint32 GetPerceptionInterval() const { return m_perceptionMs.load(std::memory_order_relaxed); }

        // PartyBotAI::UpdateAI, every bot tick after the leader checks.
        void OnBotUpdate(PartyBotAI* bot);
        // PartyBotAI::UpdateAI, in combat, before target selection and movement.
        // True = an action was executed, skip the rest of UpdateAI for this tick.
        bool OnBotCombatUpdate(PartyBotAI* bot);
        // True while an out-of-combat plan (pull in progress) needs the hook above to run
        // out of combat too, and the bot must not sit down to eat or drink.
        bool WantsOutOfCombatControl(PartyBotAI* bot);
        // `.partybot ai retreat` is running for the bot's party: out of combat it must not stop
        // to resurrect, dispel or buff on the way to the rally point (RetreatBehavior).
        bool IsRetreating(PartyBotAI* bot);

        BehaviorList const& GetBehaviors() const { return m_behaviors; }
        AssignerList const& GetAssigners() const { return m_assigners; }
        // Enables / disables every module (behavior and assigner) with this id. False if none.
        bool SetModuleEnabled(std::string const& id, bool on);
        bool IsModuleEnabled(std::string const& id) const;
        size_t GetCommanderCount();
        // Debug commands: the commander of this player's party on the player's map, if any.
        std::shared_ptr<PartyCommander> FindCommander(Player const* member);

        // Cost of PartyBotAI::UpdateAI ticks (step 0b measurements).
        void RecordBotTick(uint64 micros);
        TickStats GetStats() const;
        void ResetStats();

    private:
        CommanderMgr();
        std::shared_ptr<PartyCommander> GetCommander(PartyBotAI* bot);

        std::atomic<bool> m_enabled{ true };
        std::atomic<uint32> m_botUpdateMs{ 1000 };
        std::atomic<uint32> m_perceptionMs{ 200 };

        BehaviorList m_behaviors;
        AssignerList m_assigners;

        using Key = std::tuple<uint32, uint32, uint32>;     // group id, map id, instance id
        struct Entry
        {
            std::shared_ptr<PartyCommander> commander;
            uint32 lastUsedMs = 0;
        };
        std::mutex m_commandersLock;
        std::map<Key, Entry> m_commanders;
        uint32 m_lastSweepMs = 0;

        std::atomic<uint64> m_statTicks{ 0 };
        std::atomic<uint64> m_statTotalMicros{ 0 };
        std::atomic<uint64> m_statMaxMicros{ 0 };
        std::atomic<int64> m_statResetAt{ 0 };          // steady_clock ms
    };

    // Put at the top of a bot AI tick; records how long the tick took.
    class BotTickCostScope
    {
    public:
        BotTickCostScope() : m_start(std::chrono::steady_clock::now()) {}
        ~BotTickCostScope();
    private:
        std::chrono::steady_clock::time_point m_start;
    };
}

#define sCommanderMgr Commander::CommanderMgr::Instance()

#endif
