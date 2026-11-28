/*
 * Captain Kromcrush module (header comment: script, match, doctrine).
 */

#include "Commander/Encounters/DireMaulKromcrush.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include "TeamBotAI.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"

namespace Commander
{
    bool DireMaulKromcrush::Matches(WorldModel const& world, uint32 mapId) const
    {
        return mapId == MAP_ID && EncounterHelpers::HasEnemyEntry(world, NPC_KROMCRUSH);
    }

    void DireMaulKromcrush::OnReset()
    {
        *this = DireMaulKromcrush();
    }

    void DireMaulKromcrush::Refresh(TeamBotAI* bot, WorldModel const& world)
    {
        EnemyFacts const* boss = EncounterHelpers::FindEnemyByEntry(world, NPC_KROMCRUSH);
        m_boss = boss ? boss->guid : ObjectGuid();
        m_retaliation = false;
        if (boss && bot && bot->me->IsInWorld())
            if (Creature const* c = bot->me->GetMap()->GetCreature(boss->guid))
                m_retaliation = c->HasAura(SPELL_RETALIATION);

        if (m_retaliation && !m_said)
        {
            m_said = true;
            Note("Kromcrush: Retaliation up for 15 s, no melee on him.");
            Say(EncounterHelpers::Line("voice.boss.kromcrush.retaliation", VoiceTier::Urgent));
        }
    }

    void DireMaulKromcrush::ShapeDoctrine(WorldModel const& /*world*/, TeamPlan& plan)
    {
        if (m_retaliation && !m_boss.IsEmpty())
            plan.doctrine.Enemy(m_boss).stamps |= STAMP_NO_MELEE;
    }
}
