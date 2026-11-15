#include "Commander/Perception/Spatial/LeaderRun.h"
#include "Commander/Perception/Tanks.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
    namespace
    {
        // Header "the run": the fight's centre, false = no enemy.
        bool FightCentre(WorldModel const& world, PartyMemberFacts const& leader, Position3& out)
        {
            if (PartyMemberFacts const* tank = Tanks::Main(world))
                if (tank->guid != leader.guid)
                {
                    out = tank->pos;
                    return true;
                }
            for (int pass = 0; pass < 2; ++pass)
            {
                float x = 0.0f, y = 0.0f, z = 0.0f;
                uint32 n = 0;
                for (EnemyFacts const& e : world.enemies)
                {
                    if (e.onAllyOnly || (pass == 0 && e.victim == leader.guid))
                        continue;
                    x += e.pos.x;
                    y += e.pos.y;
                    z += e.pos.z;
                    ++n;
                }
                if (n)
                {
                    out.x = x / n;
                    out.y = y / n;
                    out.z = z / n;
                    return true;
                }
            }
            return false;
        }
    }

    void LeaderRun::Reset()
    {
        m_running = false;
        m_bestGain = 0.0f;
    }

    void LeaderRun::Annotate(WorldModel& world, uint32 nowMs)
    {
        LeaderRunFacts& f = world.leaderRun;
        f = LeaderRunFacts();
        bool const fighting = world.HasPartyEnemy();
        if (!fighting)
        {
            Reset();
            m_hasLast = false;
            m_wasHereInFight = false;
            m_leftInFight = false;
            return;
        }
        PartyMemberFacts const* leader = world.FindMember(world.leader);
        if (!leader)
        {
            f.onMap = false;
            m_leftInFight |= m_wasHereInFight;
            f.leftInFight = m_leftInFight;
            Reset();
            m_hasLast = false;
            return;
        }
        m_wasHereInFight = true;

        Position3 const here = leader->pos;
        Position3 centre;
        bool const hasCentre = FightCentre(world, *leader, centre);
        if (hasCentre)
            f.fromFightYd = std::sqrt((here.x - centre.x) * (here.x - centre.x) + (here.y - centre.y) * (here.y - centre.y));

        if (!leader->alive || !m_hasLast)
        {
            Reset();
            m_last = here;
            m_hasLast = leader->alive;
            return;
        }
        float const sx = here.x - m_last.x, sy = here.y - m_last.y;
        float const step = std::sqrt(sx * sx + sy * sy);
        m_last = here;

        bool stepped = step >= MIN_STEP_YD && !leader->casting;
        if (stepped && !m_running)
        {
            // A new run: the way out from the fight's centre (or along the step).
            float ox = here.x - (hasCentre ? centre.x : here.x - sx);
            float oy = here.y - (hasCentre ? centre.y : here.y - sy);
            float len = std::sqrt(ox * ox + oy * oy);
            if (len < 1.0f)
            {
                ox = sx;
                oy = sy;
                len = step;
            }
            m_outX = ox / len;
            m_outY = oy / len;
            if ((sx * m_outX + sy * m_outY) / step >= MIN_AWAY_COS)
            {
                m_running = true;
                m_startMs = nowMs;
                m_start.x = here.x - sx;
                m_start.y = here.y - sy;
                m_start.z = here.z;
                m_bestGain = 0.0f;
            }
            else
                stepped = false;
        }
        else if (stepped && (sx * m_outX + sy * m_outY) / step < MIN_AWAY_COS)
            stepped = false;

        if (!m_running)
            return;
        if (stepped)
            m_lastStepMs = nowMs;
        float const gain = (here.x - m_start.x) * m_outX + (here.y - m_start.y) * m_outY;
        m_bestGain = std::max(m_bestGain, gain);
        if (WorldTimer::getMSTimeDiff(m_lastStepMs, nowMs) > GRACE_MS || gain < m_bestGain * 0.5f)
        {
            Reset();
            return;
        }
        f.running = true;
        f.runMs = WorldTimer::getMSTimeDiff(m_startMs, nowMs);
        f.gainYd = std::max(0.0f, gain);
    }
}
