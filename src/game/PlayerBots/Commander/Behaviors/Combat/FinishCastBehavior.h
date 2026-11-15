/*
 * PartyBot Commander layer 3 (cerebellum): a cast that will land on a dead enemy is worth
 * nothing (user 2026-11-15). The cast in progress (a single target damage spell with a cast
 * time) is wasted when the others kill its target before it lands
 * (Perception/Forecast/FinishForecast.h, live: time since the refresh + cast left + flight).
 *
 *   ContinueValue     0 for such a cast (CurrentAction::keep: stopping it loses nothing), else
 *                     not this module's (-1).
 *   CastBreakBenefit  such a cast while the brain already gave the bot another enemy
 *                     (TargetDuty, DamageTargets "finishing"): the time left of the cast, which
 *                     it would spend for nothing (Value::LostAction). No other enemy: 0, the
 *                     cast goes on (nothing better to do with the time).
 * Starting such a cast is refused by Constraints::WastedCast. Proposes nothing.
 */

#ifndef MANGOS_COMMANDER_FINISH_CAST_BEHAVIOR_H
#define MANGOS_COMMANDER_FINISH_CAST_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class FinishCastBehavior : public IBehavior
    {
    public:
        char const* GetId() const override { return "finishcast"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* /*bot*/, WorldModel const& /*world*/,
                     TeamPlan const& /*plan*/, std::vector<Proposal>& /*out*/) const override {}
        float CastBreakBenefit(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                               uint32 castLeftMs, char const*& reason) const override;
        float ContinueValue(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                            SpellEntry const* spell, char const*& reason) const override;
    };
}

#endif
