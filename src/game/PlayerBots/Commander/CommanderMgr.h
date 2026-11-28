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
#include "Commander/Coordination/Situation/Pull/PullStance.h"
#include "Commander/Coordination/Situation/Summon.h"
#include "Commander/Commands/Voice/VoiceSettings.h"
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
    struct StaySpot;
    struct PullQuestion;
    struct VoiceEvent;

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

        // Debug speech: bots say their decisions, the brain's notes and review anomalies in party
        // chat (BotSpeech.h; `.partybot ai debugsay on|off`, PartyBot.Commander.DebugSpeech,
        // default off). For testing, not for players: what players hear is the party voice
        // (Commands/Voice/PartyVoice.h). Off: notes are only kept in the note log. Any thread.
        bool IsDebugSpeechEnabled() const { return m_debugSpeech.load(std::memory_order_relaxed); }
        void SetDebugSpeechEnabled(bool on) { m_debugSpeech.store(on, std::memory_order_relaxed); }

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
        // The bot waits where the player left it (Situation/Stay.h): true and its spot.
        bool GetStay(TeamBotAI const* bot, StaySpot& spot);
        // The player's "wait here" / "follow me" for one member (its own team's plan: the member
        // may be on another map than the player). False: no commander of its team yet, or
        // nothing changed (follow while not staying).
        bool SetStay(Player const* member, bool stay);
        // A warlock's summoning ritual (Situation/Summon.h), on the warlock's team's plan. False:
        // no commander yet, or one is under way.
        bool StartSummon(Player const* warlock, ObjectGuid target, std::vector<ObjectGuid> const& helpers);
        // The bot's part in a ritual under way (None: no ritual or not in it); `warlock` and
        // `target` filled while there is one.
        SummonRole GetSummonRole(TeamBotAI const* bot, ObjectGuid& warlock, ObjectGuid& target);
        // The warlock's ritual is over (`leaderBack`: its target was the player the team waited
        // for and he came: the stays end).
        void EndSummon(Player const* warlock, bool leaderBack);
        // Its summon went out to `target` (not summoned again while it may accept).
        void MarkSummonSent(Player const* warlock, ObjectGuid target);
        // "把掉队的都拉过来": the team gathers its members left behind now (GatherPlanner), on the
        // leader's map. False: no commander there.
        bool GatherNow(Player const* leader);
        // The team's question to the player before a pull (Situation/Pull/PullQuestions.h), waiting for
        // his answer; the asker's life shows it (Companions/Fight/PullQuestion). False: none.
        bool GetPullQuestion(TeamBotAI const* bot, PullQuestion& out);
        // The asker showed question `id` to him (one never shown is not taken as a no).
        void MarkPullQuestionShown(Player const* bot, uint32 id);
        // His answer to question `id` (an old one: ignored).
        void AnswerPullQuestion(Player const* bot, uint32 id, bool yes);
        // A line for the players from `member`'s party voice (Commands/Voice/PartyVoice.h), e.g.
        // a rotation's. Map thread of the member only. Nothing without a commander there.
        void QueueVoice(Player const* member, VoiceEvent event);
        // `member` saw `caster` interrupt `target`'s `spellId` (PartyVoice::NoteInterrupt). Same
        // thread rule as QueueVoice.
        void NoteInterrupt(Player const* member, ObjectGuid caster, ObjectGuid target, uint32 spellId);

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
        // Whether this player's party starts fights on its own (PullStance.h; the talk window,
        // `.partybot ai pullstance`). Saved per character (custom_character_pull_stance, Active
        // not stored); any thread (same lock as the pull method).
        void SetPullStance(ObjectGuid player, PullStance stance);
        PullStance GetPullStance(ObjectGuid player) const;
        // Reads the saved stances (startup, PlayerBotMgr::Load).
        void LoadPullStances();
        // What this player wants to hear from his party (`.partybot voice`, the addon's
        // settings; VoiceSettings.h). Runtime only, any thread.
        void SetVoiceSettings(ObjectGuid player, VoiceSettings const& settings);
        VoiceSettings GetVoiceSettings(ObjectGuid player) const;

        // Cost of PartyBotAI::UpdateAI ticks (step 0b measurements).
        void RecordBotTick(uint64 micros);
        TickStats GetStats() const;
        void ResetStats();

    private:
        CommanderMgr();
        std::shared_ptr<PartyCommander> GetCommander(TeamBotAI* bot);

        std::atomic<bool> m_enabled{ true };
        std::atomic<bool> m_debugSpeech{ false };
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
        std::map<uint64, PullStance> m_pullStances;     // player guid -> stance, Active not stored

        mutable std::mutex m_voiceLock;
        std::map<uint64, VoiceSettings> m_voiceSettings;    // player guid -> settings, defaults not stored

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
