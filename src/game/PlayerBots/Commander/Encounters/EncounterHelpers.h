/*
 * PartyBot Commander: small helpers shared by boss modules (pure functions on facts / plan).
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_HELPERS_H
#define MANGOS_COMMANDER_ENCOUNTER_HELPERS_H

#include "Common.h"
#include "Commander/CommanderTypes.h"

namespace Commander
{
    struct WorldModel;
    struct TeamPlan;
    struct EnemyFacts;
    struct PartyMemberFacts;

    namespace EncounterHelpers
    {
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
    }
}

#endif
