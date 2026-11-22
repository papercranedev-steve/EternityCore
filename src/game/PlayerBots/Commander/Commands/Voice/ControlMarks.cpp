#include "Commander/Commands/Voice/ControlMarks.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Group.h"
#include "Map.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        // Free icons tried after the class's own, in this order (skull and cross never).
        uint8 const FALLBACK[] = { RAID_TARGET_ICON_MOON, RAID_TARGET_ICON_SQUARE, RAID_TARGET_ICON_TRIANGLE,
                                   RAID_TARGET_ICON_DIAMOND, RAID_TARGET_ICON_CIRCLE, RAID_TARGET_ICON_STAR };

        int8 ClassIcon(uint8 classId)
        {
            switch (classId)
            {
                case CLASS_MAGE:    return RAID_TARGET_ICON_MOON;
                case CLASS_ROGUE:   return RAID_TARGET_ICON_STAR;
                case CLASS_PRIEST:  return RAID_TARGET_ICON_TRIANGLE;
                case CLASS_HUNTER:  return RAID_TARGET_ICON_SQUARE;
                case CLASS_DRUID:   return RAID_TARGET_ICON_CIRCLE;
                case CLASS_WARLOCK: return RAID_TARGET_ICON_DIAMOND;
                default:            return ControlMarks::NONE;
            }
        }

        struct Wanted
        {
            ObjectGuid enemy;
            uint8 classId = 0;      // the controller's
        };

        // The bot behind a control: a bot member, or the owner of a bot's pet. Null otherwise
        // (a player's control is his to mark).
        PartyMemberFacts const* BotController(WorldModel const& world, ObjectGuid caster)
        {
            PartyMemberFacts const* m = world.FindMember(caster);
            if (m && m->isPet)
                m = world.FindMember(m->ownerGuid);
            return m && m->isBot ? m : nullptr;
        }

        void Want(std::vector<Wanted>& wanted, WorldModel const& world, ObjectGuid enemy, ObjectGuid caster)
        {
            if (enemy.IsEmpty())
                return;
            PartyMemberFacts const* bot = BotController(world, caster);
            if (!bot)
                return;
            for (Wanted const& w : wanted)
                if (w.enemy == enemy)
                    return;
            wanted.push_back({ enemy, bot->classId });
        }
    }

    int8 ControlMarks::IconOf(ObjectGuid enemy) const
    {
        for (Mark const& m : m_marks)
            if (m.enemy == enemy)
                return int8(m.icon);
        return NONE;
    }

    void ControlMarks::Step(Player* anyMember, WorldModel const& world, TeamPlan const& plan, bool on, uint32 nowMs)
    {
        Group* group = anyMember ? anyMember->GetGroup() : nullptr;
        if (!group)
        {
            m_marks.clear();
            return;
        }
        Map* map = anyMember->GetMap();

        std::vector<Wanted> wanted;
        if (on)
        {
            if (plan.pull.control.Active())
                Want(wanted, world, plan.pull.control.target, plan.pull.control.bot);
            for (ControlDuty const& d : plan.controls)
                if (!d.area)
                    Want(wanted, world, d.enemy, d.bot);
            for (EnemyFacts const& e : world.enemies)
                if (e.control.spellId)
                    Want(wanted, world, e.guid, e.control.caster);
        }

        // Ours that are still wanted stay; the others go after RELEASE_MS (at once when off).
        for (auto itr = m_marks.begin(); itr != m_marks.end();)
        {
            bool const still = std::any_of(wanted.begin(), wanted.end(), [&](Wanted const& w) { return w.enemy == itr->enemy; });
            if (still)
                itr->lastWantedMs = nowMs;
            bool const ours = group->GetTargetWithIcon(RaidTargetIcon(itr->icon)) == itr->enemy;
            if (!ours)
            {
                itr = m_marks.erase(itr);   // the player moved it or put another one there
                continue;
            }
            if (!still && (!on || WorldTimer::getMSTimeDiff(itr->lastWantedMs, nowMs) >= RELEASE_MS))
            {
                group->SetTargetIcon(itr->icon, ObjectGuid());
                itr = m_marks.erase(itr);
                continue;
            }
            ++itr;
        }

        for (Wanted const& w : wanted)
        {
            if (IconOf(w.enemy) != NONE)
                continue;
            // Marked by the player already.
            bool marked = false;
            for (uint8 i = 0; i < TARGET_ICON_COUNT; ++i)
                if (group->GetTargetWithIcon(RaidTargetIcon(i)) == w.enemy)
                    marked = true;
            if (marked)
                continue;

            auto free = [&](uint8 icon)
            {
                for (Mark const& m : m_marks)
                    if (m.icon == icon)
                        return false;
                ObjectGuid const holder = group->GetTargetWithIcon(RaidTargetIcon(icon));
                if (holder.IsEmpty())
                    return true;
                // The player's mark on a living creature here is his; a dead or gone one is stale.
                Unit const* unit = map ? map->GetUnit(holder) : nullptr;
                return !unit || !unit->IsAlive();
            };
            int8 icon = ClassIcon(w.classId);
            if (icon == NONE || !free(uint8(icon)))
            {
                icon = NONE;
                for (uint8 candidate : FALLBACK)
                    if (free(candidate))
                    {
                        icon = int8(candidate);
                        break;
                    }
            }
            if (icon == NONE)
                continue;
            group->SetTargetIcon(uint8(icon), w.enemy);
            Mark mark;
            mark.enemy = w.enemy;
            mark.icon = uint8(icon);
            mark.lastWantedMs = nowMs;
            m_marks.push_back(mark);
        }
    }
}
