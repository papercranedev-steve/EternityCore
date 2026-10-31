#include "Commander/Perception/CombatRates.h"
#include "Commander/Perception/ValueModel.h"
#include "SharedDefines.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
    void CombatRates::Annotate(WorldModel& world, uint32 nowMs)
    {
        RateFacts& r = world.rates;
        r = RateFacts();

        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet)
                continue;
            r.partyMaxHealth += float(m.maxHealth);
            if (!m.CanAct())
                continue;   // dead, asleep, stunned or mind controlled: contributes nothing now
            if (m.isBot && m.role == ROLE_HEALER)
                ++r.healers;
            else
                ++r.damageDealers;
        }

        for (EnemyFacts const& e : world.enemies)
        {
            r.enemyHealth += float(e.health);
            if (e.crowdControlled || !e.acting || e.victim.IsEmpty())
                continue;
            if (PartyMemberFacts const* victim = world.FindMember(e.victim))
                r.incomingDps += Value::HitDps(e, *victim);
        }

        // No enemies: the fight is over, start a new measurement next time.
        if (world.enemies.empty())
        {
            m_samples.clear();
            return;
        }

        while (!m_samples.empty() && WorldTimer::getMSTimeDiff(m_samples.front().timeMs, nowMs) > MAX_WINDOW_MS)
            m_samples.pop_front();

        Sample const* base = nullptr;
        for (auto itr = m_samples.rbegin(); itr != m_samples.rend(); ++itr)
        {
            if (WorldTimer::getMSTimeDiff(itr->timeMs, nowMs) >= MIN_WINDOW_MS)
            {
                base = &*itr;
                break;
            }
        }

        if (base)
        {
            float lost = 0.0f;
            for (Row const& row : base->rows)
            {
                EnemyFacts const* now = world.FindEnemy(row.enemy);
                // Gone from the list = killed (or evaded; rare during a fight).
                lost += now ? std::max(0.0f, row.health - float(now->health)) : row.health;
            }
            float const seconds = WorldTimer::getMSTimeDiff(base->timeMs, nowMs) / 1000.0f;
            r.dealtDps = lost / seconds;
            r.dealtMeasured = true;
        }

        Sample sample;
        sample.timeMs = nowMs;
        for (EnemyFacts const& e : world.enemies)
            sample.rows.push_back({ e.guid, float(e.health) });
        m_samples.push_back(std::move(sample));
    }
}
