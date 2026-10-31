#include "Population/PopulationMgr.h"
#include "Population/PopulationBotAI.h"
#include "PlayerBotMgr.h"
#include "Player.h"
#include "Config/Config.h"
#include "Log.h"
#include "Utilities/Random.h"
#include <algorithm>

namespace Population
{
    namespace
    {
        int64 SteadyNowMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        uint32 Clamp(int32 value, uint32 lo, uint32 hi)
        {
            if (value < int32(lo))
                return lo;
            if (uint32(value) > hi)
                return hi;
            return uint32(value);
        }

        Player* OnlinePlayer(BotHandle const& handle)
        {
            if (!handle.alive || !handle.ai)
                return nullptr;
            // After logout the AI can outlive its Player until the session is deleted;
            // the entry state turns OFFLINE before the Player goes away.
            if (!handle.ai->botEntry || handle.ai->botEntry->state != PB_STATE_ONLINE)
                return nullptr;
            Player* me = handle.ai->me;
            return (me && me->IsInWorld()) ? me : nullptr;
        }
    }

    char const* BotKindName(BotKind kind)
    {
        switch (kind)
        {
            case BotKind::Test:  return "test";
            case BotKind::City:  return "city";
            case BotKind::Field: return "field";
        }
        return "?";
    }

    char const* GearTierName(GearTier tier)
    {
        switch (tier)
        {
            case GearTier::Normal:   return "normal";
            case GearTier::PreRaid:  return "preraid";
            case GearTier::Showcase: return "showcase";
        }
        return "?";
    }

    PopulationMgr& PopulationMgr::Instance()
    {
        static PopulationMgr instance;
        return instance;
    }

    PopulationMgr::PopulationMgr()
    {
        m_statResetAt.store(SteadyNowMs());
    }

    void PopulationMgr::LoadConfig()
    {
        bool const wasEnabled = IsEnabled();

        m_enabled.store(sConfig.GetBoolDefault("PopBot.Enable", false));
        m_updateMs.store(Clamp(sConfig.GetIntDefault("PopBot.UpdateMs", 1000), 200, 5000));
        m_maxBots = Clamp(sConfig.GetIntDefault("PopBot.MaxBots", 100), 0, 500);
        m_spawnGapMs = Clamp(sConfig.GetIntDefault("PopBot.SpawnGapMs", 3000), 500, 60000);

        uint32 const minMinutes = Clamp(sConfig.GetIntDefault("PopBot.LifetimeMinMinutes", 10), 1, 24 * 60);
        uint32 const maxMinutes = Clamp(sConfig.GetIntDefault("PopBot.LifetimeMaxMinutes", 40), minMinutes, 24 * 60);
        m_lifetimeMinMs = minMinutes * MINUTE * IN_MILLISECONDS;
        m_lifetimeMaxMs = maxMinutes * MINUTE * IN_MILLISECONDS;

        if (wasEnabled && !IsEnabled())
        {
            m_queue.clear();
            DespawnAll();
        }
    }

    void PopulationMgr::LoadData()
    {
        m_identity.Load();
        m_points.Load();
        m_dataLoaded = true;
    }

    void PopulationMgr::EnsureDataLoaded()
    {
        if (!m_dataLoaded)
            LoadData();
    }

    void PopulationMgr::SetEnabled(bool on)
    {
        m_enabled.store(on);
        if (!on)
        {
            m_queue.clear();
            DespawnAll();
        }
    }

    uint32 PopulationMgr::RandomLifetimeMs() const
    {
        return urand(m_lifetimeMinMs, m_lifetimeMaxMs);
    }

    void PopulationMgr::Update(uint32 diff)
    {
        m_nowMs += diff;

        // Forget bots whose AI is gone (logged out and erased by PlayerBotMgr, or failed to load).
        m_bots.erase(std::remove_if(m_bots.begin(), m_bots.end(),
            [](Record const& r) { return !r.handle->alive; }), m_bots.end());

        if (!IsEnabled())
            return;

        EnsureDataLoaded();

        for (Record& r : m_bots)
            if (!r.removing && r.expireMs && m_nowMs >= r.expireMs)
                RequestRemoval(r);

        if (!m_queue.empty() && m_nowMs >= m_nextSpawnMs)
        {
            SpawnRequest const request = m_queue.front();
            m_queue.pop_front();
            if (GetBotCount() < m_maxBots)
                SpawnNow(request);
            m_nextSpawnMs = m_nowMs + m_spawnGapMs;
        }
    }

    bool PopulationMgr::QueueSpawn(SpawnRequest const& request)
    {
        if (!IsEnabled())
            return false;
        if (GetBotCount() + m_queue.size() >= m_maxBots)
            return false;
        m_queue.push_back(request);
        return true;
    }

