#include "Commander/Perception/Forecast/DamageMeter.h"
#include "Commander/Perception/WorldModel.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
    DamageMeter::Entry& DamageMeter::Fight::Of(ObjectGuid guid)
    {
        for (Entry& e : entries)
            if (e.guid == guid)
                return e;
        entries.push_back(Entry());
        entries.back().guid = guid;
        return entries.back();
    }

    void DamageMeter::Note(ObjectGuid member, uint32 damage, bool area, uint32 nowMs)
    {
        if (!damage || member.IsEmpty())
            return;
        if (!m_active)
        {
            m_active = true;
            m_current = Fight();
            m_current.startMs = nowMs;
        }
        m_current.lastMs = nowMs;
        Entry& e = m_current.Of(member);
        e.damage += damage;
        if (area)
            e.area += damage;
    }

    void DamageMeter::Annotate(WorldModel& world, uint32 nowMs)
    {
        bool const fights = world.HasPartyEnemy();
        if (fights && !m_active)
        {
            m_active = true;
            m_current = Fight();
            m_current.startMs = nowMs;
            m_current.lastMs = nowMs;
        }
        if (m_active)
        {
            // Everyone in the team takes part (a healer's 0 counts for it, a member that joined
            // later is not dragged down by the fights before).
            for (PartyMemberFacts const& m : world.party)
                if (!m.isPet)
                    m_current.Of(m.guid);
            m_current.sec = WorldTimer::getMSTimeDiff(m_current.startMs, std::max(m_current.lastMs, nowMs)) / 1000.0f;
            if (!fights && WorldTimer::getMSTimeDiff(m_current.lastMs, nowMs) >= END_QUIET_MS)
            {
                m_current.sec = WorldTimer::getMSTimeDiff(m_current.startMs, m_current.lastMs) / 1000.0f;
                if (m_current.sec >= MIN_FIGHT_SEC)
                {
                    m_kept.push_front(m_current);
                    if (m_kept.size() > KEEP_FIGHTS)
                        m_kept.pop_back();
                }
                m_active = false;
            }
        }

        DamageMeterFacts& out = world.damage;
        out = DamageMeterFacts();
        out.fighting = m_active;
        out.currentSec = m_active ? m_current.sec : 0.0f;
        auto const member = [&out](ObjectGuid guid) -> MemberDamage&
        {
            for (MemberDamage& m : out.members)
                if (m.guid == guid)
                    return m;
            out.members.push_back(MemberDamage());
            out.members.back().guid = guid;
            return out.members.back();
        };
        struct Sum
        {
            ObjectGuid guid;
            float damage = 0.0f, area = 0.0f, sec = 0.0f;
        };
        std::vector<Sum> sums;
        for (Fight const& f : m_kept)
            for (Entry const& e : f.entries)
            {
                auto it = std::find_if(sums.begin(), sums.end(), [&e](Sum const& s) { return s.guid == e.guid; });
                if (it == sums.end())
                {
                    sums.push_back(Sum());
                    it = sums.end() - 1;
                    it->guid = e.guid;
                }
                it->damage += float(e.damage);
                it->area += float(e.area);
                it->sec += f.sec;
                ++member(e.guid).fights;
            }
        for (Sum const& s : sums)
        {
            MemberDamage& m = member(s.guid);
            m.dps = s.sec > 0.0f ? s.damage / s.sec : 0.0f;
            m.areaShare = s.damage > 0.0f ? s.area / s.damage : 0.0f;
        }
        if (m_active)
            for (Entry const& e : m_current.entries)
                member(e.guid).currentDamage = e.damage;
    }
}
