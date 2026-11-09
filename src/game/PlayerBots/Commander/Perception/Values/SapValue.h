/*
 * PartyBot Commander layer 1 (eyes): what a Sap before the pull is worth, in points, failure
 * included (Sap doc .kiro/docs/2026-11-08-sap.md section 3, todo 10c). A fact for PullControl
 * (brain): "if this rogue Saps that creature from that spot, this happens".
 *
 *   net = hit x benefit - cost - (1 - hit) x (failure + disruption)
 *
 *   hit         1 - SpellCaster::GetMeleeMissChance (Sap is a melee spell that cannot be
 *               dodged, parried or blocked: only a miss; level and weapon skill against the
 *               creature's defense, +hit gear). Immunity is ControlValue::Affects (the caller).
 *   holdSec     expected seconds it stays sapped once the fight starts: the server rolls
 *               every 5 s against MagicSpellHitChance and ends it early on a miss (PvE
 *               heartbeat, auras over 10 s), minus `tankTravelSec` (the tank walks to its
 *               opener after the Sap lands).
 *   benefit, cost  ControlValue::OfPull with that duration.
 *   failure     worst case (user 2026-11-08: a failed Sap is dangerous, count it high):
 *               the target and every other expected creature hit the rogue until the tank
 *               takes them, no heals: FightLoad::FoeOf on the rogue x DAMAGE_MARGIN for
 *               TAKEOVER_REACT_SEC + the tank's walk to the spot (to melee reach); the
 *               rogue's death penalty when its health does not last.
 *   disruption  the fight happens at the Sap spot instead of the planned fight spot: the
 *               idle creatures near it (PullOdds::NearbyAdds; its distance fade 0..1 as the
 *               chance, health and damage scaled by it) joining after
 *               ControlValue::ADD_ARRIVE_SEC, Loss with them - Loss without,
 *               at the Sap spot minus at the planned spot (at least 0).
 *
 * Reads the rogue and the creatures live (hit chances, damage fields): map thread.
 */

#ifndef MANGOS_COMMANDER_SAP_VALUE_H
#define MANGOS_COMMANDER_SAP_VALUE_H

#include "Commander/Perception/Values/ControlValue.h"
#include "Commander/Perception/Forecast/FightLoad.h"
#include "Commander/Perception/WorldModel.h"
#include <string>
#include <vector>

class Creature;
class Map;
class Player;

namespace Commander
{
namespace SapValue
{
    float const HEARTBEAT_SEC = 5.0f;           // Aura heartbeat resist on creatures (SpellAuras.cpp)
    float const HEARTBEAT_MIN_SEC = 10.0f;      // only auras longer than this roll it
    float const TAKEOVER_REACT_SEC = 1.0f;
    float const RUN_YARDS_PER_SEC = 7.0f;
    float const MELEE_REACH_YD = 5.0f;

    struct Worth
    {
        ControlValue::Worth control;    // benefit and cost with holdSec
        float hit = 0.0f;
        float holdSec = 0.0f;
        float failure = 0.0f;
        float disruption = 0.0f;

        float Net() const
        {
            return hit * control.Benefit() - control.Cost() - (1.0f - hit) * (failure + disruption);
        }
    };

    struct Input
    {
        Player* rogue = nullptr;
        Creature* target = nullptr;
        Position3 spot;
        std::vector<ObjectGuid> expected;   // the pull's creatures, target included
        bool hasPlannedSpot = false;
        Position3 plannedSpot;              // where the fight was planned (PullPlan::PlannedFightSpot)
        float tankTravelSec = 0.0f;         // from the Sap until the fight starts
        uint32 nearTarget = 0;              // as ControlValue::OfPull
    };

    // `fight` = FightLoad::Build of the expected creatures, `asIs` its Simulate.
    Worth Of(WorldModel const& world, Map* map, FightLoad::Input const& fight, FightLoad::Result const& asIs,
             PartyMemberFacts const& member, ControlAbility const& a, Input const& in);

    // Upper bound before a spot is known (sorting the options): benefit at the full hit
    // chance and duration, no failure.
    float Optimistic(WorldModel const& world, FightLoad::Input const& fight, FightLoad::Result const& asIs,
                     PartyMemberFacts const& member, ControlAbility const& a, ObjectGuid target, uint32 nearTarget);

    // "net +3.1 (hit 92%, 21 s held: benefit 6.0 cost 0.8 | failure 18.4 disruption 2.0)"
    std::string Describe(Worth const& w);
}
}

#endif
