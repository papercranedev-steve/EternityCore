/*
 * PartyBot Commander layer 1: fight-wide rates (value-scoring doc section 3).
 *  - incoming melee damage per second, after armor
 *  - damage the party deals per second, measured from enemy health over a few seconds
 * Pure data, one instance per PartyCommander. Runs after ThreatHistory::Annotate.
 * Enemies fighting only an ally (EnemyFacts::onAllyOnly) are left out: their stored rows are
 * dropped while they are, and measured again from scratch once they fight the party.
 */

#ifndef MANGOS_COMMANDER_COMBAT_RATES_H
#define MANGOS_COMMANDER_COMBAT_RATES_H

#include "Commander/Perception/WorldModel.h"
#include <deque>

namespace Commander
{
    class CombatRates
    {
    public:
        static uint32 const MIN_WINDOW_MS = 2000;
        static uint32 const MAX_WINDOW_MS = 6000;

        // Fills world.rates from the freshly built model and stores a sample.
        void Annotate(WorldModel& world, uint32 nowMs);

    private:
        struct Row
        {
            ObjectGuid enemy;
            float health;
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
