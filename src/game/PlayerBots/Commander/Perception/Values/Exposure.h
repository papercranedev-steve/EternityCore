/*
 * PartyBot Commander layer 1 (eyes): what standing near enemies that fight someone else costs
 * a member (doc .kiro/docs/2026-11-10-position-field.md 16 step 4). Shared by the healer and
 * ranged stance modules, so both price "too close" the same way.
 *
 * An enemy that turns to the member (it gets aggro: a heal's threat, an overpull) reaches it
 * after gap / ENEMY_RUN_SPEED seconds; the tank takes it back within MAX_GAIN_SEC. The member
 * is exposed for max(0, MAX_GAIN_SEC - gap / ENEMY_RUN_SPEED) seconds:
 *   Loss = TURN_CHANCE x (Value::HitLoss(enemy, member, exposed) + Value::LostAction(member, exposed))
 * (hits taken and casts pushed back or stopped meanwhile). gap = to the enemy's edge.
 * Moved here from HealerStanceBehavior (its SpotLoss "enemies" term, step E).
 */

#ifndef MANGOS_COMMANDER_EXPOSURE_H
#define MANGOS_COMMANDER_EXPOSURE_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

namespace Commander
{
namespace Exposure
{
    float const TURN_CHANCE = 0.5f;         // an enemy turns to the member within the horizon
    float const ENEMY_RUN_SPEED = 7.0f;
    float const MAX_GAIN_SEC = 3.0f;

    // Gap from `p` to the edge of `e`, 2D.
    float Gap(EnemyFacts const& e, Position3 const& p);

    // One enemy at `gapYd` (header).
    float Loss(WorldModel const& world, PartyMemberFacts const& member, EnemyFacts const& e, float gapYd);

    // Every enemy of `enemies` around `p`.
    float LossAt(WorldModel const& world, PartyMemberFacts const& member,
                 std::vector<EnemyFacts const*> const& enemies, Position3 const& p);

    // A channel caster (cannot step away, 2026-11-12, doc .kiro/docs/2026-11-11-aoe-threat.md 8):
    // an enemy at `gapYd` that turns to it as the channel starts comes at `speed` (the spell's
    // slow included) and pushes it back from gap / speed on until the tank has it back
    // (MAX_GAIN_SEC later) or the channel of `channelSec` ends, whichever is first:
    //   exposed = max(0, min(channelSec, arrival + MAX_GAIN_SEC) - arrival)
    //   Loss = TURN_CHANCE x LostAction(member, exposed)
    // Only for cast spots before the fight, with no enemy to value (AreaSpot); once enemies are
    // there AreaValue's worst case prices the spot (doc .kiro/docs/2026-11-12-mage-hold.md 3.1).
    float ChannelLoss(WorldModel const& world, PartyMemberFacts const& member, float gapYd, float speed, float channelSec);

    // Enemies that can act, fight us (not only an ally) and do not hit `member`: the ones that
    // may turn to it. (Those hitting it already are the escape module's.)
    std::vector<EnemyFacts const*> Turnable(WorldModel const& world, ObjectGuid member);
}
}

#endif
