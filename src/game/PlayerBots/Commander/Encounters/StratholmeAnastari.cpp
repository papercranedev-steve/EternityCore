/*
 * Anastari boss module (header comment: script, match, doctrine, limits).
 */

#include "Commander/Encounters/StratholmeAnastari.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include <algorithm>
#include <cstdio>

namespace Commander
{
    bool StratholmeAnastari::IsPossessed(PartyMemberFacts const& m)
    {
        return m.charmed && !m.isPet && m.alive && m.charmer.GetEntry() == NPC_ANASTARI;
    }

    bool StratholmeAnastari::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        if (EncounterHelpers::HasEnemyEntry(world, NPC_ANASTARI))
            return true;
        return std::any_of(world.party.begin(), world.party.end(), &IsPossessed);
    }

    void StratholmeAnastari::OnReset()
    {
        *this = StratholmeAnastari();
    }

    void StratholmeAnastari::ShapeDoctrine(WorldModel const& world, TeamPlan& plan)
    {
        for (PartyMemberFacts const& m : world.party)
        {
            if (!IsPossessed(m))
            {
                m_noted.erase(std::remove(m_noted.begin(), m_noted.end(), m.guid), m_noted.end());
                continue;
            }

            DoctrineMember dm;
            dm.guid = m.guid;
            dm.stamps = MEMBER_HIT_TO_FREE | MEMBER_DAMAGE_LIMIT;
            dm.limit.stopPct = STOP_PCT;
            dm.limit.keepPct = KEEP_PCT;
            dm.limit.windowSec = WINDOW_SEC;
            dm.why = "anastari: possessed, freed below 25%";
            plan.doctrine.members.push_back(dm);

            // No control on the ally (AllyCharm's hold): she cannot be killed meanwhile.
            for (CharmedAlly& c : plan.doctrine.charmed)
                if (c.ally == m.guid)
                    c.hold = false;

            if (std::find(m_noted.begin(), m_noted.end(), m.guid) == m_noted.end())
            {
                m_noted.push_back(m.guid);
                char buf[160];
                snprintf(buf, sizeof(buf), "Anastari possessed a member at %.0f%%: hitting it down below %.0f%% frees it.",
                         m.healthPct, FREE_BELOW_PCT);
                Note(buf);
            }
        }
    }
}
