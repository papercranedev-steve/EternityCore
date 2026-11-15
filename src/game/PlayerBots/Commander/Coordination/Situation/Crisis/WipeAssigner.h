/*
 * PartyBot Commander layer 2 (brain), situation: the fight is lost, keep the way back (user
 * 2026-11-15, "protect the embers": when nobody can run for the door, keep someone who rises
 * and resurrects the others once the enemies have gone home).
 *
 * Called (TeamPlan::wipe.active, kept until the fight is over) when, while the party fights:
 *   - the contingency has no bearable option (TeamPlan::crisis: the tank or the healers gone,
 *     its best option not FightLoad::Bearable, or none) for LOSE_CONFIRM_MS; or
 *   - a stand-in already took over (TeamPlan::emergencyRoles) and the fight as it is now
 *     ends with the tank dead (FightLoad::FromWorld, tankDies) for LOSE_CONFIRM_MS; or
 *   - the player ran and the retreat could not start (RetreatPlan::declineNoted).
 * Then, every refresh:
 *   revivers  members (players too) that rise by themselves and resurrect others
 *             (PartyMemberFacts::selfRes and resurrects): nothing to do but die where they
 *             stand; CorpseRun keeps the body and rises once it is safe, then the bots
 *             resurrect the others out of combat (PartyBotAI).
 *   stone     no reviver: a warlock bot with a Soulstone ready (soulstoneItem) puts it on a
 *             member who resurrects (healer bots first, then bots, then players); WipeBehavior.
 *   hiders    bots that are not revivers with a "wipe" threat drop ready (Feign Death, Vanish:
 *             ThreatDropAbility::wipe) drop out of the fight and stay out until it is over
 *             (WipeBehavior holds them; no target, no heal).
 * The leader gets one note per change: what is lost, who rises, "don't release".
 * Pure.
 */

#ifndef MANGOS_COMMANDER_WIPE_ASSIGNER_H
#define MANGOS_COMMANDER_WIPE_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class WipeAssigner : public IAssigner
    {
    public:
        static constexpr uint32 LOSE_CONFIRM_MS = 3000;

        char const* GetId() const override { return "wipe"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
