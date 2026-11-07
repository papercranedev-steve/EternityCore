/*
 * PartyBot Commander layer 2 (brain): small helpers shared by the pull planning files
 * (PullPlanner, PullChoice, PullPatrolCheck, PullLosSearch, PullAssigner). Not used outside the
 * pull. Split out of PullPlanner.cpp (todo item 14), behaviour unchanged.
 */

#ifndef MANGOS_COMMANDER_PULL_COMMON_H
#define MANGOS_COMMANDER_PULL_COMMON_H

#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/AssistPredictor.h"
#include "Commander/Perception/ShotSpot.h"
#include <algorithm>
#include <string>
#include <vector>

class WorldObject;
class Creature;
class Player;
class Unit;
class Map;

namespace Commander
{
    namespace PullDetail
    {
        Position3 PositionOf(WorldObject const* obj);
        std::string Format(char const* fmt, ...);

        bool Contains(std::vector<ObjectGuid> const& list, ObjectGuid guid);
        void AddUnique(std::vector<ObjectGuid>& list, std::vector<ObjectGuid> const& more);

        PackFacts const* FindPackOf(WorldModel const& world, ObjectGuid guid);
        PackCreature const* FindMemberFacts(PackFacts const* pack, ObjectGuid guid);
        PackCreature const* FindPackCreature(WorldModel const& world, ObjectGuid guid);
        bool IsStealthedPackCreature(WorldModel const& world, ObjectGuid guid);

        template <class Pred>
        bool AnyMember(PackFacts const& pack, Pred pred)
        {
            return std::any_of(pack.members.begin(), pack.members.end(), pred);
        }

        // Stands back and casts its main ranged spell (creature_spells CF_MAIN_RANGED_SPELL):
        // does not follow the tank.
        bool IsRangedCaster(Creature const* c);

        // Stand point of a walk-in pull: in melee on the tank's side (straight line, pathing
        // not checked).
        Position3 MeleeStandPoint(Creature const* target, Unit const* tank);

        // Where the tank can shoot `target` from (Perception/ShotSpot.h).
        bool FindShotSpot(Map* map, Creature const* target, Player* tank, uint32 spellId, float range,
                          std::vector<Position3> const& avoid, ShotSpot::Spot& spot, ShotSpot::Failure& why);
        // Where the tank face pulls `target` from (Perception/ShotSpot.h FindFace); the target's
        // database group comes with it anyway, so the spot may be inside their aggro ranges.
        bool FindFaceSpot(WorldModel const& world, Map* map, Creature const* target, Player* tank,
                          std::vector<Position3> const& avoid, ShotSpot::Spot& spot, ShotSpot::Failure& why);

        std::vector<ObjectGuid> AggroRangeGuids(std::vector<AssistPredictor::Joiner> const& joiners);
        uint32 CountElites(Map* map, std::vector<ObjectGuid> const& guids);
        std::string NamesOf(Map* map, std::vector<ObjectGuid> const& guids, std::vector<ObjectGuid> const* marked = nullptr,
                            char const* mark = "");

        // Enemy in the fight that the pull did not predict. Adds summoned by a predicted
        // creature belong to the pull.
        bool IsUnexpected(EnemyFacts const& e, PullPlan const& pull);
    }
}

#endif
