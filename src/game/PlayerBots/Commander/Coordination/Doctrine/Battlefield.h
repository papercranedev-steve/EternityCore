/*
 * PartyBot Commander layer 2 (brain), doctrine step: the battlefield split into groups (doc
 * .kiro/docs/2026-11-09-doctrine/2026-11-13-battlefield-groups.md 2, 3, 6; user 2026-11-13).
 * The groups are made once, before the pull from its plan, and kept by guid into the fight
 * (TacticalDoctrine carries DoctrinePlan::groups from refresh to refresh); an enemy with no
 * group yet gets one when it shows up. Before, the doctrine grouped by distance every refresh:
 * a heavy patrol 10 yd away was Special, one step closer it was the pile's and NoMez.
 *
 * Two axes: where (with the pile or away) and how it fights (Tags::Engage).
 *
 * Before the pull (FromPull), the creatures of pull.expected:
 *   Pile             the melee ones (Tags::EngageOfEntry) linked to the pull target within
 *                    CLUSTER_YD of each other (from the target; a target standing back: the
 *                    nearest melee one); its heavy one (Tags::Tier Heavy) noted: boss + adds.
 *   MeleeStraggler   the other melee ones.
 *   RangedStraggler  the ones standing back.
 * AddPatrols: every idle patrol (PatrolZone::Counts) not expected that comes to the planned
 *   fight spot within the caller's horizon (PatrolZone::ArriveSec): a Patrol group each.
 * In the fight (Place), an enemy with no group:
 *   no group at all yet (a fight that began without a pull plan): by place only;
 *   else by source first (Tags::SourceOf): a patrol walking in: a new Patrol group; a death
 *   spawn, a summon, the called, a trap swarm: the Reinforcement group; else by place;
 *   by place: within CLUSTER_YD of the main tank (from its edge): Pile; melee: MeleeStraggler;
 *   standing back: RangedStraggler.
 * Joining: a member of another group that comes within CLUSTER_YD of the main tank is the
 *   pile's from then on (DoctrineGroup::joined, one way: a pile enemy that steps back stays).
 *
 * The group stamps (StampOf), what the duties read:
 *   IN_CLUSTER  the pile, the joined, and melee enemies on their way (they are the pile soon);
 *   SPECIAL     a heavy one of another group not joined yet;
 *   OUTLIER     the rest (standing back away from the pile).
 * Pure (facts only).
 */

#ifndef MANGOS_COMMANDER_BATTLEFIELD_H
#define MANGOS_COMMANDER_BATTLEFIELD_H

#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Profiles/Tags.h"
#include <string>
#include <utility>
#include <vector>

namespace Commander
{
namespace Battlefield
{
    // A creature of the pull's plan (no live facts: it is not in the fight yet).
    struct Unit
    {
        ObjectGuid guid;
        uint32 entry = 0;
        Position3 pos;
        bool elite = false;
        uint32 maxHealth = 0;
    };

    std::vector<DoctrineGroup> FromPull(WorldModel const& world, std::vector<Unit> const& expected,
                                        ObjectGuid target, uint32 nowMs);
    void AddPatrols(WorldModel const& world, Position3 const& spot, float withinSec,
                    std::vector<ObjectGuid> const& expected, uint32 nowMs, std::vector<DoctrineGroup>& groups);

    // The group of `e`, made when it has none (header: in the fight). Index into `groups`.
    int Place(WorldModel const& world, EnemyFacts const& e, PartyMemberFacts const& tank,
              bool byPlaceOnly, uint32 nowMs, std::vector<DoctrineGroup>& groups);
    // Notes `e` joined when it is at the tank (header: joining). True = in the pile.
    bool UpdateJoined(EnemyFacts const& e, PartyMemberFacts const& tank, DoctrineGroup& g);

    int Find(std::vector<DoctrineGroup> const& groups, ObjectGuid guid);
    uint32 StampOf(GroupKind kind, bool joined, Engage engage, Tier tier);

    // Before the pull: the expected creatures the doctrine will stamp NoMez once the fight
    // starts, by the fight's rules: fodder, and the pile's (IN_CLUSTER) when there are area
    // dealers and at least TacticalDoctrine::AREA_MIN of them. `why` per guid.
    std::vector<std::pair<ObjectGuid, char const*>> PullNoMez(WorldModel const& world,
        std::vector<Unit> const& expected, ObjectGuid target);

    char const* Name(GroupKind kind);
    // "pile (boss + adds), 4, planned: area pile" (the tactic from the plan's pile / outlier tactic)
    std::string Describe(DoctrinePlan const& d, DoctrineGroup const& g);
}
}

#endif
