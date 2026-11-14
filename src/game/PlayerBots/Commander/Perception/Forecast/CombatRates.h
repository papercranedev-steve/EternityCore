/*
 * PartyBot Commander layer 1: fight-wide rates (value-scoring doc section 3).
 *  - incoming melee damage per second, after armor
 *  - damage the party deals per second, measured from enemy health over a few seconds
 *  - the same over the whole fight so far, per damage dealer (RateFacts::fightDpsPerDealer)
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

        // A fight at least this long leaves its whole-fight damage per second in
        // RateFacts::lastFightDps for the next pull's estimates.
        static uint32 const MIN_REMEMBERED_FIGHT_MS = 8000;

        // Fills world.rates from the freshly built model and stores a sample.
        void Annotate(WorldModel& world, uint32 nowMs);

    private:
        // Whole fight: enemy health lost from sample to sample since the first one.
        uint32 m_fightStartMs = 0;
        uint32 m_fightLastMs = 0;
        float m_fightLost = 0.0f;
        // RateFacts::fightDpsPerDealer: time something could be hit, and that time x the
        // damage dealers that could act, since the first sample.
        uint32 m_hittableMs = 0;
        float m_dealerSec = 0.0f;
        float m_lastFightDps = 0.0f;
        uint32 m_lastFightDealers = 0;

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
