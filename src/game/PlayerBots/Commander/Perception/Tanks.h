/*
 * PartyBot Commander layer 1: who the tanks are. The one place every module asks.
 * Design: .kiro/docs/2026-11-01-roles-and-tanks.md.
 *
 * A tank is a member (not a pet) whose PartyMemberFacts::role is ROLE_TANK:
 *  - bots: their role (chosen by the user, or given by the "roles" assigner from
 *    RoleCapability and the party composition);
 *  - players: the role they chose (`.partybot setrole <role> <own name>`), else ROLE_TANK only
 *    when talents and gear say tank and no bot is a tank (WorldModelBuilder).
 * The stance is not part of it: a warrior switching to Defensive Stance by mistake is not a tank.
 *
 * Several tanks are allowed (two locked tanks, raids later). Order: best tank capability
 * first (RoleCapability::tank), then guid; the first is the main tank.
 */

#ifndef MANGOS_COMMANDER_TANKS_H
#define MANGOS_COMMANDER_TANKS_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

namespace Commander
{
    namespace Tanks
    {
        bool IsTank(PartyMemberFacts const& m);
        // Alive tanks, main tank first. botsOnly: only bots (the ones that can be ordered to pull).
        std::vector<PartyMemberFacts const*> Alive(WorldModel const& world, bool botsOnly = false);
        // The main tank, or nullptr.
        PartyMemberFacts const* Main(WorldModel const& world, bool botsOnly = false);
        // Somebody holds enemies for the party: an alive tank, or a tank-like ally (Sergeant Bly).
        bool Any(WorldModel const& world);
    }
}

#endif
