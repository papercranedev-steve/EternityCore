/*
 * PartyBot Commander layer 3 (cerebellum): a class fear break that also makes the caster immune
 * to fear for a while (warrior Berserker Rage, 10 s, Berserker Stance only) is cast before a
 * sure fear on the bot (its own IncomingTimeline::Lockout with MECHANIC_FEAR) that lands within
 * that time (boss doc G8). Afterwards, already feared, TeamBotAI::BreakControl still casts it.
 * Not a stance change for it: a tank in Defensive Stance does not leave it.
 * Assigned tier: value = the control it saves (Value::LostAction over the fear), cost = one
 * global cooldown.
 */

#ifndef MANGOS_COMMANDER_FEAR_BREAK_AHEAD_BEHAVIOR_H
#define MANGOS_COMMANDER_FEAR_BREAK_AHEAD_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class FearBreakAheadBehavior : public IBehavior
    {
    public:
        // The fear must land at least this far off (the cast goes first).
        static constexpr float LEAD_SEC = 0.2f;

        char const* GetId() const override { return "fear break ahead"; }
        bool AppliesTo(TeamBotAI const* bot) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
