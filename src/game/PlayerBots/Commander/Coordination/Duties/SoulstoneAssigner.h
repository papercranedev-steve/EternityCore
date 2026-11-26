/*
 * PartyBot Commander layer 2: whom each warlock bot gives its Soulstone out of combat
 * ("soulstone" module, review 2026-11-26; before, WarlockRotation picked the party's healer
 * itself: by role, and by a guess of the player's role).
 *
 * Who gets one (TargetRank, also the wipe's stone, Situation/Crisis/WipeAssigner.h): an alive
 * member, not a pet, that cannot rise by itself yet (PartyMemberFacts::selfRes: a stone on it,
 * Reincarnation ready). By function, not by role:
 *   5  a bot that heals the team (HealsTeam) and resurrects others
 *   4  another bot that resurrects others
 *   3  a player who resurrects others
 *   2  a player who does not (it saves him the corpse run)
 *   1  a bot that does not (a lone warlock: itself)
 * Ties: guid order.
 *
 * Stones wanted: one per warlock of the team, the stones already on members (soulstoned, any
 * warlock's) count as given. The warlock bots in order (stone ready first, then guid) each take
 * the best member nobody took, while stones are still wanted. A warlock whose stone is on
 * cooldown or who has not learned it still gets its member: the rotation says why that member
 * goes without (party voice), once per reason.
 * Tasks stage like CurseAssigner (no DutyAllocator: no primary duty); runs every refresh.
 */

#ifndef MANGOS_COMMANDER_SOULSTONE_ASSIGNER_H
#define MANGOS_COMMANDER_SOULSTONE_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    struct WorldModel;
    struct PartyMemberFacts;

    class SoulstoneAssigner : public IAssigner
    {
    public:
        // How much a Soulstone on `m` is worth (header); 0 = not for it.
        static int TargetRank(PartyMemberFacts const& m);
        // Rank of a member who resurrects the others afterwards (the wipe's stone needs one).
        static int constexpr RESURRECTS_RANK = 3;

        char const* GetId() const override { return "soulstone"; }
        AssignStage GetStage() const override { return AssignStage::Tasks; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
