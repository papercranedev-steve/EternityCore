/*
 * PartyBot Commander layer 1: which creatures join if `target` is pulled.
 * Uses the server's own checks, so it reads live objects (map thread or world thread
 * while maps are idle).
 *
 * Rules (Creature.cpp CallAssistance / AssistDelayEvent, CreatureGroups.cpp):
 *  - A creature that starts attacking calls assistance: creatures within
 *    CreatureFamilyAssistanceRadius that pass CanAssistTo and are in line of sight join
 *    after CreatureFamilyAssistanceDelay. Those helpers are flagged and do not call again.
 *  - creature_groups with OPTION_AGGRO_TOGETHER: all members join, and they do call
 *    assistance themselves.
 */

#ifndef MANGOS_COMMANDER_ASSIST_PREDICTOR_H
#define MANGOS_COMMANDER_ASSIST_PREDICTOR_H

#include "Common.h"
#include "ObjectGuid.h"
#include <vector>

class Creature;
class Unit;

namespace Commander
{
namespace AssistPredictor
{
    // Every creature expected in the fight, `target` included.
    std::vector<ObjectGuid> Predict(Creature* target, Unit* puller);
}
}

#endif
