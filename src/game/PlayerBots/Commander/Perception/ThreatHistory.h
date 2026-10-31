/*
 * PartyBot Commander layer 1: facts that need more than one snapshot.
 *  - threat per second of every party-side unit on every enemy
 *  - since when an enemy has been hitting its current victim
 * Pure data, one instance per PartyCommander.
 */

#ifndef MANGOS_COMMANDER_THREAT_HISTORY_H
#define MANGOS_COMMANDER_THREAT_HISTORY_H

#include "Commander/Perception/WorldModel.h"
#include <deque>

namespace Commander
{
    class ThreatHistory
    {
    public:
        // Rate is measured against the newest sample at least this old.
        static uint32 const MIN_WINDOW_MS = 1500;
        // Samples older than this are dropped.
        static uint32 const MAX_WINDOW_MS = 5000;

        // Fills threatPerSec and victimSinceMs in `world` (just rebuilt), then stores it as a sample.
        // `previous` is the model before the rebuild.
        void Annotate(WorldModel& world, WorldModel const& previous, uint32 nowMs);

    private:
        struct Row
        {
            ObjectGuid enemy;
            ObjectGuid unit;
            float threat;
        };
        struct Sample
        {
            uint32 timeMs;
            std::vector<Row> rows;
        };
        std::deque<Sample> m_samples;
    };
}

#endif
