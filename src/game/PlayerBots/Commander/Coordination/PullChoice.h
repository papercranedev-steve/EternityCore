/*
 * PartyBot Commander layer 2 (brain): which pack member the tank pulls and how it opens
 * (walk in, a shot from a shot spot, a face pull), and the new choice when the tank cannot
 * shoot from its spot. Used by PullPlanner (Plan, RecheckBeforeApproach). Map thread.
 * Split out of PullPlanner.cpp (todo item 14), behaviour unchanged.
 * Design: .kiro/docs/2026-11-01-pull-planning.md; ranking in PullPlanner.h.
 */

#ifndef MANGOS_COMMANDER_PULL_CHOICE_H
#define MANGOS_COMMANDER_PULL_CHOICE_H

#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/ShotSpot.h"
#include <string>
#include <vector>

class Creature;
class Player;
class Map;

namespace Commander
{
    namespace PullDetail
    {
        // One way to pull one candidate.
        struct Option
        {
            Creature* pullTarget = nullptr;
            std::vector<ObjectGuid> expected;
            std::vector<ObjectGuid> aggroAdds;      // subset of expected joining by aggro range
            uint32 elites = 0;
            float distance = 0.0f;
            bool caster = false;                    // stands back and casts (LosBreak::IsRangedCaster)
            bool ranged = false;                    // opens with a shot from `spot`
            bool face = false;                      // face pull from `spot`
            std::string rangedWhy;                  // why it opens from a distance
            ShotSpot::Spot spot;                    // ranged / face: where the tank opens from
            // Opening from a distance was wanted but no spot works: walks in instead.
            bool noSpot = false;
            ShotSpot::Failure noSpotWhy = ShotSpot::Failure::None;
            // Walking in would bring this many by aggro range and the tank has no ranged pull.
            uint32 meleeAggroAddsNoRanged = 0;
        };

        // How the tank may open from a distance (the leader's PullMethod).
        struct Openers
        {
            uint32 shotSpell = 0;       // 0 = no shot
            float shotRange = 0.0f;
            bool face = false;
            // The player chose the method: use it whenever there is a spot, even with no
            // reason for it or when it brings more.
            bool forced = false;
        };

        // Opening from a distance is used when the player chose it (Openers::forced) or for a
        // reason: the target stands back and casts, walking in would bring more by aggro range,
        // or a creature the prediction does not bring stands next to it. A shot or a face pull,
        // whichever brings fewer, then fewer elites; a shot on a tie (more room to the aggro
        // ranges). It needs a spot (`avoid`: spots that failed live); none: walks in (`noSpot`,
        // ranked after the candidates that have one).
        Option Evaluate(WorldModel const& world, Map* map, Creature* c, Player* tank, Openers const& op,
                        std::vector<Position3> const& avoid = std::vector<Position3>());

        // Best pull among `candidates` (target choice in PullChoice.cpp, Better), leaving out
        // `excluded`. Stealthed members are skipped (the tank cannot target them and would walk
        // for 20 seconds) and counted.
        Option ChooseOption(WorldModel const& world, Map* map, PackFacts const* pack,
                            std::vector<ObjectGuid> const& candidates, std::vector<ObjectGuid> const& excluded,
                            Player* tank, Openers const& op, uint32& stealthed);

        // No shot spot works for the pull's target. Another pack member that has a spot becomes
        // the target (at most PullPlanner::MAX_TARGET_SWITCHES); none: the tank walks in on the
        // current target. Either way a new Approach and a full recheck.
        void ReplaceUnshootableTarget(TeamPlan& plan, WorldModel const& world, Map* map, Player* tank,
                                      Creature* target, char const* why, uint32 nowMs);

        // The tank reported that it cannot shoot from its spot (PullShotState::spotFailed).
        // Another spot for the same target (not a failed one, at most
        // PullPlanner::MAX_SPOT_FAILURES), else another target (ReplaceUnshootableTarget).
        void HandleSpotFailure(TeamPlan& plan, WorldModel const& world, Map* map, uint32 nowMs);
    }
}

#endif
