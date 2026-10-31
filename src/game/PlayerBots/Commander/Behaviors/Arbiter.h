/*
 * PartyBot Commander layer 3: picks one proposal and executes it.
 * Highest tier wins, then highest score inside the tier. If execution fails
 * (spell not ready, target gone) the next proposal in the same order is tried.
 */

#ifndef MANGOS_COMMANDER_ARBITER_H
#define MANGOS_COMMANDER_ARBITER_H

#include "Commander/Registry.h"

class PartyBotAI;

namespace Commander
{
    struct WorldModel;
    struct TeamPlan;

    class Arbiter
    {
    public:
        // Returns true when an action was executed; the caller then skips the
        // original target selection, movement and class rotation for this tick.
        static bool Run(PartyBotAI* bot, WorldModel const& world, TeamPlan const& plan,
                        BehaviorList const& behaviors);

    private:
        static bool Execute(PartyBotAI* bot, Proposal const& proposal);
    };
}

#endif
