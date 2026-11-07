/*
 * PartyBot Commander layer 1 (eyes): where a rogue can Sap a pulled creature from before the
 * tank opens (Sap doc .kiro/docs/2026-11-08-sap.md section 4, todo 10c).
 *
 * Server facts (doc section 2): Sap takes the rogue out of stealth (Improved Sap is not used,
 * user 2026-11-08), so after the cast every idle creature sees the rogue with its normal
 * aggro range. On the way there it is stealthed (Perception/StealthDetect.h).
 *
 * Find: candidates on a ring around the target at RING_RADIUS_YD inside the spell's melee
 * reach (target and rogue combat reach + MELEE_EXTRA_YD - RING_INSIDE_YD), first behind the
 * target, then TURNS away from there. A candidate is usable when
 *  - its ground is within MAX_HEIGHT_DIFF of the target's height;
 *  - no idle creature notices the stealthed rogue there (StealthDetect, the target included);
 *  - no idle creature but the target reaches it with its normal aggro range + AGGRO_MARGIN
 *    (SafeRoute::IdleCreatures: the server's rule with line of sight, random wanderers' circles):
 *    after the Sap the rogue stands there visible. The user (2026-11-08): when the target's
 *    own pack would see it, no Sap at all;
 *  - no patrol comes within its aggro range + AGGRO_MARGIN during Request::standSec;
 *  - the target is in its sight;
 *  - the stealthed walk there is not noticed (StealthDetect::Watchers::WalkIsSafe).
 * At most MAX_PATHS path searches per call. Facts only: PullControl (brain) decides.
 * Reads live creatures and pathfinds: map thread.
 */

#ifndef MANGOS_COMMANDER_SAP_SPOT_H
#define MANGOS_COMMANDER_SAP_SPOT_H

#include "Common.h"
#include "Commander/Perception/WorldModel.h"

class Creature;
class Map;
class Player;

namespace Commander
{
namespace SapSpot
{
    // Melee spell reach: both combat reaches + this (Unit::GetCombatReach based melee range).
    float const MELEE_EXTRA_YD = 4.0f / 3.0f;
    // The spot lies this far inside that reach (the target turns a little, the last step).
    float const RING_INSIDE_YD = 1.0f;
    float const MIN_RING_YD = 2.5f;     // above StealthDetect::COLLISION_YD + margin
    // From behind the target first: 0, +-45, +-90, +-135, 180 degrees off its back.
    float const TURNS[] = { 0.0f, 0.7854f, -0.7854f, 1.5708f, -1.5708f, 2.3562f, -2.3562f, 3.1416f };
    float const MAX_HEIGHT_DIFF = 3.0f;
    float const AGGRO_MARGIN = 2.0f;    // = ShotSpot::AGGRO_MARGIN
    float const MAX_DETOUR = 2.0f;
    float const SCAN_RADIUS = 80.0f;    // idle creatures and patrols looked at around the target
    uint32 const MAX_PATHS = 4;

    struct Request
    {
        Creature* target = nullptr;
        Player* rogue = nullptr;            // walks there from where it stands now
        int32 stealthSkill = 0;             // StealthDetect::StealthSkill
        float standSec = 0.0f;              // seconds it stands there before the fight
    };

    enum class Failure : uint8
    {
        // In check order: the furthest check some candidate reached.
        None,
        NoGround,
        Noticed,        // a creature notices the stealthed rogue at every candidate left
        Seen,           // after the Sap another creature would see the rogue there
        Patrol,
        NoSight,
        NoPath,         // no unnoticed walk
        Budget,
    };

    struct Spot
    {
        Position3 at;
        float walkYd = 0.0f;    // length of the walk there
    };

    bool Find(Map* map, Request const& request, Spot& out, Failure* why = nullptr);
    char const* FailureName(Failure failure);
}
}

#endif
