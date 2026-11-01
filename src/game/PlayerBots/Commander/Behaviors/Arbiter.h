/*
 * PartyBot Commander layer 3: picks one proposal and executes it.
 * Value-scored proposals with benefit <= cost are dropped. Veto proposals are
 * registered in BotDirectives (the original rotation keeps running). Of the
 * replace proposals, the highest tier wins, then the highest score inside the tier.
 * ProposalTier::Override (retreat) hides every other module's replace proposal.
 * If execution fails (spell not ready, target gone) the next one in the same tier
 * is tried. Every candidate is written to the bot's decision log.
 * UseObject (gameobject target) returns true both while walking and on use, so the original
 * rotation and movement stay off until the module stops proposing it.
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

        // UseObject walks to this many yards in front of the object (inside INTERACTION_DISTANCE).
        static constexpr float USE_APPROACH_YARDS = 2.5f;

    private:
        static bool Execute(PartyBotAI* bot, Proposal const& proposal);
        // UseObject: walk into INTERACTION_DISTANCE (movement held), then GameObject::Use(me).
        // True while walking or after the use; false if the object is gone or the bot is rooted.
        static bool ExecuteUseObject(PartyBotAI* bot, BotAction const& action);
        static std::string Describe(PartyBotAI const* bot, Proposal const& proposal);
    };
}

#endif
