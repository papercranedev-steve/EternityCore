#include "Commander/Perception/Spatial/StealthDetect.h"
#include "Commander/Perception/Spatial/AggroRange.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Creature.h"
#include "Map.h"
#include "PathFinder.h"
#include "Player.h"
#include "SpellEntry.h"
#include "World.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <algorithm>
#include <cmath>
#include <list>

namespace Commander
{
namespace StealthDetect
{
    namespace
    {
        float Distance3d(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        // Unit::HasInArc(target) with the default arc (pi): within 90 degrees of its facing.
        bool InFront(Watcher const& w, Position3 const& at)
        {
            float const angle = std::atan2(at.y - w.pos.y, at.x - w.pos.x);
            float diff = std::fmod(angle - w.orientation, 2.0f * float(M_PI));
            if (diff < -float(M_PI))
                diff += 2.0f * float(M_PI);
            else if (diff > float(M_PI))
                diff -= 2.0f * float(M_PI);
            return std::fabs(diff) <= float(M_PI) / 2.0f;
        }

        class IdleCheck
        {
        public:
            IdleCheck(Player const* rogue, Position3 const& center, float range)
                : m_rogue(rogue), m_center(center), m_range(range) {}
            bool operator()(Creature* c)
            {
                return AggroRange::IsIdleAggressor(c, m_rogue) && c->GetDistance2d(m_center.x, m_center.y) <= m_range;
            }

        private:
            Player const* m_rogue;
            Position3 m_center;
            float m_range;
        };
    }

    int32 StealthSkill(Player const* rogue, SpellEntry const* stealthSpell)
    {
        int32 skill = rogue->GetTotalAuraModifier(SPELL_AURA_MOD_STEALTH);
        if (!rogue->HasAuraType(SPELL_AURA_MOD_STEALTH) && stealthSpell)
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                if (stealthSpell->Effect[i] == SPELL_EFFECT_APPLY_AURA &&
                    stealthSpell->EffectApplyAuraName[i] == SPELL_AURA_MOD_STEALTH)
                    skill = std::max(skill, stealthSpell->CalculateSimpleValue(SpellEffectIndex(i)));
        return skill + rogue->GetTotalAuraModifier(SPELL_AURA_MOD_STEALTH_LEVEL);
    }

    Watcher WatcherOf(Creature const* c, Player const* rogue, int32 stealthSkill)
    {
        Watcher w;
        w.guid = c->GetObjectGuid();
        w.pos.x = c->GetPositionX();
        w.pos.y = c->GetPositionY();
        w.pos.z = c->GetPositionZ();
        w.orientation = c->GetOrientation();
        w.blind = c->HasUnitState(UNIT_STATE_STUNNED);
        w.maxRange = sWorld.getConfig(CONFIG_FLOAT_MAX_CREATURES_STEALTH_DETECT_RANGE);

        // Unit::CanDetectStealthOf, creature detector.
        float yardsPerLevel = 5.0f / 6.0f;
        int32 const detectSkill = int32(c->GetLevelForTarget(rogue)) * 5 +
                                  c->GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_STEALTH_DETECT, 0);
        int32 const levelDiff = int32(c->GetLevelForTarget(rogue)) - int32(rogue->GetLevelForTarget(c));
        if (levelDiff > 3)
            yardsPerLevel *= 2.0f;
        float detect = 5.0f / 6.0f + float(detectSkill - stealthSkill) * yardsPerLevel / 5.0f;
        w.front = std::min(MAX_DETECT_YD, std::max(0.0f, detect));
        w.behind = w.front - BEHIND_PENALTY_YD;

        AggroRange::Wander const wander = AggroRange::WanderOf(c);
        w.home = wander.home;
        w.wander = wander.radius;
        w.turns = wander.radius > 0.0f || c->GetDefaultMovementType() == WAYPOINT_MOTION_TYPE || c->IsMoving();
        return w;
    }

    float SafeDistance(Watcher const& w, Position3 const& at)
    {
        if (w.blind)
            return 0.0f;
        float need = (w.turns || InFront(w, at)) ? w.front : std::min(std::max(0.0f, w.behind + ALERT_YD), w.front);
        need = std::min(need, w.maxRange);
        return std::max(COLLISION_YD, need) + MARGIN_YD;
    }

    bool Notices(Watcher const& w, Position3 const& at)
    {
        if (w.blind)
            return false;
        float const need = SafeDistance(w, at);
        if (Distance3d(w.pos, at) < need)
            return true;
        // A wanderer may stand anywhere in its circle.
        return w.wander > 0.0f && Distance3d(w.home, at) < need + w.wander;
    }

    Watchers::Watchers(Player* rogue, Position3 const& center, float scanRadius, int32 stealthSkill)
    {
        std::list<Creature*> found;
        IdleCheck check(rogue, center, scanRadius);
        MaNGOS::CreatureListSearcher<IdleCheck> searcher(found, check);
        Cell::VisitGridObjects(center.x, center.y, rogue->GetMap(), searcher, scanRadius);
        for (Creature* c : found)
            m_watchers.push_back(WatcherOf(c, rogue, stealthSkill));
    }

    bool Watchers::Notice(Position3 const& at) const
    {
        return std::any_of(m_watchers.begin(), m_watchers.end(), [&at](Watcher const& w) { return Notices(w, at); });
    }

    bool Watchers::WalkIsSafe(Player* rogue, Position3 const& to, float maxDetour, float* length) const
    {
        Position3 here;
        here.x = rogue->GetPositionX();
        here.y = rogue->GetPositionY();
        here.z = rogue->GetPositionZ();
        PathInfo path(rogue);
        path.calculate(to.x, to.y, to.z);
        uint32 const type = path.getPathType();
        if (type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE))
            return false;
        std::vector<Position3> points;
        points.push_back(here);
        if (type & PATHFIND_NOT_USING_PATH)
        {
            if (!rogue->GetMap()->isInLineOfSight(here.x, here.y, here.z + 2.0f, to.x, to.y, to.z + 2.0f))
                return false;
            points.push_back(to);
        }
        else
        {
            if (!SafeRoute::PathReaches(rogue->GetMap(), path, to))
                return false;
            if (path.Length() > SafeRoute::Distance2d(here, to) * maxDetour + 2.0f)
                return false;
            for (auto const& p : path.getPath())
            {
                Position3 q;
                q.x = p.x; q.y = p.y; q.z = p.z;
                points.push_back(q);
            }
            points.push_back(to);
        }

        float total = 0.0f;
        for (size_t i = 1; i < points.size(); ++i)
        {
            Position3 const& a = points[i - 1];
            Position3 const& b = points[i];
            float const segment = Distance3d(a, b);
            total += segment;
            uint32 const steps = std::max(1u, uint32(segment / PATH_SAMPLE_STEP));
            for (uint32 s = 1; s <= steps; ++s)
            {
                float const f = float(s) / steps;
                Position3 p;
                p.x = a.x + (b.x - a.x) * f;
                p.y = a.y + (b.y - a.y) * f;
                p.z = a.z + (b.z - a.z) * f;
                if (Notice(p))
                    return false;
            }
        }
        if (length)
            *length = total;
        return true;
    }
}
}
