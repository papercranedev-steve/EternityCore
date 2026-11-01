/*
 * PartyBot Commander layer 1: which creatures join if `target` is pulled.
 * Uses the server's own checks, so it reads live objects (map thread or world thread
 * while maps are idle).
 *
 * Rules (Creature.cpp CallAssistance / AssistDelayEvent, CreatureGroups.cpp, BasicAI.cpp):
 *  - A creature that starts attacking calls assistance: creatures within
 *    CreatureFamilyAssistanceRadius that pass CanAssistTo and are in line of sight join
 *    after CreatureFamilyAssistanceDelay. Those helpers are flagged and do not call again.
 *  - creature_groups with OPTION_AGGRO_TOGETHER: all members join, and they do call
 *    assistance themselves.
 *  - Aggro range (only when `standAt` is given): the puller stands at `standAt` when it hits
 *    the target. Every other idle creature that would attack the puller at `standAt` on its
 *    own (AggroRange.h, the shared BasicAI::MoveInLineOfSight rule, range + AGGRO_RANGE_MARGIN)
 *    attacks too. It starts attacking, so
 *    it calls assistance too (combat scenarios 11 "Lord 522" and 13, table rows 10-11).
 *    Only the stand point is checked, not the way there.
 */

#ifndef MANGOS_COMMANDER_ASSIST_PREDICTOR_H
#define MANGOS_COMMANDER_ASSIST_PREDICTOR_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/AggroRange.h"
#include <vector>

class Creature;
class Unit;

namespace Commander
{
namespace AssistPredictor
{
    // Server aggro checks run on every movement update; the margin covers the last step
    // of the puller and small differences of the stand point.
    float const AGGRO_RANGE_MARGIN = 2.0f;
    // Search radius around the stand point for aggro-range joiners.
    float const AGGRO_SEARCH_RADIUS = AggroRange::MAX_ATTACK_DISTANCE;

    enum class JoinReason : uint8
    {
        Target,
        Assist,         // called for help by a creature that started attacking
        AggroTogether,  // creature_groups member of a joiner
        AggroRange,     // the puller stands within its aggro range
    };

    struct Joiner
    {
        ObjectGuid guid;
        JoinReason reason = JoinReason::Assist;
    };

    // Every creature expected in the fight, `target` included, with why it joins.
    // `standAt` = where the puller stands when it hits the target; null skips aggro range.
    std::vector<Joiner> PredictDetailed(Creature* target, Unit* puller, Position3 const* standAt);

    // Assistance and aggro-together only (no aggro range), guids only.
    std::vector<ObjectGuid> Predict(Creature* target, Unit* puller);

    std::vector<ObjectGuid> Guids(std::vector<Joiner> const& joiners);
    char const* ReasonName(JoinReason reason);
}
}

#endif
