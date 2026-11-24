#include "Commander/Perception/Spatial/LeaderActivity.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        float Distance(Position3 const& a, Position3 const& b)
        {
            return PackResolver::Distance(a, b);
        }

        bool ComesAnyway(PackCreature const& c, WorldModel const& world)
        {
            if (!c.aggressive || c.noAggro || c.stealthed)
                return false;
            for (PartyMemberFacts const& m : world.party)
                if (m.alive && Distance(m.pos, c.pos) <= c.reach + LeaderActivity::FIGHT_MARGIN_YD)
                    return true;
            return false;
        }

        std::vector<uint32> EntriesOf(PackFacts const& pack)
        {
            std::vector<uint32> entries;
            for (PackCreature const& c : pack.members)
                if (std::find(entries.begin(), entries.end(), c.entry) == entries.end())
                    entries.push_back(c.entry);
            return entries;
        }
    }

    void LeaderActivity::Annotate(WorldModel& world, uint32 nowMs)
    {
        world.leaderActivity = LeaderActivityFacts();
        for (PackFacts& pack : world.packs)
            for (PackCreature& c : pack.members)
                c.comesAnyway = ComesAnyway(c, world);

        PartyMemberFacts const* leader = world.FindMember(world.leader);
        if (!leader || !world.humanInTeam)
        {
            m_trail.clear();
            m_watched = LeaderEngage();
            return;
        }

        if (m_trail.empty() || WorldTimer::getMSTimeDiff(m_trail.back().atMs, nowMs) >= TRAIL_STEP_MS)
            m_trail.push_back({ nowMs, leader->pos });
        while (!m_trail.empty() && WorldTimer::getMSTimeDiff(m_trail.front().atMs, nowMs) > WANDER_MS + TRAIL_STEP_MS)
            m_trail.erase(m_trail.begin());
        world.leaderActivity.wanders = WorldTimer::getMSTimeDiff(m_trail.front().atMs, nowMs) + TRAIL_STEP_MS >= WANDER_MS &&
            std::all_of(m_trail.begin(), m_trail.end(), [leader](Sample const& s) { return Distance(s.pos, leader->pos) <= WANDER_YD; });

        Watch(world, *leader);
    }

    void LeaderActivity::Watch(WorldModel& world, PartyMemberFacts const& leader)
    {
        // The watched one is in a fight now: his if he attacks it or it attacks him.
        if (!m_watched.creature.IsEmpty())
            if (EnemyFacts const* enemy = world.FindEnemy(m_watched.creature))
            {
                if (enemy->victim == leader.guid || leader.victim == m_watched.creature)
                    world.leaderActivity.engaged.push_back(m_watched);
                m_watched = LeaderEngage();
            }

        for (PackFacts const& pack : world.packs)
            for (PackCreature const& c : pack.members)
            {
                if (c.guid != leader.selection)
                    continue;
                // (it comes on its own: a fight with it says nothing about him)
                if (c.comesAnyway || c.tappedByOther)
                {
                    if (m_watched.creature == c.guid)
                        m_watched = LeaderEngage();
                    return;
                }
                m_watched.creature = c.guid;
                m_watched.entries = EntriesOf(pack);
                m_watched.pos = c.pos;
                return;
            }
        // Selected none idle: the old one stays watched while its fight may be starting.
        if (!m_watched.creature.IsEmpty() && leader.selection != m_watched.creature)
            m_watched = LeaderEngage();
    }
}
