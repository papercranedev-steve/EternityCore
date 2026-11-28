#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Coordination/Situation/Pull/PullQuestions.h"
#include "Commander/Coordination/Situation/Guide.h"
#include "Commander/Coordination/Situation/Stay.h"
#include "Commander/Coordination/Situation/Summon.h"
#include "Commander/Team.h"
#include "TeamBotAI.h"
#include "Config/Config.h"
#include "Database/DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
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
        RegisterAssigners(m_assigners);
        m_statResetAt.store(SteadyNowMs());
        LoadConfig();
    }

    void CommanderMgr::LoadConfig()
    {
        m_enabled.store(sConfig.GetBoolDefault("PartyBot.Commander.Enable", true));
        m_debugSpeech.store(sConfig.GetBoolDefault("PartyBot.Commander.DebugSpeech", false));
        SetBotUpdateInterval(Clamp(sConfig.GetIntDefault("PartyBot.Commander.BotUpdateMs", 200), MIN_BOT_UPDATE_MS, MAX_BOT_UPDATE_MS));
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

        auto isOff = [&ids](char const* id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
        for (auto const& slot : m_behaviors)
            slot->enabled.store(!isOff(slot->module->GetId()));
        for (auto const& slot : m_assigners)
            slot->enabled.store(!isOff(slot->module->GetId()));
    }

    void CommanderMgr::SetBotUpdateInterval(uint32 ms)
    {
        if (ms < MIN_BOT_UPDATE_MS)
            ms = MIN_BOT_UPDATE_MS;
        if (ms > MAX_BOT_UPDATE_MS)
            ms = MAX_BOT_UPDATE_MS;
        m_botUpdateMs.store(ms, std::memory_order_relaxed);
    }

    std::shared_ptr<PartyCommander> CommanderMgr::GetCommander(TeamBotAI* bot)
    {
        Player* me = bot->me;
        uint64 const team = Team::KeyOf(me);
        if (!team || !me->IsInWorld())
            return nullptr;

        uint32 const now = WorldTimer::getMSTime();
        Key const key(team, me->GetMapId(), me->GetInstanceId());

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
        uint64 const team = Team::KeyOf(member);
        if (!team)
            return nullptr;

        Key const key(team, member->GetMapId(), member->GetInstanceId());
        std::lock_guard<std::mutex> guard(m_commandersLock);
        auto itr = m_commanders.find(key);
        return itr != m_commanders.end() ? itr->second.commander : nullptr;
    }

    size_t CommanderMgr::GetCommanderCount()
    {
        std::lock_guard<std::mutex> guard(m_commandersLock);
        return m_commanders.size();
    }

    void CommanderMgr::OnBotUpdate(TeamBotAI* bot)
    {
        if (!IsEnabled())
            return;

        if (std::shared_ptr<PartyCommander> commander = GetCommander(bot))
            commander->Update(bot, WorldTimer::getMSTime(), GetPerceptionInterval(), m_assigners, m_behaviors);
    }

    bool CommanderMgr::OnBotCombatUpdate(TeamBotAI* bot)
    {
        if (!IsEnabled())
            return false;

        if (std::shared_ptr<PartyCommander> commander = GetCommander(bot))
            return commander->RunCombat(bot, m_behaviors);

        return false;
    }

    bool CommanderMgr::WantsOutOfCombatControl(TeamBotAI* bot)
    {
        if (!IsEnabled())
            return false;
        std::shared_ptr<PartyCommander> commander = FindCommander(bot->me);
        return commander && commander->WantsOutOfCombatControl();
    }

    bool CommanderMgr::IsRetreating(TeamBotAI* bot)
    {
        if (!IsEnabled())
            return false;
        std::shared_ptr<PartyCommander> commander = FindCommander(bot->me);
        return commander && commander->GetPlan().retreat.active;
    }

    Player* CommanderMgr::GetGuide(TeamBotAI* bot)
    {
        if (!IsEnabled())
            return nullptr;
        std::shared_ptr<PartyCommander> commander = FindCommander(bot->me);
        return commander ? Guide::Current(commander->GetPlan(), bot->me) : nullptr;
    }

    bool CommanderMgr::GetStay(TeamBotAI const* bot, StaySpot& spot)
    {
        if (!IsEnabled() || !bot || !bot->me)
            return false;
        std::shared_ptr<PartyCommander> commander = FindCommander(bot->me);
        StaySpot const* current = commander ? Stay::Current(commander->GetPlan(), bot->me) : nullptr;
        if (!current)
            return false;
        spot = *current;
        return true;
    }

    bool CommanderMgr::SetStay(Player const* member, bool stay)
    {
        if (!IsEnabled() || !member)
            return false;
        std::shared_ptr<PartyCommander> commander = FindCommander(member);
        if (!commander)
            return false;
        TeamPlan& plan = commander->GetPlanMutable();
        return stay ? Stay::Begin(plan, member) : Stay::End(plan, member);
    }

    bool CommanderMgr::GetPullQuestion(TeamBotAI const* bot, PullQuestion& out)
    {
        if (!IsEnabled() || !bot || !bot->me)
            return false;
        std::shared_ptr<PartyCommander> commander = FindCommander(bot->me);
        return commander && PullQuestions::Open(commander->GetPlan(), out);
    }

    void CommanderMgr::MarkPullQuestionShown(Player const* bot, uint32 id)
    {
        if (!IsEnabled() || !bot)
            return;
        if (std::shared_ptr<PartyCommander> commander = FindCommander(bot))
            PullQuestions::MarkShown(commander->GetPlanMutable(), id);
    }

    void CommanderMgr::AnswerPullQuestion(Player const* bot, uint32 id, bool yes)
    {
        if (!IsEnabled() || !bot)
            return;
        if (std::shared_ptr<PartyCommander> commander = FindCommander(bot))
            PullQuestions::Answer(commander->GetPlanMutable(), id, yes);
    }

    bool CommanderMgr::StartSummon(Player const* warlock, ObjectGuid target, std::vector<ObjectGuid> const& helpers)
    {
        if (!IsEnabled() || !warlock)
            return false;
        std::shared_ptr<PartyCommander> commander = FindCommander(warlock);
        return commander && Summon::Start(commander->GetPlanMutable(), warlock, target, helpers);
    }

    SummonRole CommanderMgr::GetSummonRole(TeamBotAI const* bot, ObjectGuid& warlock, ObjectGuid& target)
    {
        if (!IsEnabled() || !bot || !bot->me)
            return SummonRole::None;
        std::shared_ptr<PartyCommander> commander = FindCommander(bot->me);
        if (!commander)
            return SummonRole::None;
        TeamPlan const& plan = commander->GetPlan();
        ObjectGuid const me = bot->me->GetObjectGuid();
        SummonRole const role = Summon::RoleOf(plan, me);
        if (role != SummonRole::None)
            if (SummonRitual const* r = plan.summon.RitualOf(me))
            {
                warlock = r->warlock;
                target = r->target;
            }
        return role;
    }

    void CommanderMgr::EndSummon(Player const* warlock, bool leaderBack)
    {
        if (!warlock)
            return;
        if (std::shared_ptr<PartyCommander> commander = FindCommander(warlock))
            Summon::End(commander->GetPlanMutable(), warlock->GetObjectGuid(), leaderBack);
    }

    void CommanderMgr::MarkSummonSent(Player const* warlock, ObjectGuid target)
    {
        if (!warlock)
            return;
        if (std::shared_ptr<PartyCommander> commander = FindCommander(warlock))
            Summon::MarkSent(commander->GetPlanMutable(), target, WorldTimer::getMSTime());
    }

    bool CommanderMgr::GatherNow(Player const* leader)
    {
        if (!IsEnabled() || !leader)
            return false;
        std::shared_ptr<PartyCommander> commander = FindCommander(leader);
        if (!commander)
            return false;
        commander->GetPlanMutable().summon.gatherNow = true;
        return true;
    }

    bool CommanderMgr::IsModuleEnabled(std::string const& id) const
    {
        for (auto const& slot : m_assigners)
            if (id == slot->module->GetId() && !slot->enabled.load())
                return false;
        for (auto const& slot : m_behaviors)
            if (id == slot->module->GetId() && !slot->enabled.load())
                return false;
        return true;
    }

    void CommanderMgr::OrderAttack(Player const* player, ObjectGuid enemy, bool tanksOnly)
    {
        std::shared_ptr<PartyCommander> commander = FindCommander(player);
        if (!commander)
            return;
        PlayerOrders& orders = commander->GetPlanMutable().orders;
        orders.attack = enemy;
        orders.attackTanksOnly = tanksOnly;
        orders.attackSinceMs = WorldTimer::getMSTime();
        orders.leaveAlone.erase(std::remove(orders.leaveAlone.begin(), orders.leaveAlone.end(), enemy),
                                orders.leaveAlone.end());
    }

    void CommanderMgr::OrderLeaveAlone(Player const* player, ObjectGuid enemy)
    {
        std::shared_ptr<PartyCommander> commander = FindCommander(player);
        if (!commander)
            return;
        PlayerOrders& orders = commander->GetPlanMutable().orders;
        if (!orders.LeavesAlone(enemy))
            orders.leaveAlone.push_back(enemy);
        if (orders.attack == enemy)
            orders.attack.Clear();
    }

    bool CommanderMgr::SetModuleEnabled(std::string const& id, bool on)
    {
        bool found = false;
        for (auto const& slot : m_behaviors)
        {
            if (id == slot->module->GetId())
            {
                slot->enabled.store(on);
                found = true;
            }
        }
        for (auto const& slot : m_assigners)
        {
            if (id == slot->module->GetId())
            {
                slot->enabled.store(on);
                found = true;
            }
        }
        return found;
    }

    void CommanderMgr::SetPlayerRole(ObjectGuid player, uint8 role)
    {
        std::lock_guard<std::mutex> guard(m_playerRolesLock);
        if (role == ROLE_INVALID)
            m_playerRoles.erase(player.GetRawValue());
        else
            m_playerRoles[player.GetRawValue()] = role;
    }

    uint8 CommanderMgr::GetPlayerRole(ObjectGuid player) const
    {
        std::lock_guard<std::mutex> guard(m_playerRolesLock);
        auto itr = m_playerRoles.find(player.GetRawValue());
        return itr != m_playerRoles.end() ? itr->second : uint8(ROLE_INVALID);
    }

    void CommanderMgr::SetThreatWatch(ObjectGuid player, ThreatWatch mode)
    {
        std::lock_guard<std::mutex> guard(m_threatWatchLock);
        if (mode == ThreatWatch::Off)
            m_threatWatch.erase(player.GetRawValue());
        else
            m_threatWatch[player.GetRawValue()] = mode;
    }

    ThreatWatch CommanderMgr::GetThreatWatch(ObjectGuid player) const
    {
        std::lock_guard<std::mutex> guard(m_threatWatchLock);
        auto itr = m_threatWatch.find(player.GetRawValue());
        return itr != m_threatWatch.end() ? itr->second : ThreatWatch::Off;
    }

    void CommanderMgr::SetPullMethod(ObjectGuid player, PullMethod method)
    {
        std::lock_guard<std::mutex> guard(m_pullMethodLock);
        if (method == PullMethod::Auto)
            m_pullMethods.erase(player.GetRawValue());
        else
            m_pullMethods[player.GetRawValue()] = method;
    }

    PullMethod CommanderMgr::GetPullMethod(ObjectGuid player) const
    {
        std::lock_guard<std::mutex> guard(m_pullMethodLock);
        auto itr = m_pullMethods.find(player.GetRawValue());
        return itr != m_pullMethods.end() ? itr->second : PullMethod::Auto;
    }

    void CommanderMgr::SetPullPlace(ObjectGuid player, PullPlace place)
    {
        std::lock_guard<std::mutex> guard(m_pullMethodLock);
        if (place == PullPlace::Auto)
            m_pullPlaces.erase(player.GetRawValue());
        else
            m_pullPlaces[player.GetRawValue()] = place;
    }

    PullPlace CommanderMgr::GetPullPlace(ObjectGuid player) const
    {
        std::lock_guard<std::mutex> guard(m_pullMethodLock);
        auto itr = m_pullPlaces.find(player.GetRawValue());
        return itr != m_pullPlaces.end() ? itr->second : PullPlace::Auto;
    }

    void CommanderMgr::SetPullStance(ObjectGuid player, PullStance stance)
    {
        {
            std::lock_guard<std::mutex> guard(m_pullMethodLock);
            if (stance == PullStance::Active)
                m_pullStances.erase(player.GetRawValue());
            else
                m_pullStances[player.GetRawValue()] = stance;
        }
        if (stance == PullStance::Active)
            CharacterDatabase.PExecute("DELETE FROM `custom_character_pull_stance` WHERE `guid` = %u", player.GetCounter());
        else
            CharacterDatabase.PExecute("REPLACE INTO `custom_character_pull_stance` (`guid`, `stance`) VALUES (%u, '%s')",
                player.GetCounter(), PullStanceName(stance));
    }

    void CommanderMgr::LoadPullStances()
    {
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `custom_character_pull_stance` ("
            " `guid` INT UNSIGNED NOT NULL COMMENT 'characters.guid',"
            " `stance` VARCHAR(8) NOT NULL COMMENT 'passive (active is not stored)',"
            " PRIMARY KEY (`guid`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='PartyBot: whether the party starts fights on its own (the talk window)'");

        std::map<uint64, PullStance> stances;
        std::unique_ptr<QueryResult> result(CharacterDatabase.Query("SELECT `guid`, `stance` FROM `custom_character_pull_stance`"));
        if (result)
        {
            do
            {
                Field* f = result->Fetch();
                PullStance stance;
                std::string const name = f[1].GetCppString();
                if (ParsePullStance(name.c_str(), stance) && stance != PullStance::Active)
                    stances[ObjectGuid(HIGHGUID_PLAYER, f[0].GetUInt32()).GetRawValue()] = stance;
            } while (result->NextRow());
        }

        std::lock_guard<std::mutex> guard(m_pullMethodLock);
        m_pullStances = std::move(stances);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> [PartyBot] %u pull stances loaded.", uint32(m_pullStances.size()));
    }

    PullStance CommanderMgr::GetPullStance(ObjectGuid player) const
    {
        std::lock_guard<std::mutex> guard(m_pullMethodLock);
        auto itr = m_pullStances.find(player.GetRawValue());
        return itr != m_pullStances.end() ? itr->second : PullStance::Active;
    }

    void CommanderMgr::QueueVoice(Player const* member, VoiceEvent event)
    {
        if (!IsEnabled() || !member)
            return;
        if (std::shared_ptr<PartyCommander> commander = FindCommander(member))
            commander->GetVoice().Queue(std::move(event), WorldTimer::getMSTime());
    }

    void CommanderMgr::NoteInterrupt(Player const* member, ObjectGuid caster, ObjectGuid target, uint32 spellId)
    {
        if (!IsEnabled() || !member)
            return;
        if (std::shared_ptr<PartyCommander> commander = FindCommander(member))
            commander->GetVoice().NoteInterrupt(caster, target, spellId, WorldTimer::getMSTime());
    }

    void CommanderMgr::NoteDamage(Player const* reporter, ObjectGuid dealer, uint32 damage, bool area)
    {
        if (!IsEnabled() || !reporter)
            return;
        if (std::shared_ptr<PartyCommander> commander = FindCommander(reporter))
            commander->GetDamageMeter().Note(dealer, damage, area, WorldTimer::getMSTime());
    }

    void CommanderMgr::SetVoiceSettings(ObjectGuid player, VoiceSettings const& settings)
    {
        std::lock_guard<std::mutex> guard(m_voiceLock);
        if (settings.IsDefault())
            m_voiceSettings.erase(player.GetRawValue());
        else
            m_voiceSettings[player.GetRawValue()] = settings;
    }

    VoiceSettings CommanderMgr::GetVoiceSettings(ObjectGuid player) const
    {
        std::lock_guard<std::mutex> guard(m_voiceLock);
        auto itr = m_voiceSettings.find(player.GetRawValue());
        return itr != m_voiceSettings.end() ? itr->second : VoiceSettings();
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
