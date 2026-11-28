#include "Commander/Perception/Forecast/WaveEvent.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
namespace WaveEvent
{
    namespace
    {
        float WaveCost(WorldModel const& world, PartyMemberFacts const& member)
        {
            float const measured = world.waves.WaveManaOf(member.guid);
            if (measured >= 0.0f)
                return measured;
            return member.fightManaShare > 0.0f ? member.fightManaShare * float(member.maxMana) : 0.0f;
        }
    }

    float FutureMana(WorldModel const& world, PartyMemberFacts const& member)
    {
        WaveEventFacts const& waves = world.waves;
        if (!waves.active || waves.pace == WavePace::Player || !waves.wavesLeft || !member.UsesMana())
            return 0.0f;
        float const cost = WaveCost(world, member);
        if (cost <= 0.0f)
            return 0.0f;
        float refill = 0.0f;
        if (waves.pace == WavePace::Timer && member.hasDrink && member.drinkManaPerSec > 0.0f)
        {
            float const fight = waves.waveFightSec > 0.0f ? waves.waveFightSec : waves.intervalSec * 0.5f;
            float const drinkSec = waves.intervalSec - fight - SIT_SEC - STAND_LEAD_SEC;
            refill = member.drinkManaPerSec * std::max(0.0f, drinkSec);
        }
        return float(waves.wavesLeft) * std::max(0.0f, cost - refill);
    }
}

    void WaveSpend::Annotate(WorldModel& world, uint32 nowMs)
    {
        WaveEventFacts& waves = world.waves;
        if (!waves.active)
        {
            m_active = false;
            m_members.clear();
            m_fightSec = 0.0f;
            return;
        }
        bool const fighting = world.HasPartyEnemy();
        if (!m_active)
        {
            m_active = true;
            m_lastMs = nowMs;
        }
        float const stepSec = WorldTimer::getMSTimeDiff(m_lastMs, nowMs) / 1000.0f;
        m_lastMs = nowMs;
        if (fighting)
            m_fightSec += stepSec;

        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet || !m.UsesMana())
                continue;
            auto itr = std::find_if(m_members.begin(), m_members.end(),
                                    [&m](Member const& r) { return r.guid == m.guid; });
            if (itr == m_members.end())
            {
                m_members.push_back({ m.guid, float(m.mana), 0.0f });
                continue;
            }
            if (m.alive && float(m.mana) < itr->lastMana)
                itr->spent += itr->lastMana - float(m.mana);
            itr->lastMana = float(m.mana);
        }

        float const fought = float(waves.wavesArrived) - (fighting ? WaveEvent::WAVE_IN_PROGRESS_SHARE : 0.0f);
        waves.waveMana.clear();
        waves.waveFightSec = 0.0f;
        if (fought < 1.0f)
            return;
        waves.waveFightSec = m_fightSec / fought;
        for (Member const& r : m_members)
            waves.waveMana.push_back({ r.guid, r.spent / fought });
    }
}
