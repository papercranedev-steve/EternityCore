/*
 * PartyBot Commander layer 2 (brain), doctrine step: where the party fights (doc
 * .kiro/docs/2026-11-09-doctrine/2026-11-10-fight-place.md; todo 7 of that folder, user
 * 2026-11-10: option B, the choice is the doctrine's, the pull only carries it out).
 *
 * Two choices, both pure:
 *
 * Choose (before the pull; PullPlanner::Plan gathers the facts on the world thread and carries
 * the choice out):
 *   Corner     the player wants corner fights (PullPlace::Corner, a test switch);
 *   BringBack  the pack stands farther than PullPlanner::RETURN_MIN_DISTANCE from the leader,
 *              the party should not fight it there (PullOdds::FightThere) and the leader's spot
 *              has less add risk;
 *   Corner     the fight estimate is hard (IsHard: FightLoad says the tank drops below
 *              CORNER_TANK_PCT, the healers end below CORNER_MANA_LEFT_SHARE of their mana, or
 *              it is not bearable) and the pack is not brought back (user 2026-11-07: Auto takes
 *              the corner only under high pressure);
 *   Here       otherwise.
 *
 * LureTargets (in the fight, TacticalDoctrine every refresh while the pull is in Settle and the
 * line of sight break was not searched yet): the enemies the tank hides from so they walk to it.
 *   1. Pulled enemies on the tank standing back casting (melee off, or a main ranged spell and
 *      standing still) more than LOS_CASTER_MIN_DISTANCE away;
 *   2. none and the pull is a corner fight: the pulled enemies on the tank farther than that,
 *      the casters first, then the farthest (they bunch up round the corner).
 *   At most LOS_MAX_CASTERS (the point must hide from each of them).
 *   TacticalDoctrine stamps them STAMP_LURE (OutlierTactic::Lure); the pull's map step
 *   (PullPlanner::SearchLosBreak) looks for the point. No point (PullLosBreak::NoPoint): the
 *   doctrine falls back to OutlierTactic::KillFirst (scratchpad 剧本1: 方法1 line of sight, then
 *   方法2/3 kill the casters first).
 */

#ifndef MANGOS_COMMANDER_FIGHT_PLACE_H
#define MANGOS_COMMANDER_FIGHT_PLACE_H

#include "Commander/Coordination/Situation/Pull/PullPlace.h"
#include "Commander/Perception/Forecast/FightLoad.h"
#include "ObjectGuid.h"
#include <string>
#include <vector>

namespace Commander
{
    struct PullPlan;

    namespace FightPlace
    {
        float const CORNER_TANK_PCT = 50.0f;
        float const CORNER_MANA_LEFT_SHARE = 0.25f;
        // Lure: enemies closer than this to the tank are left alone.
        float const LOS_CASTER_MIN_DISTANCE = 10.0f;
        uint32 const LOS_MAX_CASTERS = 2;

        enum class Kind : uint8
        {
            Here,
            BringBack,
            Corner,
        };

        struct Input
        {
            PullPlace wanted = PullPlace::Auto;     // the leader's choice (`.partybot ai pullplace`)
            bool distant = false;                   // the pack is farther than RETURN_MIN_DISTANCE
            bool fightThere = false;                // PullOdds::FightThere at the pack
            float packRisk = 0.0f;                  // PullOdds::AddRisk::total at the pack
            float spotRisk = 0.0f;                  // and at the leader's spot
            bool haveEstimate = false;              // FightLoad::Build succeeded
            FightLoad::Result estimate;
        };

        struct Choice
        {
            Kind place = Kind::Here;
            bool hard = false;                      // IsHard(estimate)
            std::string why;
        };

        Choice Choose(Input const& in);
        bool IsHard(FightLoad::Result const& r);
        char const* Name(Kind place);

        struct Lure
        {
            std::vector<ObjectGuid> targets;
            bool corner = false;                    // the corner fight's targets, not casters standing back
        };
        Lure LureTargets(WorldModel const& world, PullPlan const& pull, PartyMemberFacts const& tank);
    }
}

#endif
