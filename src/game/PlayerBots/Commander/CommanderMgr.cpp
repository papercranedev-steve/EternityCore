#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "PartyBotAI.h"
#include "Config/Config.h"
#include "Group.h"
#include "Map.h"
#include "Timer.h"
#include <algorithm>
#include <sstream>

namespace Commander
{
    namespace
    {
        uint32 const SWEEP_INTERVAL_MS = 10 * IN_MILLISECONDS;
        uint32 const COMMANDER_IDLE_MS = 60 * IN_MILLISECONDS;

        uint32 const MIN_BOT_UPDATE_MS = 100;
        uint32 const MAX_BOT_UPDATE_MS = 5000;

        int64 SteadyNowMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        uint32 Clamp(int32 value, uint32 lo, uint32 hi)
        {
            if (value < int32(lo))
                return lo;
            if (value > int32(hi))
                return hi;
            return uint32(value);
        }
    }

    CommanderMgr& CommanderMgr::Instance()
    {
        static CommanderMgr instance;
        return instance;
    }

    CommanderMgr::CommanderMgr()
    {
        RegisterBehaviors(m_behaviors);
        m_statResetAt.store(SteadyNowMs());
        LoadConfig();
    }

    void CommanderMgr::LoadConfig()
    {
        m_enabled.store(sConfig.GetBoolDefault("PartyBot.Commander.Enable", true));
        SetBotUpdateInterval(Clamp(sConfig.GetIntDefault("PartyBot.Commander.BotUpdateMs", 1000), MIN_BOT_UPDATE_MS, MAX_BOT_UPDATE_MS));
        m_perceptionMs.store(Clamp(sConfig.GetIntDefault("PartyBot.Commander.PerceptionMs", 200), 50, 2000));

        // Comma or space separated behavior ids.
        std::string disabled = sConfig.GetStringDefault("PartyBot.Commander.DisabledModules", "");
        for (char& c : disabled)
            if (c == ',')
                c = ' ';
        std::istringstream iss(disabled);
        std::vector<std::string> ids;
        for (std::string id; iss >> id;)
            ids.push_back(id);

        for (auto const& slot : m_behaviors)
        {
            bool off = std::find(ids.begin(), ids.end(), slot->behavior->GetId()) != ids.end();
            slot->enabled.store(!off);
        }
    }

    void CommanderMgr::SetBotUpdateInterval(uint32 ms)
    {
        if (ms < MIN_BOT_UPDATE_MS)
            ms = MIN_BOT_UPDATE_MS;
        if (ms > MAX_BOT_UPDATE_MS)
            ms = MAX_BOT_UPDATE_MS;
        m_botUpdateMs.store(ms, std::memory_order_relaxed);
    }

    std::shared_ptr<PartyCommander> CommanderMgr::GetCommander(PartyBotAI* bot)
    {
        Player* me = bot->me;
        Group* pGroup = me->GetGroup();
        if (!pGroup || !me->IsInWorld())
            return nullptr;

        uint32 const now = WorldTimer::getMSTime();
        Key const key(pGroup->GetId(), me->GetMapId(), me->GetInstanceId());

        std::lock_guard<std::mutex> guard(m_commandersLock);

        if (WorldTimer::getMSTimeDiff(m_lastSweepMs, now) >= SWEEP_INTERVAL_MS)
        {
            m_lastSweepMs = now;
            for (auto itr = m_commanders.begin(); itr != m_commanders.end();)
            {
                if (WorldTimer::getMSTimeDiff(itr->second.lastUsedMs, now) >= COMMANDER_IDLE_MS)
                    itr = m_commanders.erase(itr);
                else
                    ++itr;
            }
        }

        Entry& entry = m_commanders[key];
        if (!entry.commander)
            entry.commander = std::make_shared<PartyCommander>(std::get<0>(key), std::get<1>(key), std::get<2>(key));
        entry.lastUsedMs = now;
        return entry.commander;
    }

    std::shared_ptr<PartyCommander> CommanderMgr::FindCommander(Player const* member)
    {
        Group const* pGroup = member ? member->GetGroup() : nullptr;
        if (!pGroup)
            return nullptr;

        Key const key(pGroup->GetId(), member->GetMapId(), member->GetInstanceId());
        std::lock_guard<std::mutex> guard(m_commandersLock);
        auto itr = m_commanders.find(key);
        return itr != m_commanders.end() ? itr->second.commander : nullptr;
    }

    size_t CommanderMgr::GetCommanderCount()
    {
        std::lock_guard<std::mutex> guard(m_commandersLock);
        return m_commanders.size();
    }

    void CommanderMgr::OnBotUpdate(PartyBotAI* bot)
    {
        if (!IsEnabled())
            return;

        if (std::shared_ptr<PartyCommander> commander = GetCommander(bot))
            commander->Update(bot, WorldTimer::getMSTime(), GetPerceptionInterval());
    }

    bool CommanderMgr::OnBotCombatUpdate(PartyBotAI* bot)
    {
        if (!IsEnabled() || bot->IsInDuel())
            return false;

        if (std::shared_ptr<PartyCommander> commander = GetCommander(bot))
            return commander->RunCombat(bot, m_behaviors);

        return false;
    }

    IBehavior const* CommanderMgr::FindBehavior(std::string const& id, BehaviorSlot** slotOut)
    {
        for (auto const& slot : m_behaviors)
        {
            if (id == slot->behavior->GetId())
            {
                if (slotOut)
                    *slotOut = slot.get();
                return slot->behavior.get();
            }
        }
        return nullptr;
    }

    void CommanderMgr::RecordBotTick(uint64 micros)
    {
        m_statTicks.fetch_add(1, std::memory_order_relaxed);
        m_statTotalMicros.fetch_add(micros, std::memory_order_relaxed);

        uint64 prevMax = m_statMaxMicros.load(std::memory_order_relaxed);
        while (micros > prevMax && !m_statMaxMicros.compare_exchange_weak(prevMax, micros, std::memory_order_relaxed))
        {
        }
    }

    TickStats CommanderMgr::GetStats() const
    {
        TickStats s;
        s.ticks = m_statTicks.load(std::memory_order_relaxed);
        s.totalMicros = m_statTotalMicros.load(std::memory_order_relaxed);
        s.maxMicros = m_statMaxMicros.load(std::memory_order_relaxed);
        s.elapsedMs = uint64(SteadyNowMs() - m_statResetAt.load(std::memory_order_relaxed));
        return s;
    }

    void CommanderMgr::ResetStats()
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
        sCommanderMgr.RecordBotTick(uint64(micros));
    }
}
