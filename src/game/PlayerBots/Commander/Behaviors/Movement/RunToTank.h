/*
 * PartyBot Commander layer 3: the walk of a bot with melee enemies on it to the tank, which
 * takes them over there (EscapeBehavior step 2, header there). Shared by EscapeBehavior and
 * the area caster's own room (AreaRoomBehavior, doc .kiro/docs/2026-11-12-mage-hold.md 3.4
 * step 3: Frost Nova what it can, then to the tank). Also the stop of the bot's own walk once
 * nobody chases it.
 */

#ifndef MANGOS_COMMANDER_RUN_TO_TANK_H
#define MANGOS_COMMANDER_RUN_TO_TANK_H

#include "Commander/CommanderTypes.h"
#include <vector>

class TeamBotAI;
class Unit;

namespace Commander
{
    struct WorldModel;
    struct EnemyFacts;
    struct PartyMemberFacts;

    namespace RunToTank
    {
        // Step 2 of EscapeBehavior for `chasers` (their hits are free for `absorbedSec`).
        // `fleeFrom`: the chaser FleePoint runs from when the way to the tank is blocked.
        // `source`: the proposing module (owner of the walk). False: no tank to run to, already
        // next to it, too far, or no way.
        bool Propose(TeamBotAI const* bot, WorldModel const& world, PartyMemberFacts const& self,
                     std::vector<EnemyFacts const*> const& chasers, Unit* fleeFrom, float absorbedSec,
                     ProposalTier tier, char const* source, std::vector<Proposal>& out);

        // The walk `source` started (BotDirectives::GetMovementOwner) is under way: stop it
        // where the bot is. False: not walking, or somebody else's walk.
        bool ProposeStop(TeamBotAI const* bot, char const* source, char const* reason, std::vector<Proposal>& out);
    }
}

#endif
