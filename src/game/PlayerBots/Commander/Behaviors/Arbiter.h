/*
 * PartyBot Commander layer 3: picks one proposal and executes it.
 * Value-scored proposals with benefit <= cost are dropped. Veto proposals are
 * registered in BotDirectives (the original rotation keeps running). Of the
 * replace proposals, the highest tier wins, then the highest score inside the tier.
 * ProposalTier::Override (retreat) hides every other module's replace proposal.
 * If execution fails (spell not ready, target gone) the next one in the same tier
 * is tried. Every candidate is written to the bot's decision log.
 */

#ifndef MANGOS_COMMANDER_ARBITER_H
#define MANGOS_COMMANDER_ARBITER_H

#include "Commander/Registry.h"
#include <string>

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

        // Vetoes last this many bot ticks, so they expire soon after the module stops proposing them.
        static uint32 const VETO_TICKS = 3;

    private:
        static bool Execute(PartyBotAI* bot, Proposal const& proposal);
        static std::string Describe(PartyBotAI const* bot, Proposal const& proposal);
    };
}

#endif
