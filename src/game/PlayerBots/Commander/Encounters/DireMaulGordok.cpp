/*
 * King Gordok / Cho'Rush module (header comment: script, match, doctrine).
 */

#include "Commander/Encounters/DireMaulGordok.h"
#include "Commander/Encounters/EncounterHelpers.h"

namespace Commander
{
    bool DireMaulGordok::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        return EncounterHelpers::HasEnemyEntry(world, NPC_KING) || EncounterHelpers::HasEnemyEntry(world, NPC_CHORUSH);
    }

    void DireMaulGordok::OnReset()
    {
        *this = DireMaulGordok();
    }

    void DireMaulGordok::Refresh(TeamBotAI* /*bot*/, WorldModel const& world)
    {
        EnemyFacts const* king = EncounterHelpers::FindEnemyByEntry(world, NPC_KING);
        EnemyFacts const* chorush = EncounterHelpers::FindEnemyByEntry(world, NPC_CHORUSH);
        m_king = king ? king->guid : ObjectGuid();
        m_chorush = chorush ? chorush->guid : ObjectGuid();
        if (king && chorush && !m_noted)
        {
            m_noted = true;
            Note("Gordok: the King first. Cho'Rush turns friendly when the King dies and counts as a "
                 "dead guard for the tribute if killed: held above 30%.");
        }
    }

    void DireMaulGordok::ShapeDoctrine(WorldModel const& /*world*/, TeamPlan& plan)
    {
        if (!m_king.IsEmpty())
            plan.doctrine.SetKillTier(m_king, TIER_KING, "gordok: the King first, Cho'Rush gives up then");
        // Only while the King lives: alone, Cho'Rush is a fight of his own to finish.
        if (!m_chorush.IsEmpty() && !m_king.IsEmpty())
        {
            DoctrineEnemy& c = plan.doctrine.Enemy(m_chorush);
            c.stamps |= STAMP_DAMAGE_LIMIT;
            c.limit.stopPct = SPARE_STOP_PCT;
            c.limit.keepPct = SPARE_KEEP_PCT;
            plan.doctrine.SetKillTier(m_chorush, TIER_LAST, "gordok: Cho'Rush is spared for the tribute");
        }
    }
}
