/*
 * PartyBot Commander layer 3 (cerebellum): a warlock bot keeps the damage curse the brain gave
 * it (TeamPlan::curses, Coordination/Duties/CurseAssigner.h) on its victim ("curse" module, the
 * assigner's id). Routine tier, valued by Perception/Values/CurseValue.h: the arbiter drops it
 * when it does not pay (trash that dies first).
 * Not while the curse is on it with more than REFRESH_MS left, nor while someone else's curse
 * of the same kind is on it (a player warlock's: they do not stack). The rotation casts no
 * curse of its own.
 */

#ifndef MANGOS_COMMANDER_CURSE_BEHAVIOR_H
#define MANGOS_COMMANDER_CURSE_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class CurseBehavior : public IBehavior
    {
    public:
        // Recast when its own curse has less left than this (a cast and the flight).
        static constexpr uint32 REFRESH_MS = 2000;

        char const* GetId() const override { return "curse"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
