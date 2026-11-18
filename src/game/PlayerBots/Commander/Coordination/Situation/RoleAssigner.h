/*
 * PartyBot Commander layer 2: who tanks, who heals, who deals damage ("roles" assigner).
 * Design: .kiro/docs/2026-11-01-roles-and-tanks.md.
 *
 * Input: every member's RoleCapability (talents, gear, known spells) and whether its role is
 * locked (chosen by the user: bots with .partybot setrole / load / add <role>, players with the
 * role button on their own character). Output: TeamPlan::roles, a role for every bot whose role
 * is not locked; PartyCommander::ApplyRoles sets it (TeamBotAI::AssignRole) and tells the leader.
 *
 * Needs: NEED_TANKS_SMALL tank and NEED_HEALERS_SMALL healer in a party of up to 5; larger
 * groups NEED_TANKS_RAID tanks and one healer per 5 members. Locked members fill needs first
 * (players only when locked: an unlocked player's role is a guess).
 *
 * Choice (party of up to 5): every (tank, healer) pair of unlocked bots is tried, including
 * "nobody" when the need is already met or nobody qualifies (tank >= MIN_TANK, heal >= MIN_HEAL):
 *   score = tank score + heal score + sum of the others' best non-tank score
 *           - MISSING_ROLE for every need left open,
 *   each score + INCUMBENT_BONUS when it is the bot's current role (no flipping between equals).
 * So a holy paladin with a shield is not made the tank when it is the only healer.
 * Larger groups: greedy, best tank first, then best healer (raids come later).
 * Everyone else: RoleCapability::BestNonTank (a second priest heals too).
 *
 * When: not while the party fights, pulls, clears or retreats (the previous result is kept), so
 * a role only changes between fights.
 */

#ifndef MANGOS_COMMANDER_ROLE_ASSIGNER_H
#define MANGOS_COMMANDER_ROLE_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class RoleAssigner : public IAssigner
    {
    public:
        static constexpr uint32 SMALL_PARTY = 5;
        static constexpr uint32 NEED_TANKS_SMALL = 1;
        static constexpr uint32 NEED_HEALERS_SMALL = 1;
        static constexpr uint32 NEED_TANKS_RAID = 2;
        static constexpr float MISSING_ROLE = 1.0f;
        static constexpr float INCUMBENT_BONUS = 0.1f;

        char const* GetId() const override { return "roles"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
