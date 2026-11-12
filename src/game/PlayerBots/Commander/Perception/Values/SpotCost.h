/*
 * PartyBot Commander layer 1 (eyes): what standing at a point costs a member, in points
 * (doc .kiro/docs/2026-11-11-spot-scoring.md). One function per term; the stance modules
 * (cerebellum) pick the terms they need, add them and sort their candidate cells. The
 * enemies' exposure term is Exposure.h.
 *
 * All terms are continuous where the game is (doc section 4): a step of the game (spell range)
 * is priced as what fixing it costs, not a fixed penalty. All terms of one choice use the same
 * horizon H (seconds the member stays there).
 *
 * Walk(member, sec)       = Value::LostAction(member, sec): a cast-time spell stops the walk.
 *
 * Hazard zones (HazardZones.h), moved from PositioningBehavior (2026-11-11, step 1):
 *  ZoneHits(zone, H, leaveSec):
 *    leaveSec < 0: hits taken standing in `zone` for H; leaveSec >= 0: hits saved by walking
 *    out of it in leaveSec.
 *    Pulse, or not casting: H / interval.
 *    Casting: (H - cast remaining) / interval later hits, + 1 for the cast in progress when
 *    standing, or when the walk is out DODGE_SLACK_SEC before it lands (Dodges).
 *  AddZoneLoss: hits x hitChance x HazardZones::HitPoints points, hits x hitChance x damage.
 *  Hazard(member, role, zones, p, H): every zone dangerous to `roleBit` that contains `p`
 *    (HazardZones::DistanceTo <= radius), ZoneHits(H, -1) each; + Value::DeathPenalty when the
 *    damage reaches the member's health.
 *
 * TargetReach(member, at, target, targetReach, range, inSight, speed, H): what the member loses
 * at `at` for not reaching its target (a ranged victim):
 *    in sight and within `range` of the target's edge: 0;
 *    in sight, `excess` yards too far: LostAction(min(H, excess / speed)) (the walk in);
 *    out of sight: LostAction(H) (no estimate of how far a cell in sight is).
 *
 * Pure: WorldModel facts. Map thread of the party.
 */

#ifndef MANGOS_COMMANDER_SPOT_COST_H
#define MANGOS_COMMANDER_SPOT_COST_H

#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Spatial/HazardZones.h"
#include <vector>

namespace Commander
{
namespace SpotCost
{
    float const DODGE_SLACK_SEC = 0.3f;     // out this long before an area cast lands

    struct HazardLoss
    {
        float points = 0.0f;
        float damage = 0.0f;                // health, for the death check
        bool lethal = false;                // Hazard only: damage reaches the member's health
    };

    float Walk(WorldModel const& world, PartyMemberFacts const& member, float walkSec);

    // A walk of `leaveSec` gets out before the cast in progress lands.
    bool Dodges(HazardZone const& zone, float leaveSec);
    float ZoneHits(HazardZone const& zone, float horizonSec, float leaveSec);
    void AddZoneLoss(WorldModel const& world, HazardZone const& zone, PartyMemberFacts const& member,
                     float hits, HazardLoss& loss);
    HazardLoss Hazard(WorldModel const& world, PartyMemberFacts const& member, uint8 roleBit,
                      std::vector<HazardZone> const& zones, Position3 const& p, float horizonSec);

    float TargetReach(WorldModel const& world, PartyMemberFacts const& member, Position3 const& at,
                      Position3 const& target, float targetReach, float rangeYd, bool inSight,
                      float speed, float horizonSec);

    // Breaks a hold (battlefield groups doc .kiro/docs/2026-11-09-doctrine/2026-11-13-battlefield-groups.md
    // 8.1), a term of the fight spot (where the main tank keeps the pile): every enemy held by
    // a control that breaks on damage and is not a fear (ActiveControl: polymorph, shackle,
    // sap; anyone's) within `clearYd` (ControlValue::ClearOfPileYd) of the spot costs what
    // losing that control costs (ControlValue::HeldWorth). A stun or banish costs nothing.
    // HeldMezzes values them once per refresh (one fight simulation each); BreaksHold is then
    // a distance check per candidate.
    struct HeldMez
    {
        ObjectGuid guid;
        Position3 pos;
        float worth = 0.0f;
    };
    std::vector<HeldMez> HeldMezzes(WorldModel const& world);
    float BreaksHold(std::vector<HeldMez> const& held, Position3 const& spot, float clearYd);
}
}

#endif
