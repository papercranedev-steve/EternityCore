// Pull planning helpers (PullCommon.h).
#include "Commander/Coordination/Situation/Pull/PullPlanner.h"
#include "Commander/Coordination/Situation/Pull/PullCommon.h"
#include "Commander/Coordination/Situation/Pull/PullChoice.h"
#include "Commander/Coordination/Situation/Pull/PullPatrolCheck.h"
#include "Commander/Coordination/Situation/Pull/PullLosSearch.h"
#include "Commander/Coordination/Situation/ClearPlanner.h"
#include "Commander/Coordination/Situation/Los/LosBreak.h"
#include "Commander/Coordination/Situation/Los/LosHealerSpot.h"
#include "Commander/Coordination/Situation/Patrol/PatrolAssigner.h"
#include "Commander/Coordination/Situation/Pull/PullMethod.h"
#include "Commander/Coordination/Situation/Pull/PullOpening.h"
#include "Commander/Coordination/Situation/Pull/PullControl.h"
#include "Commander/Coordination/Situation/Pull/PullPlace.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/Forecast/AssistPredictor.h"
#include "Commander/Perception/Forecast/FightLoad.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/PatrolTracker.h"
#include "Commander/Perception/Forecast/PullOdds.h"
#include "Commander/Perception/Spatial/ShotSpot.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "PartyBotAI.h"
#include "Creature.h"
#include "CreatureGroups.h"
#include "DBCStores.h"
#include "Group.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "Map.h"
#include "Player.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Commander
{
    using namespace PullDetail;

    namespace
    {
        // Point on the line from the target towards the tank, `distance` from the target
        // (the tank's position if it is closer already). Straight line: pathing is not checked.
        Position3 StandPoint(Creature const* target, Unit const* tank, float distance)
        {
            Position3 const t = PositionOf(target);
            Position3 const k = PositionOf(tank);
            float const dx = k.x - t.x;
            float const dy = k.y - t.y;
            float const len = std::sqrt(dx * dx + dy * dy);
            if (len < 0.1f || len <= distance)
                return len < 0.1f ? t : k;
            float const f = distance / len;
            Position3 p;
            p.x = t.x + dx * f;
            p.y = t.y + dy * f;
            p.z = t.z + (k.z - t.z) * f;
            return p;
        }

        float MeleeStandDistance(Creature const* target)
        {
            return target->GetCombatReach() + PullPlanner::MELEE_STAND_EXTRA;
        }

        float SpellMinRange(uint32 spellId)
        {
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
            SpellRangeEntry const* range = spell ? sSpellRangeStore.LookupEntry(spell->rangeIndex) : nullptr;
            return range ? range->minRange : 0.0f;
        }

        // The target's database group (CreatureGroup): it comes with the target anyway, so a
        // face pull spot may be inside their aggro ranges too.
        std::vector<ObjectGuid> GroupOf(WorldModel const& world, Creature const* target)
        {
            std::vector<ObjectGuid> group;
            PackCreature const* facts = FindPackCreature(world, target->GetObjectGuid());
            PackFacts const* pack = FindPackOf(world, target->GetObjectGuid());
            if (!facts || !pack || facts->dbGroupLeader.IsEmpty())
                return group;
            for (PackCreature const& m : pack->members)
                if (m.dbGroupLeader == facts->dbGroupLeader)
                    group.push_back(m.guid);
            return group;
        }
    }

    namespace PullDetail
    {
        Position3 PositionOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }

        std::string Format(char const* fmt, ...)
        {
            char buf[256];
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(buf, sizeof(buf), fmt, ap);
            va_end(ap);
            return buf;
        }

        bool Contains(std::vector<ObjectGuid> const& list, ObjectGuid guid)
        {
            return std::find(list.begin(), list.end(), guid) != list.end();
        }

        void AddUnique(std::vector<ObjectGuid>& list, std::vector<ObjectGuid> const& more)
        {
            for (ObjectGuid g : more)
                if (!Contains(list, g))
                    list.push_back(g);
        }

        PackFacts const* FindPackOf(WorldModel const& world, ObjectGuid guid)
        {
            for (PackFacts const& pack : world.packs)
                for (PackCreature const& c : pack.members)
                    if (c.guid == guid)
                        return &pack;
            return nullptr;
        }

        PackCreature const* FindMemberFacts(PackFacts const* pack, ObjectGuid guid)
        {
            if (!pack)
                return nullptr;
            for (PackCreature const& c : pack->members)
                if (c.guid == guid)
                    return &c;
            return nullptr;
        }

        PackCreature const* FindPackCreature(WorldModel const& world, ObjectGuid guid)
        {
            return FindMemberFacts(FindPackOf(world, guid), guid);
        }

        // Stands back and casts its main ranged spell (creature_spells CF_MAIN_RANGED_SPELL):
        // does not follow the tank.
        bool IsRangedCaster(Creature const* c)
        {
            return LosBreak::IsRangedCaster(c->GetEntry());
        }

        // Stand point of a walk-in pull: in melee on the tank's side.
        Position3 MeleeStandPoint(Creature const* target, Unit const* tank)
        {
            return StandPoint(target, tank, MeleeStandDistance(target));
        }

        // Where the tank can shoot `target` from (Perception/Spatial/ShotSpot.h).
        bool FindShotSpot(Map* map, Creature const* target, Player* tank, uint32 spellId, float range,
                          std::vector<Position3> const& avoid, ShotSpot::Spot& spot, ShotSpot::Failure& why)
        {
            ShotSpot::Request request;
            request.target = target;
            request.shooter = tank;
            request.maxRange = range;
            request.minRange = SpellMinRange(spellId);
            request.avoid = avoid;
            return ShotSpot::Find(map, request, spot, &why);
        }

        // Where the tank face pulls `target` from (Perception/Spatial/ShotSpot.h FindFace).
        bool FindFaceSpot(WorldModel const& world, Map* map, Creature const* target, Player* tank,
                          std::vector<Position3> const& avoid, ShotSpot::Spot& spot, ShotSpot::Failure& why)
        {
            ShotSpot::Request request;
            request.target = target;
            request.shooter = tank;
            request.avoid = avoid;
            request.ignore = GroupOf(world, target);
            return ShotSpot::FindFace(map, request, spot, &why);
        }

        std::vector<ObjectGuid> AggroRangeGuids(std::vector<AssistPredictor::Joiner> const& joiners)
        {
            std::vector<ObjectGuid> out;
            for (AssistPredictor::Joiner const& j : joiners)
                if (j.reason == AssistPredictor::JoinReason::AggroRange)
                    out.push_back(j.guid);
            return out;
        }

        uint32 CountElites(Map* map, std::vector<ObjectGuid> const& guids)
        {
            uint32 elites = 0;
            for (ObjectGuid g : guids)
                if (Creature* e = map->GetCreature(g))
                    if (e->IsElite())
                        ++elites;
            return elites;
        }

        std::string NamesOf(Map* map, std::vector<ObjectGuid> const& guids, std::vector<ObjectGuid> const* marked,
                            char const* mark)
        {
            std::string names;
            for (ObjectGuid g : guids)
            {
                if (!names.empty())
                    names += ", ";
                Creature* e = map->GetCreature(g);
                names += e ? e->GetName() : "?";
                if (marked && Contains(*marked, g))
                    names += mark;
            }
            return names;
        }

        // Enemy in the fight that the pull did not predict. Adds summoned by a predicted
        // creature belong to the pull.
        bool IsUnexpected(EnemyFacts const& e, PullPlan const& pull)
        {
            return !pull.Expects(e);
        }

        bool IsStealthedPackCreature(WorldModel const& world, ObjectGuid guid)
        {
            PackCreature const* c = FindPackCreature(world, guid);
            return c && c->stealthed;
        }
    }
}
