/*
 * PartyBot Commander layer 3 (per-bot behaviors): one module per concern.
 * Behaviors are stateless and shared by all bots on all map threads,
 * so Propose / Adjust must not write to member variables.
 */

#ifndef MANGOS_COMMANDER_IBEHAVIOR_H
#define MANGOS_COMMANDER_IBEHAVIOR_H

#include "Commander/CommanderTypes.h"
#include <vector>

class PartyBotAI;

namespace Commander
{
    struct WorldModel;
    struct TeamPlan;

    class IBehavior
    {
    public:
        virtual ~IBehavior() = default;

        // Short id used by config (PartyBot.Commander.DisabledModules) and GM commands.
        virtual char const* GetId() const = 0;

        // Which bots this module is relevant for (role, class).
        virtual bool AppliesTo(PartyBotAI const* bot) const = 0;

        // Map thread, every combat tick of the bot: propose zero or more actions.
        virtual void Propose(PartyBotAI const* bot, WorldModel const& world,
                             TeamPlan const& plan, std::vector<Proposal>& out) const = 0;

        // True = this module decides when the bot taunts; the original rotation's
        // "taunt whatever attacks someone else" is suppressed (BotDirectives::IsTauntControlled).
        virtual bool ControlsTaunt(PartyBotAI const* /*bot*/) const { return false; }

        // Optional: rescore or drop proposals made by other modules.
        virtual void Adjust(PartyBotAI const* /*bot*/, WorldModel const& /*world*/,
                            std::vector<Proposal>& /*proposals*/) const {}
    };
}

#endif
