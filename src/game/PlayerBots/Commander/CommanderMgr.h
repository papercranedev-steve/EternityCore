/*
 * PartyBot Commander entry point. Called from TeamBotAI (see the official-code
 * change table in .kiro/docs/2026-11-01-ai-teammate-code-structure.md section 4).
 *
 * Threads: OnBotUpdate / OnBotCombatUpdate run on map update threads. The commander
 * table is guarded by a mutex; each PartyCommander belongs to one map and is only
 * used by that map's thread. Settings and stats are atomics.
 */

#ifndef MANGOS_COMMANDER_MGR_H
#define MANGOS_COMMANDER_MGR_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Commander/Registry.h"
#include "Commander/ThreatFeed.h"
#include "Commander/Coordination/Situation/Pull/PullMethod.h"
#include "Commander/Coordination/Situation/Pull/PullPlace.h"
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>

class TeamBotAI;
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

        // Bots say their decisions, the brain's notes and review anomalies in party chat
        // (BotSpeech.h; `.partybot ai say on|off`, PartyBot.Commander.Speech). Off: notes go to
        // the leader as system messages, decisions are not told. Any thread.
        bool IsSpeechEnabled() const { return m_speech.load(std::memory_order_relaxed); }
        void SetSpeechEnabled(bool on) { m_speech.store(on, std::memory_order_relaxed); }

        // Replaces PB_UPDATE_INTERVAL in PartyBotAI::UpdateAI.
        uint32 GetBotUpdateInterval() const { return m_botUpdateMs.load(std::memory_order_relaxed); }
        void SetBotUpdateInterval(uint32 ms);
        uint32 GetPerceptionInterval() const { return m_perceptionMs.load(std::memory_order_relaxed); }

        // PartyBotAI::UpdateAI, every bot tick after the leader checks.
        void OnBotUpdate(TeamBotAI* bot);
        // PartyBotAI::UpdateAI, in combat, before target selection and movement.
        // True = an action was executed, skip the rest of UpdateAI for this tick.
        bool OnBotCombatUpdate(TeamBotAI* bot);
        // True while an out-of-combat plan (pull in progress, the party fighting, a retreat, a
        // boss module's waves) needs the hook above to run out of combat too. Whether the bot
        // may sit down to eat or drink is the plan's constraint (TeamPlan::ForbidsRest).
        bool WantsOutOfCombatControl(TeamBotAI* bot);
        // `.partybot ai retreat` is running for the bot's party: out of combat it must not stop
        // to resurrect, dispel or buff on the way to the rally point (RetreatBehavior).
        bool IsRetreating(TeamBotAI* bot);
        // The party bot the bot's party follows instead of the player (Guide.h), null = the player.
        Player* GetGuide(TeamBotAI* bot);

        BehaviorList const& GetBehaviors() const { return m_behaviors; }
        AssignerList const& GetAssigners() const { return m_assigners; }
        // Enables / disables every module (behavior and assigner) with this id. False if none.
        bool SetModuleEnabled(std::string const& id, bool on);
        bool IsModuleEnabled(std::string const& id) const;
        size_t GetCommanderCount();
        // Debug commands: the commander of this player's party on the player's map, if any.
        std::shared_ptr<PartyCommander> FindCommander(Player const* member);
        // Player orders for TargetAssigner (TeamPlan::orders), from `.partybot attackstart` /
        // `.partybot pull` (tanksOnly) and `.partybot attackstop`. Chat command thread, like
        // `.partybot ai pull`. No commander yet (no bot ticked on this map): nothing stored.
        void OrderAttack(Player const* player, ObjectGuid enemy, bool tanksOnly);
        void OrderLeaveAlone(Player const* player, ObjectGuid enemy);

        // Role a player chose for himself (`.partybot setrole <role> <own name>`, the addon's role
        // button on your own character). ROLE_INVALID (0) = none: talents and gear decide
        // (RoleCapability). Runtime only, any thread.
        void SetPlayerRole(ObjectGuid player, uint8 role);
        uint8 GetPlayerRole(ObjectGuid player) const;

        // Threat meter subscription of a player (`.partybot ai threat watch`, the addon's threat
        // window). ThreatFeed reads it on map threads. Runtime only, any thread.
        void SetThreatWatch(ObjectGuid player, ThreatWatch mode);
        ThreatWatch GetThreatWatch(ObjectGuid player) const;

        // How the tank opens pulls this player starts (`.partybot ai pullmethod`, the addon's
        // pull method button). Runtime only, any thread.
        void SetPullMethod(ObjectGuid player, PullMethod method);
        PullMethod GetPullMethod(ObjectGuid player) const;
        // Where the party fights pulls this player starts (`.partybot ai pullplace`, the addon's
        // fight place button). Runtime only, any thread (same lock as the pull method).
        void SetPullPlace(ObjectGuid player, PullPlace place);
        PullPlace GetPullPlace(ObjectGuid player) const;

        // Cost of PartyBotAI::UpdateAI ticks (step 0b measurements).
        void RecordBotTick(uint64 micros);
        TickStats GetStats() const;
        void ResetStats();

    private:
        CommanderMgr();
        std::shared_ptr<PartyCommander> GetCommander(TeamBotAI* bot);

        std::atomic<bool> m_enabled{ true };
        std::atomic<bool> m_speech{ true };
        std::atomic<uint32> m_botUpdateMs{ 1000 };
        std::atomic<uint32> m_perceptionMs{ 200 };

        BehaviorList m_behaviors;
        AssignerList m_assigners;

        using Key = std::tuple<uint64, uint32, uint32>;     // team key (Team.h), map id, instance id
        struct Entry
        {
            std::shared_ptr<PartyCommander> commander;
            uint32 lastUsedMs = 0;
        };
        std::mutex m_commandersLock;
        std::map<Key, Entry> m_commanders;
        uint32 m_lastSweepMs = 0;

        mutable std::mutex m_playerRolesLock;
        std::map<uint64, uint8> m_playerRoles;      // player guid -> CombatBotRoles

        mutable std::mutex m_threatWatchLock;
        std::map<uint64, ThreatWatch> m_threatWatch;    // player guid -> mode, Off not stored

        mutable std::mutex m_pullMethodLock;
        std::map<uint64, PullMethod> m_pullMethods;     // player guid -> method, Auto not stored
        std::map<uint64, PullPlace> m_pullPlaces;       // player guid -> place, Auto not stored

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
