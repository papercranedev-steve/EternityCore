/*
 * PartyBot Commander layer 3: damage dealers follow FocusAssigner's plan.
 * Design: .kiro/docs/2026-11-01-focus-assignment.md
 *
 *  Intercept - the bot owns an intercept duty: Urgent `Attack` on that enemy.
 *  Focus     - the bot's target is not the party focus: Assigned `Attack` on the focus.
 *              Benefit = the bot's share of the points saved by killing the focus before
 *              the current target (w_f * t_c - w_c * t_f, FocusAssigner.h); cost = the time
 *              lost switching (and walking, for melee).
 *              Not proposed while a player focus mark (bot->m_marksToFocus) is on a live
 *              enemy, while the focus is out of reach (30 yd; melee: in reach or within
 *              10 yd), or while switching would put the bot at or near aggro on it (the
 *              same rule ThreatBehavior uses to stop, so the two do not undo each other).
 * Tanks and healers are not affected.
 */

#ifndef MANGOS_COMMANDER_FOCUS_BEHAVIOR_H
#define MANGOS_COMMANDER_FOCUS_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class FocusBehavior : public IBehavior
    {
    public:
        static constexpr float FOCUS_RANGE = 30.0f;
        // Melee only switches to a focus that is this close (or already in melee reach), so it
        // does not run to a caster standing at the back (scenario 2: ranged first).
        static constexpr float MELEE_FOCUS_RANGE = 10.0f;
        static constexpr float MELEE_REACH = 5.0f;
        static constexpr float RUN_YARDS_PER_SEC = 7.0f;

        char const* GetId() const override { return "focus"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;

    private:
        void ProposeIntercept(PartyBotAI const* bot, WorldModel const& world,
                              TeamPlan const& plan, std::vector<Proposal>& out) const;
        void ProposeFocus(PartyBotAI const* bot, WorldModel const& world,
                          TeamPlan const& plan, std::vector<Proposal>& out) const;
    };
}

#endif
