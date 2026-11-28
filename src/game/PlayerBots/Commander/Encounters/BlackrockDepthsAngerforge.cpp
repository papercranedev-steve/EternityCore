/*
 * General Angerforge boss module (header comment: match, doctrine, limits).
 */

#include "Commander/Encounters/BlackrockDepthsAngerforge.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Timer.h"
#include <algorithm>
#include <cstdio>

namespace Commander
{
    bool BlackrockDepthsAngerforge::IsCalled(EnemyFacts const& e)
    {
        return e.summoned && (e.entry == NPC_MEDIC || e.entry == NPC_RESERVIST);
    }

    bool BlackrockDepthsAngerforge::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        return std::any_of(world.enemies.begin(), world.enemies.end(), [](EnemyFacts const& e)
        {
            return e.entry == NPC_ANGERFORGE || IsCalled(e);
        });
    }

    void BlackrockDepthsAngerforge::OnReset()
    {
        *this = BlackrockDepthsAngerforge();
    }

    void BlackrockDepthsAngerforge::Refresh(TeamBotAI* /*bot*/, WorldModel const& world)
    {
        uint32 const now = world.refreshedAtMs;

        EnemyFacts const* boss = EncounterHelpers::FindEnemyByEntry(world, NPC_ANGERFORGE);
        m_bossAlive = boss != nullptr;
        if (boss)
        {
            m_boss = boss->guid;
            m_bossPct = boss->healthPct;
        }

        m_medics.clear();
        bool called = false;
        bool othersUp = false;
        for (EnemyFacts const& e : world.enemies)
        {
            if (!e.health || e.onAllyOnly)
                continue;
            if (IsCalled(e))
            {
                called = true;
                if (e.entry == NPC_MEDIC)
                    m_medics.push_back(e.guid);
            }
            else if (e.entry != NPC_ANGERFORGE && e.acting)
                othersUp = true;
        }
        if (called && !m_calledAtMs)
        {
            m_calledAtMs = now;
            m_holdDamage = false;
            Note("Angerforge called his reserves: medics first, area spells on the reservists.");
            Say(EncounterHelpers::Line("voice.boss.angerforge.adds", VoiceTier::Urgent));
        }

        // After the call: is he worth finishing before the medics?
        m_burn = m_bossAlive && m_calledAtMs &&
                 float(boss->health) / std::max(1.0f, Value::DealtDps(world)) <= BURN_SEC;

        // Hold before the call.
        PartyMemberFacts const* tank = EncounterHelpers::FindHolder(world, boss);
        PartyMemberFacts const* healer = EncounterHelpers::FindLowestManaHealer(world);
        bool const inBand = m_bossAlive && !m_calledAtMs && m_bossPct > SUMMON_PCT && m_bossPct <= HOLD_BAND_PCT;
        bool const tankLow = tank && tank->healthPct < TANK_READY_PCT;
        bool const manaLow = healer && healer->manaPct < HEALER_READY_MANA_PCT;
        bool const wantHold = inBand && !m_holdGivenUp && (othersUp || tankLow || manaLow);

        if (wantHold && !m_holdDamage)
        {
            m_holdDamage = true;
            m_holdSinceMs = now;
            char buf[200];
            snprintf(buf, sizeof(buf), "Angerforge at %.0f%%: damage held before the 30%% call (%s%s%s).",
                m_bossPct, othersUp ? "others alive " : "", tankLow ? "tank low " : "", manaLow ? "healer mana low" : "");
            Note(buf);
            Say(EncounterHelpers::Line("voice.boss.angerforge.hold", VoiceTier::Essential));
        }
        else if (!wantHold && m_holdDamage)
        {
            m_holdDamage = false;
            if (inBand)
            {
                Note("Angerforge: ready, pushing past 30%.");
                Say(EncounterHelpers::Line("voice.boss.angerforge.push", VoiceTier::Essential));
            }
        }
        if (m_holdDamage && WorldTimer::getMSTimeDiff(m_holdSinceMs, now) >= MAX_HOLD_MS)
        {
            m_holdDamage = false;
            m_holdGivenUp = true;
            Note("Angerforge: waited too long, pushing past 30% anyway.");
            Say(EncounterHelpers::Line("voice.boss.angerforge.push", VoiceTier::Essential));
        }
    }

    void BlackrockDepthsAngerforge::ShapeDoctrine(WorldModel const& /*world*/, TeamPlan& plan)
    {
        if (m_holdDamage && !m_boss.IsEmpty())
        {
            DoctrineEnemy& boss = plan.doctrine.Enemy(m_boss);
            boss.stamps |= STAMP_DAMAGE_LIMIT;
            boss.limit.stopPct = HOLD_BAND_PCT;
            boss.limit.keepPct = SUMMON_PCT;
            boss.limit.holderKeepsHitting = true;
        }
        if (!m_calledAtMs)
            return;
        if (m_burn)
            plan.doctrine.SetKillTier(m_boss, TIER_BOSS_BURN, "angerforge: dies before the medics heal him");
        for (ObjectGuid const& medic : m_medics)
            plan.doctrine.SetKillTier(medic, TIER_MEDIC, "angerforge: medic heals the boss and the reserves");
    }
}
