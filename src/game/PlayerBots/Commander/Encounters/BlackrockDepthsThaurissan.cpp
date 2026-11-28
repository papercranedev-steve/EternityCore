/*
 * Emperor Thaurissan / Princess Moira module (header comment: quests, match, doctrine).
 */

#include "Commander/Encounters/BlackrockDepthsThaurissan.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include "TeamBotAI.h"
#include "Map.h"
#include "Player.h"
#include <algorithm>

namespace Commander
{
    bool BlackrockDepthsThaurissan::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        return EncounterHelpers::HasEnemyEntry(world, NPC_EMPEROR) || EncounterHelpers::HasEnemyEntry(world, NPC_MOIRA);
    }

    void BlackrockDepthsThaurissan::OnReset()
    {
        *this = BlackrockDepthsThaurissan();
    }

    void BlackrockDepthsThaurissan::Refresh(TeamBotAI* bot, WorldModel const& world)
    {
        EnemyFacts const* moira = EncounterHelpers::FindEnemyByEntry(world, NPC_MOIRA);
        m_moira = moira ? moira->guid : ObjectGuid();

        m_spare = false;
        if (moira && bot && bot->me->IsInWorld())
        {
            Map* map = bot->me->GetMap();
            for (PartyMemberFacts const& m : world.party)
            {
                if (m.isPet)
                    continue;
                Player* p = map->GetPlayer(m.guid);
                if (p && (p->GetQuestStatus(QUEST_ROYAL_RESCUE) == QUEST_STATUS_INCOMPLETE ||
                          p->GetQuestStatus(QUEST_FATE_OF_KINGDOM) == QUEST_STATUS_INCOMPLETE))
                {
                    m_spare = true;
                    break;
                }
            }
        }

        if (m_spare && !m_spareSaid)
        {
            m_spareSaid = true;
            Note("Thaurissan: the quest needs Moira alive. Emperor first, Moira held above 30%.");
            Say(EncounterHelpers::Line("voice.boss.moira.spare", VoiceTier::Essential));
        }
    }

    void BlackrockDepthsThaurissan::ShapeDoctrine(WorldModel const& /*world*/, TeamPlan& plan)
    {
        if (!m_spare || m_moira.IsEmpty())
            return;
        DoctrineEnemy& moira = plan.doctrine.Enemy(m_moira);
        moira.stamps |= STAMP_DAMAGE_LIMIT;
        moira.limit.stopPct = SPARE_STOP_PCT;
        moira.limit.keepPct = SPARE_KEEP_PCT;
        plan.doctrine.SetKillTier(m_moira, KILL_TIER_LAST, "thaurissan: the quest needs Moira alive");
    }
}
