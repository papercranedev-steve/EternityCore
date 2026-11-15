/*
 * PartyBot Commander layer 3 (cerebellum): carries out the flee stop the brain reserved for
 * this bot (TeamPlan::fleeStops, Coordination/Duties/FleeStopAssigner.h, todo 13). The brain refreshes
 * about once a second, so the trigger is checked here, live, every tick:
 *   hold (stun / root)  the enemy runs, or its health is at or below FleeStopDuty::triggerPct;
 *                       and the party still kills it within holdSec from its live health
 *                       (Value::DealtDps): otherwise the stun only delays the run
 *   slow                the enemy runs
 *   catch-up            the enemy runs, is still the bot's victim and out of its melee reach
 *                       (Sprint, Dash on itself)
 * and the enemy is not under a stun / root already. Then cast the spell on it (an area spell
 * around the bot: in its radius). Assigned tier, value = the duty's net + one global cooldown,
 * cost = that global cooldown (as the rescue and crowd control behaviors).
 * Before the trigger: nothing here; the spell is kept by TeamPlan::HoldsSpell.
 * WantsTickNow while the trigger is close (health within WAKE_MARGIN_PCT above it, or it runs).
 */

#ifndef MANGOS_COMMANDER_FLEE_STOP_BEHAVIOR_H
#define MANGOS_COMMANDER_FLEE_STOP_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class FleeStopBehavior : public IBehavior
    {
    public:
        static constexpr float WAKE_MARGIN_PCT = 3.0f;

        char const* GetId() const override { return "fleestop"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
        bool WantsTickNow(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;
    };
}

#endif
