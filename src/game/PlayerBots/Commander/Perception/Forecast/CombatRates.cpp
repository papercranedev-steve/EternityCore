#include "Commander/Perception/Forecast/CombatRates.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Tanks.h"
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
            // Not exclusive: a lone healer heals and deals damage (PartyMemberFacts::functions).
            if (m.HealsTeam())
                ++r.healers;
            if (m.DealsDamage())
                ++r.damageDealers;
        }

        PartyMemberFacts const* const tank = Tanks::HitTaker(world);
        float heldDps = 0.0f;   // controlled enemies, once they wake (RateFacts::worthDps)
        bool hittable = false;  // an enemy of ours the party may hit now (fightDpsPerDealer)
        for (EnemyFacts const& e : world.enemies)
        {
            // Enemies only fighting an ally (prisoners, guards) are not our fight: their health
            // would stretch our remaining fight time and their loss would count as our damage.
            if (e.onAllyOnly)
                continue;
            r.enemyHealth += float(e.health);
            if (e.acting && e.health && !(e.control.spellId && e.control.breaksOnDamage))
                hittable = true;
            if (e.crowdControlled)
            {
                PartyMemberFacts const* victim = world.FindMember(e.victim);
                if (!victim)
                    victim = tank;
                if (victim)
                    heldDps += Value::HitDps(e, *victim);
                continue;
            }
            if (!e.acting || e.victim.IsEmpty())
                continue;
            if (PartyMemberFacts const* victim = world.FindMember(e.victim))
                r.incomingDps += Value::HitDps(e, *victim);
        }
        r.worthDps = r.incomingDps > 0.0f ? r.incomingDps : heldDps;

        // No enemies: the fight is over, start a new measurement next time. A long enough fight
        // is remembered for the next pull (Value::DealtDps before it is measured).
        if (!world.HasPartyEnemy())
        {
            if (m_fightStartMs && WorldTimer::getMSTimeDiff(m_fightStartMs, m_fightLastMs) >= MIN_REMEMBERED_FIGHT_MS)
            {
                m_lastFightDps = m_fightLost / (WorldTimer::getMSTimeDiff(m_fightStartMs, m_fightLastMs) / 1000.0f);
                m_lastFightDealers = r.damageDealers;
            }
            m_fightStartMs = 0;
            m_fightLost = 0.0f;
            m_hittableMs = 0;
            m_dealerSec = 0.0f;
            m_samples.clear();
            r.lastFightDps = m_lastFightDps;
            r.lastFightDealers = m_lastFightDealers;
            return;
        }
        r.lastFightDps = m_lastFightDps;
        r.lastFightDealers = m_lastFightDealers;

        while (!m_samples.empty() && WorldTimer::getMSTimeDiff(m_samples.front().timeMs, nowMs) > MAX_WINDOW_MS)
            m_samples.pop_front();

        // An enemy that now fights only an ally: drop its rows from the stored samples, so the
        // health the ally takes off it is not counted as ours (now or after it turns back to
        // us: its rows start again from the next sample).
        for (EnemyFacts const& e : world.enemies)
        {
            if (!e.onAllyOnly)
                continue;
            for (Sample& s : m_samples)
                s.rows.erase(std::remove_if(s.rows.begin(), s.rows.end(),
                    [&e](Row const& row) { return row.enemy == e.guid; }), s.rows.end());
        }

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

        // Whole fight: health lost since the previous sample.
        if (!m_fightStartMs)
            m_fightStartMs = nowMs;
        if (!m_samples.empty())
        {
            for (Row const& row : m_samples.back().rows)
            {
                EnemyFacts const* now = world.FindEnemy(row.enemy);
                m_fightLost += now ? std::max(0.0f, row.health - float(now->health)) : row.health;
            }
            // Waiting for every enemy's control to end deals nothing by choice: not counted.
            if (hittable)
            {
                uint32 const ms = WorldTimer::getMSTimeDiff(m_samples.back().timeMs, nowMs);
                m_hittableMs += ms;
                m_dealerSec += float(r.damageDealers) * ms / 1000.0f;
            }
        }
        m_fightLastMs = nowMs;
        if (m_hittableMs >= MIN_REMEMBERED_FIGHT_MS && m_dealerSec > 0.0f)
            r.fightDpsPerDealer = m_fightLost / m_dealerSec;

        Sample sample;
        sample.timeMs = nowMs;
        for (EnemyFacts const& e : world.enemies)
            if (!e.onAllyOnly)      // an ally's kills are not our damage
                sample.rows.push_back({ e.guid, float(e.health) });
        m_samples.push_back(std::move(sample));
    }
}