    void PopulationMgr::SpawnNow(SpawnRequest const& request)
    {
        SpawnRequest r = request;

        if (!r.race)
            r.race = Identity::RandomRace(r.team == HORDE ? HORDE : ALLIANCE);
        if (!r.classId || !Identity::IsValidCombo(r.race, r.classId))
            r.classId = Identity::RandomClass(r.race);
        if (!r.classId)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[PopBot] No valid class for race %u.", r.race);
            return;
        }

        if (r.tier == GearTier::Showcase)
            r.level = 60;
        else if (!r.level)
            r.level = Identity::RandomCityLevel();
        r.level = uint8(std::min<uint32>(std::max<uint32>(r.level, 1), 60));

        std::set<std::string> reserved;
        for (Record const& rec : m_bots)
            reserved.insert(rec.name);

        std::string const name = m_identity.MakeName(r.race, reserved);
        if (name.empty())
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[PopBot] Could not find a free name for race %u.", r.race);
            return;
        }

        auto handle = std::make_shared<BotHandle>();
        // PlayerBotMgr takes ownership of the AI (also when AddBot fails).
        PopulationBotAI* ai = new PopulationBotAI(r, name, handle);
        if (!sPlayerBotMgr.AddBot(ai))
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[PopBot] PlayerBotMgr::AddBot failed for '%s'.", name.c_str());
            return;
        }

        Record rec;
        rec.handle = std::move(handle);
        rec.request = r;
        rec.name = name;
        rec.bornMs = m_nowMs;
        rec.expireMs = m_nowMs + (r.lifetimeMs ? r.lifetimeMs : RandomLifetimeMs());
        m_bots.push_back(std::move(rec));
    }

    void PopulationMgr::RequestRemoval(Record& record)
    {
        record.removing = true;
        if (record.handle->alive && record.handle->ai && record.handle->ai->botEntry)
            record.handle->ai->botEntry->requestRemoval = true;
    }

    uint32 PopulationMgr::DespawnAll()
    {
        uint32 count = 0;
        for (Record& r : m_bots)
        {
            if (r.removing)
                continue;
            RequestRemoval(r);
            ++count;
        }
        return count;
    }

    bool PopulationMgr::Despawn(ObjectGuid guid)
    {
        for (Record& r : m_bots)
        {
            Player* me = OnlinePlayer(*r.handle);
            if (me && me->GetObjectGuid() == guid)
            {
                if (!r.removing)
                    RequestRemoval(r);
                return true;
            }
        }
        return false;
    }

    uint32 PopulationMgr::GetBotCount() const
    {
        return uint32(std::count_if(m_bots.begin(), m_bots.end(), [](Record const& r) { return !r.removing; }));
    }

    std::vector<BotInfo> PopulationMgr::ListBots() const
    {
        std::vector<BotInfo> list;
        for (Record const& r : m_bots)
        {
            BotInfo info;
            info.name = r.name;
            info.kind = r.request.kind;
            info.tier = r.request.tier;
            info.race = r.request.race;
            info.classId = r.request.classId;
            info.level = r.request.level;
            info.removing = r.removing;
            info.ageMs = m_nowMs - r.bornMs;
            info.leftMs = r.expireMs > m_nowMs ? r.expireMs - m_nowMs : 0;
            info.mapId = r.request.mapId;
            info.x = r.request.x;
            info.y = r.request.y;
            info.z = r.request.z;
            if (Player* me = OnlinePlayer(*r.handle))
            {
                info.online = true;
                info.guid = me->GetObjectGuid();
                info.level = uint8(me->GetLevel());
                info.mapId = me->GetMapId();
                info.x = me->GetPositionX();
                info.y = me->GetPositionY();
                info.z = me->GetPositionZ();
            }
            list.push_back(std::move(info));
        }
        return list;
    }

    void PopulationMgr::RecordBotTick(uint64 micros)
    {
        m_statTicks.fetch_add(1, std::memory_order_relaxed);
        m_statTotalMicros.fetch_add(micros, std::memory_order_relaxed);
        uint64 prev = m_statMaxMicros.load(std::memory_order_relaxed);
        while (micros > prev && !m_statMaxMicros.compare_exchange_weak(prev, micros, std::memory_order_relaxed))
            ;
    }

    TickStats PopulationMgr::GetStats() const
    {
        TickStats s;
        s.ticks = m_statTicks.load(std::memory_order_relaxed);
        s.totalMicros = m_statTotalMicros.load(std::memory_order_relaxed);
        s.maxMicros = m_statMaxMicros.load(std::memory_order_relaxed);
        s.elapsedMs = uint64(std::max<int64>(0, SteadyNowMs() - m_statResetAt.load(std::memory_order_relaxed)));
        return s;
    }

    void PopulationMgr::ResetStats()
    {
        m_statTicks.store(0);
        m_statTotalMicros.store(0);
        m_statMaxMicros.store(0);
        m_statResetAt.store(SteadyNowMs());
    }

    BotTickCostScope::~BotTickCostScope()
    {
        auto const micros = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - m_start).count();
        sPopulationMgr.RecordBotTick(uint64(micros));
    }
}
