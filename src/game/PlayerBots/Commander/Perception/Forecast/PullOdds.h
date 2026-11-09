/*
 * PartyBot Commander: can the party fight a pack where it stands, or should the tank bring it
 * back to the fight spot (PullPlanner)?
 *
 * Bringing it back keeps the fight away from other packs: what makes a pull go wrong is a
 * creature nearby joining (the tank pushed back, a bot walking past, a caster's pet...). A
 * strong party takes one or two more and does not need to; a weak one does. Rough on purpose
 * (user decision 2026-11-01): two numbers and one threshold.
 *
 *  Edge (levels)  - how far the party is above the pulled creatures: average member level
 *                   minus their average level, plus gear: average equipped item level over
 *                   the member's level + GEAR_NORM_OVER_LEVEL, GEAR_LEVELS_PER_ILVL levels per
 *                   item level. Tanks and healers count TANK_HEALER_WEIGHT times.
 *  Add risk       - hostile creatures not in the pull near where the fight would be: 1 per
 *                   normal, ELITE_RISK per elite, full within RISK_NEAR_YD of the pull target,
 *                   fading to 0 at RISK_FAR_YD.
 *  Fight there    - Edge - ADD_RISK_LEVELS x add risk >= FIGHT_THERE_MIN_EDGE.
 *
 * Map thread (reads the members' equipment).
 */

#ifndef MANGOS_COMMANDER_PULL_ODDS_H
#define MANGOS_COMMANDER_PULL_ODDS_H

#include "Commander/Perception/WorldModel.h"
#include <string>
#include <vector>

class Map;

namespace Commander
{
namespace PullOdds
{
    float const GEAR_NORM_OVER_LEVEL = 3.0f;    // quest and dungeon gear is a few levels above the wearer
    float const GEAR_LEVELS_PER_ILVL = 0.3f;    // 10 item levels over the norm ~ 3 levels
    float const TANK_HEALER_WEIGHT = 2.0f;
    float const ELITE_RISK = 3.0f;              // an elite add is about three normal ones
    float const RISK_NEAR_YD = 15.0f;
    float const RISK_FAR_YD = 30.0f;
    float const ADD_RISK_LEVELS = 2.0f;         // edge needed per normal-creature-sized risk
    float const FIGHT_THERE_MIN_EDGE = 0.0f;

    struct Edge
    {
        float levels = 0.0f;    // level part
        float gear = 0.0f;      // gear part (in levels)
        float avgItemLevel = 0.0f;
        float Total() const { return levels + gear; }
    };

    struct RiskSource
    {
        ObjectGuid guid;
        float distance = 0.0f;
        float weight = 0.0f;
        bool elite = false;
    };

    struct AddRisk
    {
        float total = 0.0f;
        std::vector<RiskSource> sources;    // largest weight first
    };

    // Party (pets excluded, dead members excluded) against the creatures in `pulled`.
    Edge PartyEdge(WorldModel const& world, Map* map, std::vector<ObjectGuid> const& pulled);

    // Idle hostile creatures (WorldModel::packs) not in `pulled`, around `at`.
    AddRisk NearbyAdds(WorldModel const& world, Position3 const& at, std::vector<ObjectGuid> const& pulled);

    bool FightThere(Edge const& edge, AddRisk const& risk);

    // "edge +1.0 levels (level +1.0, gear +0.0, item level 61), add risk 1.0 (Skeletal Guardian 9 yd)"
    std::string Describe(Map* map, Edge const& edge, AddRisk const& risk);
}
}

#endif
