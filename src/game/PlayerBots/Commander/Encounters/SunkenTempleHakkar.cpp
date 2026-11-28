/*
 * Avatar of Hakkar event module (header comment: event, match, eyes, doctrine, duties, limits).
 */

#include "Commander/Encounters/SunkenTempleHakkar.h"
#include "Commander/Perception/Tanks.h"
#include "TeamBotAI.h"
#include "Creature.h"
#include "InstanceData.h"
#include "Map.h"
#include "Player.h"
#include <algorithm>
#include <list>

namespace Commander
{
    bool SunkenTempleHakkar::IsEventEntry(uint32 entry)
    {
        return entry == NPC_AVATAR || entry == NPC_BLOODKEEPER || entry == NPC_MINION || entry == NPC_SUPPRESSOR;
    }

    bool SunkenTempleHakkar::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        if (m_eventOn)
            return true;
        return std::any_of(world.enemies.begin(), world.enemies.end(), [](EnemyFacts const& e)
        {
            return e.entry == NPC_AVATAR || (e.summoned && IsEventEntry(e.entry));
        });
    }

    void SunkenTempleHakkar::OnReset()
    {
        *this = SunkenTempleHakkar();
    }

    void SunkenTempleHakkar::Refresh(TeamBotAI* bot, WorldModel const& world)
    {
        uint32 const now = world.refreshedAtMs;
        bool const fighting = m_waves.Update(world);

        size_t const before = m_suppressors.size();
        m_suppressors.clear();
        m_eventOn = false;
        if (bot && bot->me->IsInWorld())
        {
            Map* map = bot->me->GetMap();
            if (InstanceData* data = map->GetInstanceData())
            {
                m_eventOn = data->GetData(DATA_AVATAR) == STATE_IN_PROGRESS;
                if (Creature const* shade = map->GetCreature(ObjectGuid(data->GetData64(NPC_SHADE))))
                {
                    m_shadePos.x = shade->GetPositionX();
                    m_shadePos.y = shade->GetPositionY();
                    m_shadePos.z = shade->GetPositionZ();
                    m_haveShade = true;
                }
            }
            std::list<Creature*> found;
            bot->me->GetCreatureListWithEntryInGrid(found, NPC_SUPPRESSOR, SEARCH_YD);
            for (Creature* c : found)
                if (c->IsAlive())
                    m_suppressors.push_back(c->GetObjectGuid());
        }

        if (m_eventOn && !m_startNoted)
        {
            m_startNoted = true;
            Note("Hakkar: Nightmare Suppressors walk in from the doors every minute or two: the bots kill them first. "
                 "Bloodkeepers drop the blood for the flames: put the flames out yourself. No sitting down until the Avatar dies.");
        }
        if (before == 0 && !m_suppressors.empty())
        {
            Note("Hakkar: Nightmare Suppressors are coming, kill them before they reach the Shade.");
            Say(EncounterHelpers::Line("voice.boss.hakkar.suppressors", VoiceTier::Urgent));
        }

        m_noRest = fighting || (m_eventOn && m_waves.QuietMs(now) < QUIET_STALE_MS);
    }

    void SunkenTempleHakkar::ListEnemies(TeamBotAI* bot, WorldModel const& /*world*/, std::vector<Creature*>& out) const
    {
        if (!bot || !bot->me->IsInWorld())
            return;
        Map* map = bot->me->GetMap();
        for (ObjectGuid const& guid : m_suppressors)
            if (Creature* c = map->GetCreature(guid))
                out.push_back(c);
    }

    void SunkenTempleHakkar::ShapeDoctrine(WorldModel const& world, TeamPlan& plan)
    {
        for (EnemyFacts const& e : world.enemies)
        {
            if (!e.health || e.onAllyOnly)
                continue;
            if (e.entry == NPC_SUPPRESSOR)
                plan.doctrine.SetKillTier(e.guid, TIER_SUPPRESSOR, "hakkar: suppressor, the event fails if it reaches the shade");
            else if (e.entry == NPC_BLOODKEEPER)
                plan.doctrine.SetKillTier(e.guid, TIER_BLOODKEEPER, "hakkar: bloodkeeper, its blood puts out a flame");
        }
    }

    void SunkenTempleHakkar::OfferDuties(WorldModel const& world, TeamPlan const& /*plan*/,
                                         std::vector<DutyNeed>& needs) const
    {
        PartyMemberFacts const* tank = Tanks::Main(world);
        ObjectGuid const skip = tank ? tank->guid : ObjectGuid();
        Position3 const tieTo = m_haveShade ? m_shadePos : (tank ? tank->pos : Position3());
        for (EnemyFacts const& e : world.targeted)
            if (e.listed && e.entry == NPC_SUPPRESSOR && e.health)
                EncounterHelpers::AddKillNeed(world, e, SUPPRESSOR_POINTS, "hakkar: suppressor walking to the shade",
                                              tieTo, skip, needs);
    }
}
