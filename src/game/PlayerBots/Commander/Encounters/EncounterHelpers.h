/*
 * PartyBot Commander: small helpers shared by boss modules (pure functions on facts / plan).
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_HELPERS_H
#define MANGOS_COMMANDER_ENCOUNTER_HELPERS_H

#include "Common.h"
#include "Commander/CommanderTypes.h"
#include "Commander/Coordination/DutyAllocator.h"
#include <vector>

namespace Commander
{
    struct WorldModel;
    struct TeamPlan;
    struct EnemyFacts;
    struct PartyMemberFacts;
    struct Position3;
    struct VoiceEvent;
    enum class VoiceTier : uint8;

    namespace EncounterHelpers
    {
        // A line for the players (TeamPlan::voice, Commands/Voice/PartyVoice.h): `key` a
        // PlayerBots/i18n "voice.boss.*" key, `speaker` empty: the main tank bot.
        VoiceEvent Line(char const* key, VoiceTier tier, ObjectGuid speaker = ObjectGuid());
        // First enemy with this entry (alive enemies only are in the snapshot).
        EnemyFacts const* FindEnemyByEntry(WorldModel const& world, uint32 entry);
        bool HasEnemyEntry(WorldModel const& world, uint32 entry);

        // The main tank (bot or player, Perception/Tanks.h), or null.
        PartyMemberFacts const* FindTank(WorldModel const& world);
        // Who holds `enemy`: its victim if that is a party member (not a pet), else the main tank.
        PartyMemberFacts const* FindHolder(WorldModel const& world, EnemyFacts const* enemy);
        // Healer-role bot that can act with the least mana; null if none.
        PartyMemberFacts const* FindLowestManaHealer(WorldModel const& world);

        // Overrides plan.focus (TargetAssigner's choice) with `enemy`; keeps sinceMs when the
        // focus does not change. Score fields come from FocusAssigner::Evaluate. Both hand the
        // bots their targets again from the new focus (RedistributeTargets).
        void SetFocus(WorldModel const& world, TeamPlan& plan, EnemyFacts const& enemy, char const* reason);
        void ClearFocus(WorldModel const& world, TeamPlan& plan);
        // TargetAssigner::Distribute after a boss module changed the plan (module "target" on).
        void RedistributeTargets(WorldModel const& world, TeamPlan& plan);

        // Cost of not healing `target` for two bot ticks (same as ThreatBehavior's heal veto):
        // the damage it takes meanwhile, plus its death penalty if it would not last.
        float HealDelayCost(WorldModel const& world, PartyMemberFacts const& target);

        Proposal MakeReplace(char const* source, ProposalTier tier, BotActionType type,
                             ObjectGuid target = ObjectGuid(), SpellEntry const* spell = nullptr);
        Proposal MakeHealVeto(char const* source, ObjectGuid target);

        // One intercept need (OfferDuties) to kill `e` now, worth `points`: bots that deal
        // damage, ranged within FocusAssigner::INTERCEPT_RANGE first, melee within
        // MELEE_INTERCEPT_RANGE, the closest first; never `skip` (the tank on the boss). On equal
        // points the one closest to `tieTo` first. Its EnemyHandled key keeps FocusAssigner's own
        // intercept of the same enemy from sending a second bot. No candidate: no need.
        void AddKillNeed(WorldModel const& world, EnemyFacts const& e, float points, char const* reason,
                         Position3 const& tieTo, ObjectGuid skip, std::vector<DutyNeed>& needs);
        static constexpr float KILL_NEED_RANGED_FIRST = 1000.0f;

        // An event in waves (boss doc G5): when anyone last fought (an enemy of ours or of an
        // ally, an ally in combat, a member in combat). The modules' "no sitting down between
        // waves" lets the party drink again after a long quiet (a wipe leaves the event on).
        struct WaveWatch
        {
            uint32 lastFightMs = 0;
            // Once per refresh; true while anyone fights.
            bool Update(WorldModel const& world);
            uint32 QuietMs(uint32 nowMs) const;
        };
    }
}

#endif
