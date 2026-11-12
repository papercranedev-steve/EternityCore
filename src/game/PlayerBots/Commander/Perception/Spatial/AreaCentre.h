/*
 * PartyBot Commander layer 1 (eyes): where to put the centre of a ground area spell, as the
 * user places a Blizzard (doc .kiro/docs/2026-11-12-mage-hold.md 3.2). Geometry only; the area
 * phase values the candidates with AreaValue and picks (Coordination/Doctrine/AreaPhase.h).
 *
 * NearSide (nothing runs at the caster yet): the centre on the line from the cast spot `from`
 * to `anchor` closest to `from` that keeps every one of `enemies` inside the area (its edge
 * MARGIN_YD inside the rim; without the margin when that is impossible). The farthest one ends
 * at the far rim and an enemy that turns walks through the whole area, slowed. On the line, for
 * an enemy at distance d from it, `along` yards along it, the centre at t yards keeps it inside
 * for t in along +- sqrt(r^2 - d^2) (r = radius + its bounding radius - margin); the centre is
 * at the largest lower end, not before `from`. False: no t keeps all of them (the anchor
 * itself always does without the margin when `enemies` are within the area around it).
 *
 * AheadOf (enemies run at the caster): the first of them, `lead`, inside the area near its
 * rear rim (radius + its bounding radius - MARGIN_YD from the centre, toward `to`); never past
 * `to`. The first tick comes after the spell's period (+ CAST_DELAY_SEC): by then it has walked
 * into the area and walks the rest of it slowed (user 2026-11-12: the lead inside, a little to
 * the back; not outside waiting for it to walk in).
 */

#ifndef MANGOS_COMMANDER_AREA_CENTRE_H
#define MANGOS_COMMANDER_AREA_CENTRE_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

namespace Commander
{
namespace AreaCentre
{
    // Inside the rim by this much: enemies shuffle, the cast lands a refresh later.
    static constexpr float MARGIN_YD = 1.0f;
    // From the plan to the spell's start: a brain refresh, the cast going out.
    static constexpr float CAST_DELAY_SEC = 0.3f;

    bool NearSide(Position3 const& from, Position3 const& anchor, std::vector<EnemyFacts const*> const& enemies,
                  float radius, Position3& out);

    Position3 AheadOf(EnemyFacts const& lead, Position3 const& to, float radius);
}
}

#endif
