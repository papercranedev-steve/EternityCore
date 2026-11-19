/*
 * World population bots: scheduler, configuration and statistics.
 * Design: .kiro/docs/2026-11-01-world-population-bots.md (workspace root).
 *
 * Threads: LoadConfig / Update / commands run on the world thread (Update is called from
 * PlayerBotMgr::Update, GM commands while map threads are idle). Bot AIs (map threads)
 * only read the atomics and call RecordBotTick.
 */

#ifndef MANGOS_POPULATION_MGR_H
#define MANGOS_POPULATION_MGR_H

#include "Common.h"
#include "Population/PopulationTypes.h"
#include "Population/PopulationIdentity.h"
#include "Population/ScenePoints.h"
#include "Population/CityPopulation.h"
#include "Population/FieldPopulation.h"
#include "Population/Observers.h"
#include "Population/Roster.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Population
{
    struct BotInfo
    {
        ObjectGuid guid;
        std::string name;
        BotKind kind = BotKind::Test;
        GearTier tier = GearTier::Normal;
        CityRole role = CityRole::None;
        uint32 cityZone = 0;
        uint8 race = 0, classId = 0, level = 0;
        uint32 rosterId = 0;        // 0 = random stranger
        bool online = false;
        bool removing = false;
        uint32 mapId = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        uint32 ageMs = 0;
        uint32 leftMs = 0;          // until the lifetime ends
    };

    struct TickStats
    {
        uint64 ticks = 0;
        uint64 totalMicros = 0;
        uint64 maxMicros = 0;
        uint64 elapsedMs = 0;
    };

    class PopulationMgr
    {
    public:
        static PopulationMgr& Instance();

        // Reads PopBot.* (called from PlayerBotMgr::LoadConfig, also on .reload config).
        void LoadConfig();
        // Re-reads names and scene points from the world database.
        void LoadData();
        void Update(uint32 diff);

        bool IsEnabled() const { return m_enabled.load(std::memory_order_relaxed); }
        // Off despawns every population bot and drops the queue.
        void SetEnabled(bool on);
        uint32 GetUpdateInterval() const { return m_updateMs.load(std::memory_order_relaxed); }
        uint32 GetMaxBots() const { return m_maxBots; }

        // Adds to the spawn queue (one bot every PopBot.SpawnGapMs; prewarm requests go first,
        // PopBot.Prewarm.PerTick per update). False if disabled or full.
        bool QueueSpawn(SpawnRequest const& request);

        // Hooks from the character packet handlers (world thread): fill the world around the
        // character before he enters it (Observers.h).
        void OnCharacterScreen(uint32 accountId);
        void OnLoginRequest(uint32 accountId, uint32 charGuid);
        Observers const& GetObservers() const { return m_observers; }
        // Asks every population bot to log out. Returns how many were asked.
        uint32 DespawnAll();
        bool Despawn(ObjectGuid guid);

        uint32 GetQueuedCount() const { return uint32(m_queue.size()); }
        uint32 GetBotCount() const;     // tracked, not removing
        std::vector<BotInfo> ListBots() const;

        Identity const& GetIdentity() { EnsureDataLoaded(); return m_identity; }
        ScenePoints& GetScenePoints() { EnsureDataLoaded(); return m_points; }
        CityDirector& GetCityDirector() { EnsureDataLoaded(); return m_city; }
        Roster const& GetRoster() { EnsureDataLoaded(); return m_roster; }
        // After scene points were added or deleted.
        void ReloadAnchors() { m_city.LoadAnchors(m_points); }

        // City director helpers. Counts include queued requests.
        CityCounts CountCity(uint32 zone, size_t anchorCount) const;
        // Every bot of the city leaves, queued requests are dropped.
        void DespawnCity(uint32 zone);
        // One bot leaves (oldest stand bot, else oldest mover; veterans last).
        bool DespawnOneCity(uint32 zone);

        // Field director helpers (FieldPopulation.h). Field bots within `radius` (2d) of the
        // observer, queued requests included; for a moving observer not those more than
        // `behindYd` behind him (passed by).
        uint32 CountField(Observer const& o, float radius, float behindYd) const;
        // Every field bot leaves, queued requests are dropped (field off).
        void DespawnField();
        // Field bots out of everyone's sight leave when no observer is within `keepYd` (more
        // than `behindYd` behind a moving one does not count), their lifetime is over, or they
        // are done here (BotHandle::done).
        void RetireFieldBots(Observers const& observers, float keepYd, float behindYd);

        // Cost of PopulationBotAI::UpdateAI ticks.
        void RecordBotTick(uint64 micros);
        TickStats GetStats() const;
        void ResetStats();

    private:
        PopulationMgr();

        struct Record
        {
            std::shared_ptr<BotHandle> handle;
            SpawnRequest request;
            std::string name;
            uint32 bornMs = 0;
            uint32 expireMs = 0;
            bool removing = false;      // logging out, or walking out of sight to log out
            uint32 leaveByMs = 0;       // != 0: walking out (BotHandle::leave), forced out at this time
        };

        void EnsureDataLoaded();
        void SpawnNow(SpawnRequest const& request);
        // City (and later field) bots come from the roster. False: nobody free, skip this spawn.
        bool TakeFromRoster(SpawnRequest& request, std::string& name, uint32& guid);
        void RequestRemoval(Record& record);
        // Lifetime over / city too full: log out, out of sight first when a player is looking.
        void Retire(Record& record);
        // Roster members a real player invited become companions (Companions/CompanionMgr).
        void UpdateRecruits();
        uint32 RandomLifetimeMs() const;

        std::atomic<bool> m_enabled{ false };
        std::atomic<uint32> m_updateMs{ 1000 };
        uint32 m_maxBots = 100;
        uint32 m_spawnGapMs = 3000;
        uint32 m_prewarmPerTick = 4;
        uint32 m_lifetimeMinMs = 10 * MINUTE * IN_MILLISECONDS;
        uint32 m_lifetimeMaxMs = 40 * MINUTE * IN_MILLISECONDS;

        uint32 m_nowMs = 0;
        uint32 m_nextSpawnMs = 0;
        bool m_dataLoaded = false;

        std::vector<Record> m_bots;
        std::deque<SpawnRequest> m_queue;
        Identity m_identity;
        ScenePoints m_points;
        Observers m_observers;
        CityDirector m_city;
        FieldDirector m_field;
        Roster m_roster;
        uint32 m_nextUnitId = 0;        // Commander team units of field bots

        std::atomic<uint64> m_statTicks{ 0 };
        std::atomic<uint64> m_statTotalMicros{ 0 };
        std::atomic<uint64> m_statMaxMicros{ 0 };
        std::atomic<int64> m_statResetAt{ 0 };
    };

    // Put at the top of a population bot AI tick; records how long the tick took.
    class BotTickCostScope
    {
    public:
        BotTickCostScope() : m_start(std::chrono::steady_clock::now()) {}
        ~BotTickCostScope();
    private:
        std::chrono::steady_clock::time_point m_start;
    };
}

#define sPopulationMgr Population::PopulationMgr::Instance()

#endif
