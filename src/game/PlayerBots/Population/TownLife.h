/*
 * World population bots: a city or town bot's day there (user 2026-11-19: bots stood or sat at
 * one spot for their whole life, "like posts", and movers ran about with no purpose).
 *
 * The bot goes from place to place of its city or town (SpawnRequest::places: the director's
 * anchors). At each one it stays a while and does what people do there (Life/Pastimes/
 * PlaceActs.h: bows to the trainer, fiddles at the mailbox, hammers at the anvil, sits on a
 * chair or by the fire), then picks the next place: by the place's visit weight, nearer places
 * preferred, never the one it just left or the two before. It walks a few steps, runs farther
 * (Life/Errands/Trip/Approach.h). A stand spot somebody already takes, or a taken chair, is
 * skipped. The role of the spawn only decides where the day starts: a stander or a sitter at
 * its anchor, a mover on its way to the first place. Some bots notice a real player who walks
 * by (Life/Pastimes/Notice.h).
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_POPULATION_TOWN_LIFE_H
#define MANGOS_POPULATION_TOWN_LIFE_H

#include "Common.h"
#include "Population/PopulationTypes.h"
#include "Life/Errands/Trip/Approach.h"
#include "Life/Pastimes/Notice.h"
#include "Life/Pastimes/PlaceActs.h"
#include <deque>

class Player;

namespace Population
{
    class TownLife
    {
    public:
        // Walking up to here: a few steps (and walking, not running, most of the time).
        static constexpr float WALK_YD = 18.0f;
        static constexpr uint32 WALK_PCT = 65;          // within WALK_YD
        static constexpr uint32 RUN_FAR_PCT = 85;       // beyond
        // Nearer places are likelier: weight x NEAR_YD / (NEAR_YD + distance).
        static constexpr float NEAR_YD = 60.0f;
        // Somebody stands this close to a stand spot: taken.
        static constexpr float SPOT_TAKEN_YD = 1.2f;
        static constexpr size_t RECENT_KEEP = 3;
        static constexpr uint32 RETRY_MS = 10 * IN_MILLISECONDS;   // nowhere to go: try again then

        // The day starts: at the request's anchor (stander, sitter: it stands there now), else
        // on the way to the first place.
        void Begin(Player* me, SpawnRequest const& request, uint32 now);
        void Update(Player* me, uint32 now);
        // Ends the stay or the walk (it leaves, a review); Update goes on with the next place.
        void Stop(Player* me);

        bool IsStarted() const { return m_started; }

    private:
        enum class Step : uint8 { Choosing, Walking, Staying };

        bool PickNext(Player* me, uint32 now);
        bool PickSpot(Player* me, PlaceAnchor const& place, Life::Trip::Destination& out) const;
        void StartStay(Player* me, uint32 now);

        bool m_started = false;
        PlaceMenu m_places;
        Step m_step = Step::Choosing;
        int32 m_current = -1;               // index in m_places
        std::deque<int32> m_recent;
        uint32 m_nextPickMs = 0;
        Life::Trip::Destination m_dest;
        Life::Trip::ApproachState m_leg;
        Life::Pastimes::ActState m_act;
        Life::Pastimes::NoticeState m_notice;
        bool m_wasLooking = false;
    };
}

#endif
