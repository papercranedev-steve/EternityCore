/*
 * PartyBot Commander layer 1 (eyes): an event with waves (boss doc 2.3, user 2026-11-28):
 * what each wave costs in mana, and what the waves still to come will take of a member's mana.
 * The boss module tells the event (WorldModel::waves, IEncounter::DescribeWaves); WaveSpend
 * measures the waves of this event; FutureMana reads both.
 *
 * Wave cost of a member: its mana spent (every drop between two refreshes, gross: drinking
 * between waves does not hide it) since the event started / the waves fought (the one being
 * fought counts half). Before a wave was fought: its average fight here
 * (PartyMemberFacts::fightManaShare x its pool; players: unknown, 0).
 *
 * FutureMana(member) = waves left x max(0, wave cost - what one gap gives back):
 *   Player       0: the party drinks to full between waves (the player starts the next one).
 *   Timer        a gap gives back the drink in (interval - one wave's fight - SIT_SEC -
 *                STAND_LEAD_SEC), when it has something to drink; the fight is measured
 *                (WaveEventFacts::waveFightSec), else half the interval.
 *   Continuous   nothing comes back.
 * Not counted: regeneration out of the drink, mana potions and gems (user 2026-11-28: no
 * early use of those), the gap before the next wave being already partly over.
 */

#ifndef MANGOS_COMMANDER_WAVE_EVENT_H
#define MANGOS_COMMANDER_WAVE_EVENT_H

#include "Commander/Perception/WorldModel.h"
#include <vector>

namespace Commander
{
    namespace WaveEvent
    {
        // A drink that starts needs this long before it pays: sitting, the first tick after
        // 3 s, one more (untested guess, 2026-11-28). Below it nobody sits down.
        static constexpr float SIT_SEC = 6.0f;
        // Sitting bots stand up this long before the next wave reaches them (untested guess):
        // a healer on its feet when the first hit lands.
        static constexpr float STAND_LEAD_SEC = 3.0f;
        // A wave being fought counts as this much of a wave fought.
        static constexpr float WAVE_IN_PROGRESS_SHARE = 0.5f;

        // Mana the waves still to come will cost `member` on top of what it can drink back in
        // the gaps (see the header). 0 without an event, for a player-paced one, or unknown.
        float FutureMana(WorldModel const& world, PartyMemberFacts const& member);
    }

    // Measures the waves of an event (WaveEventFacts::waveMana, waveFightSec). One per
    // PartyCommander; after the boss module described the event, each refresh.
    class WaveSpend
    {
    public:
        void Annotate(WorldModel& world, uint32 nowMs);

    private:
        struct Member
        {
            ObjectGuid guid;
            float lastMana = 0.0f;
            float spent = 0.0f;
        };
        bool m_active = false;
        uint32 m_lastMs = 0;
        float m_fightSec = 0.0f;
        std::vector<Member> m_members;
    };
}

#endif
