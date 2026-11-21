#include "Companions/CompanionMgr.h"
#include "Companions/ClassQuests/PastQuests.h"
#include "Companions/PastProfessions.h"
#include "Companions/PastRoll.h"
#include "Shared/ClassTraining.h"
#include "Life/Requests/Phrases.h"
#include "Shared/TownGates.h"
#include "PlayerBotMgr.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "AccountMgr.h"
#include "World.h"
#include "Config/Config.h"
#include "Database/DatabaseEnv.h"
#include "Utilities/Random.h"
#include "Log.h"
#include <algorithm>
#include <ctime>

namespace Companions
{
    namespace
    {
        // The temporary bot's logout save goes first; give the database a moment before the login reads it.
        uint32 const LOGIN_DELAY_MS = 2000;
        // The temporary bot did not leave (or the login never happened): give up, the character stays saved.
        uint32 const LOGIN_TIMEOUT_MS = 60000;

        // A recruit's class trainer spells stop this many levels below its own (user 2026-11-21:
        // the player takes it training, it does not come knowing everything of its level).
        uint32 const UNTRAINED_LEVELS_MIN = 4;
        uint32 const UNTRAINED_LEVELS_MAX = 8;
        uint32 const KEY_UNTRAINED = 0x80000002;   // PastRoll key (PastRoll.h)

        uint32 TrainedUpToLevel(Player const* bot)
        {
            uint32 const span = UNTRAINED_LEVELS_MAX - UNTRAINED_LEVELS_MIN + 1;
            uint32 const behind = UNTRAINED_LEVELS_MIN +
                std::min(span - 1, uint32(PastRoll(bot->GetGUIDLow(), KEY_UNTRAINED) * float(span)));
            return bot->GetLevel() > behind ? bot->GetLevel() - behind : 1;
        }
    }

    CompanionMgr& CompanionMgr::Instance()
    {
        static CompanionMgr instance;
        return instance;
    }

    void CompanionMgr::LoadConfig()
    {
        m_enabled.store(sConfig.GetBoolDefault("Companion.Enable", true));
        std::string account = sConfig.GetStringDefault("Companion.Account", "COMPANIONS");
        if (account.empty())
            account = "COMPANIONS";
        if (account != m_accountName)
        {
            m_accountName = account;
            m_accountId = 0;
            m_accountRequested = false;
        }
    }

    bool CompanionMgr::EnsureTable()
    {
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `custom_companion` ("
            " `char_guid` INT UNSIGNED NOT NULL,"
            " `member_id` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'custom_popbot_member.id, 0 = not from the roster',"
            " `recruiter_guid` INT UNSIGNED NOT NULL DEFAULT 0,"
            " `recruited_at` INT UNSIGNED NOT NULL DEFAULT 0,"
            " `reward_mode` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = picks quest rewards itself, 1 = the player picks',"
            " `past_quests_decided` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1 = class quests done before recruiting are decided (ClassQuests/PastQuests)',"
            " `affinity` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0..100, what the player did for it (class quests)',"
            " `professions_decided` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1 = professions before recruiting are given (PastProfessions)',"
            " PRIMARY KEY (`char_guid`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='companions: recruited roster members (real characters)'");

        // Class quest asks (ClassQuests/ClassQuestFlow), one row per companion and chain.
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `custom_soulbot_request` ("
            " `char_guid` INT UNSIGNED NOT NULL,"
            " `chain_id` INT UNSIGNED NOT NULL COMMENT 'last quest of the class quest chain (Shared/ClassQuestChains)',"
            " `state` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 waiting, 1 active, 2 exhausted, 3 done, 4 dropped',"
            " `asks_left` TINYINT UNSIGNED NOT NULL DEFAULT 3,"
            " `declined` TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            " `helped` TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            " PRIMARY KEY (`char_guid`, `chain_id`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='companions: class quest asks and their outcome'");

        std::unique_ptr<QueryResult> exists(CharacterDatabase.Query("SHOW TABLES LIKE 'custom_companion'"));
        if (!exists)
            return false;

        // Tables from stage S1 (before reward_mode).
        std::unique_ptr<QueryResult> rewardMode(CharacterDatabase.Query("SHOW COLUMNS FROM `custom_companion` LIKE 'reward_mode'"));
        if (!rewardMode)
            CharacterDatabase.DirectExecute("ALTER TABLE `custom_companion` ADD COLUMN `reward_mode` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `recruited_at`");
        // Tables from before class quest abilities (C0): their companions are decided when next online.
        std::unique_ptr<QueryResult> pastQuests(CharacterDatabase.Query("SHOW COLUMNS FROM `custom_companion` LIKE 'past_quests_decided'"));
        if (!pastQuests)
            CharacterDatabase.DirectExecute("ALTER TABLE `custom_companion` ADD COLUMN `past_quests_decided` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `reward_mode`");
        std::unique_ptr<QueryResult> affinity(CharacterDatabase.Query("SHOW COLUMNS FROM `custom_companion` LIKE 'affinity'"));
        if (!affinity)
            CharacterDatabase.DirectExecute("ALTER TABLE `custom_companion` ADD COLUMN `affinity` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `past_quests_decided`");
        // Tables from before professions: their companions get them when next online.
        std::unique_ptr<QueryResult> professions(CharacterDatabase.Query("SHOW COLUMNS FROM `custom_companion` LIKE 'professions_decided'"));
        if (!professions)
            CharacterDatabase.DirectExecute("ALTER TABLE `custom_companion` ADD COLUMN `professions_decided` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `affinity`");
        return true;
    }

    bool CompanionMgr::HasTagTable()
    {
        std::unique_ptr<QueryResult> exists(CharacterDatabase.Query("SHOW TABLES LIKE 'custom_popbot_member_tag'"));
        return exists != nullptr;
    }

    void CompanionMgr::Load()
    {
        m_companions.clear();
        m_pending.clear();
        m_pastQuestsPending.clear();
        m_professionsPending.clear();
        m_available = EnsureTable();
        if (!m_available)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Companion] Could not create table `custom_companion`, recruiting is off.");
            return;
        }

        // Rows whose character was deleted are left alone: the guid is never in the world again.
        std::unique_ptr<QueryResult> result(CharacterDatabase.Query(
            "SELECT c.`char_guid`, c.`member_id`, c.`recruiter_guid`, c.`recruited_at`, c.`reward_mode`, c.`past_quests_decided`, c.`affinity`, c.`professions_decided` FROM `custom_companion` c"
            " JOIN `characters` ch ON ch.`guid` = c.`char_guid`"));
        if (result)
        {
            do
            {
                Field* f = result->Fetch();
                CompanionInfo info;
                info.charGuid = f[0].GetUInt32();
                info.memberId = f[1].GetUInt32();
                info.recruiterGuid = f[2].GetUInt32();
                info.recruitedAt = f[3].GetUInt32();
                info.rewardMode = f[4].GetUInt8() == uint8(RewardMode::AskPlayer) ? RewardMode::AskPlayer : RewardMode::Auto;
                if (!f[5].GetUInt8())
                    m_pastQuestsPending.push_back(info.charGuid);
                info.affinity = f[6].GetUInt32();
                if (!f[7].GetUInt8())
                    m_professionsPending.push_back(info.charGuid);
                m_companions[info.charGuid] = info;
            } while (result->NextRow());
        }

        // Personality: the roster member's tags. The roster creates its tables on first use, which can
        // come after this load: no table yet means no tags yet.
        std::unique_ptr<QueryResult> tags;
        if (HasTagTable())
            tags = CharacterDatabase.Query(
                "SELECT c.`char_guid`, t.`tag` FROM `custom_companion` c"
                " JOIN `custom_popbot_member_tag` t ON t.`member_id` = c.`member_id` WHERE c.`member_id` <> 0");
        if (tags)
        {
            do
            {
                Field* f = tags->Fetch();
                auto itr = m_companions.find(f[0].GetUInt32());
                if (itr != m_companions.end())
                    itr->second.tags.push_back(f[1].GetCppString());
            } while (tags->NextRow());
        }

        Life::Phrases::Load();
        BotShared::TownGates::Load();

        ResolveAccount();
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> [Companion] %u companions loaded, account '%s' (%u).",
            uint32(m_companions.size()), m_accountName.c_str(), m_accountId);
    }

    uint32 CompanionMgr::ResolveAccount()
    {
        if (m_accountId)
            return m_accountId;

        m_accountId = sAccountMgr.GetId(m_accountName);
        if (m_accountId || m_accountRequested)
            return m_accountId;

        // Nobody logs in with it: a random password.
        std::string password;
        for (uint32 i = 0; i < 16; ++i)
            password += char('a' + urand(0, 25));
        AccountOpResult const res = sAccountMgr.CreateAccount(m_accountName, password);
        m_accountRequested = true;
        if (res != AOR_OK)
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Companion] Could not create account '%s' (error %u).", m_accountName.c_str(), uint32(res));
        else
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] Created account '%s' for companion characters.", m_accountName.c_str());
        // The insert is asynchronous: the id is read again on the next call.
        return 0;
    }

    std::vector<std::string> CompanionMgr::LoadTags(uint32 memberId)
    {
        std::vector<std::string> tags;
        if (!memberId || !HasTagTable())
            return tags;
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
            "SELECT `tag` FROM `custom_popbot_member_tag` WHERE `member_id` = %u", memberId));
        if (result)
        {
            do
            {
                tags.push_back(result->Fetch()[0].GetCppString());
            } while (result->NextRow());
        }
        return tags;
    }

    CompanionInfo const* CompanionMgr::Find(uint32 charGuid) const
    {
        auto itr = m_companions.find(charGuid);
        return itr != m_companions.end() ? &itr->second : nullptr;
    }

    std::vector<CompanionInfo> CompanionMgr::OfRecruiter(uint32 recruiterGuid) const
    {
        std::lock_guard<std::mutex> guard(m_affinityLock);   // affinity changes on map threads
        std::vector<CompanionInfo> list;
        for (auto const& itr : m_companions)
            if (itr.second.recruiterGuid == recruiterGuid)
                list.push_back(itr.second);
        return list;
    }

    RewardMode CompanionMgr::GetRewardMode(uint32 charGuid) const
    {
        CompanionInfo const* info = Find(charGuid);
        return info ? info->rewardMode : RewardMode::Auto;
    }

    uint32 CompanionMgr::GetAffinity(uint32 charGuid) const
    {
        std::lock_guard<std::mutex> guard(m_affinityLock);
        CompanionInfo const* info = Find(charGuid);
        return info ? info->affinity : 0;
    }

    uint32 CompanionMgr::AddAffinity(uint32 charGuid, uint32 points, char const* reason)
    {
        std::lock_guard<std::mutex> guard(m_affinityLock);
        auto itr = m_companions.find(charGuid);
        if (itr == m_companions.end())
            return 0;
        CompanionInfo& info = itr->second;
        info.affinity = std::min<uint32>(MAX_AFFINITY, info.affinity + points);
        if (!info.temporary)
            CharacterDatabase.PExecute("UPDATE `custom_companion` SET `affinity` = %u WHERE `char_guid` = %u", info.affinity, charGuid);
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Companion] %u: affinity +%u (%s) = %u.", charGuid, points, reason ? reason : "", info.affinity);
        return info.affinity;
    }

    void CompanionMgr::SetRewardMode(uint32 charGuid, RewardMode mode)
    {
        auto itr = m_companions.find(charGuid);
        if (itr == m_companions.end())
            return;
        itr->second.rewardMode = mode;
        if (itr->second.temporary)
            return;
        CharacterDatabase.PExecute("UPDATE `custom_companion` SET `reward_mode` = %u WHERE `char_guid` = %u", uint32(mode), charGuid);
    }

    bool CompanionMgr::MarkTemporary(Player* bot, Player* recruiter, std::vector<std::string> tags)
    {
        // (the recruiter may be the headless dev console character, a bot session itself)
        if (!bot || !recruiter || !bot->IsBot() || !bot->IsSavingDisabled() || bot == recruiter)
            return false;
        uint32 const guid = bot->GetGUIDLow();
        if (IsCompanion(guid))
            return false;
        CompanionInfo info;
        info.charGuid = guid;
        info.recruiterGuid = recruiter->GetGUIDLow();
        info.recruitedAt = uint32(time(nullptr));
        info.tags = std::move(tags);
        info.temporary = true;
        m_companions[guid] = std::move(info);
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Companion] %s acts as a temporary companion of %s (test scenario).",
            bot->GetName(), recruiter->GetName());
        return true;
    }

    bool CompanionMgr::ForgetTemporary(uint32 charGuid)
    {
        auto itr = m_companions.find(charGuid);
        if (itr == m_companions.end() || !itr->second.temporary)
            return false;
        m_companions.erase(itr);
        return true;
    }

    bool CompanionMgr::Recruit(Player* bot, Player* recruiter, uint32 memberId)
    {
        if (!IsEnabled() || !m_available || !bot || !recruiter || !bot->IsInWorld() || !recruiter->IsInWorld())
            return false;
        if (!bot->IsBot() || !bot->IsSavingDisabled() || recruiter->IsBot())
            return false;

        uint32 const guid = bot->GetGUIDLow();
        if (IsCompanion(guid))
            return false;

        uint32 const account = ResolveAccount();
        if (!account)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Companion] Account '%s' is not ready, '%s' not recruited.", m_accountName.c_str(), bot->GetName());
            return false;
        }

        // The name must still be this guid's: a real player may have created a character with it,
        // and a temporary bot never had a row of its own.
        std::string name = bot->GetName();
        ObjectGuid const holder = sObjectMgr.GetPlayerGuidByName(name);
        if (!holder.IsEmpty() && holder.GetCounter() != guid)
            return false;
        CharacterDatabase.escape_string(name);
        std::unique_ptr<QueryResult> taken(CharacterDatabase.PQuery(
            "SELECT 1 FROM `characters` WHERE `name` = '%s' OR `guid` = %u", name.c_str(), guid));
        if (taken)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Companion] Name or guid of '%s' (%u) is already in `characters`, not recruited.", bot->GetName(), guid);
            return false;
        }

        // What it did before it met the player: its class quests (rolled by level, the abilities of
        // those it did not do are taken away), then the class trainer spells of its level, free
        // (after the quests: a rank that needs a quest spell is learned only if it has it), but
        // not those of its last few levels; then its professions. From now on new ranks cost
        // money at a trainer.
        std::vector<std::string> const tags = LoadTags(memberId);
        PastQuests::Result const past = PastQuests::Decide(bot);
        uint32 const trainedUpTo = TrainedUpToLevel(bot);
        BotShared::ClassTraining::ForgetAbove(bot, trainedUpTo);    // a field bot knew all of its level
        BotShared::ClassTraining::Result const lessons = BotShared::ClassTraining::LearnAvailable(bot, false, trainedUpTo);
        PastProfessions::Result const professions = PastProfessions::Decide(bot, tags);

        bot->EnableSaving(account);
        bot->SaveToDB();

        // Home is where it was recruited (a city), as if it had bound its hearthstone there.
        if (!bot->GetMap()->Instanceable())
            CharacterDatabase.PExecute("REPLACE INTO `character_homebind` (`guid`, `map`, `zone`, `position_x`, `position_y`, `position_z`)"
                " VALUES (%u, %u, %u, %f, %f, %f)", guid, bot->GetMapId(), bot->GetZoneId(),
                bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());

        CompanionInfo info;
        info.charGuid = guid;
        info.memberId = memberId;
        info.recruiterGuid = recruiter->GetGUIDLow();
        info.recruitedAt = uint32(time(nullptr));
        info.tags = tags;
        CharacterDatabase.PExecute("REPLACE INTO `custom_companion` (`char_guid`, `member_id`, `recruiter_guid`, `recruited_at`, `past_quests_decided`, `professions_decided`)"
            " VALUES (%u, %u, %u, %u, 1, 1)", info.charGuid, info.memberId, info.recruiterGuid, info.recruitedAt);
        m_companions[guid] = info;

        // The name cache holds the temporary session's account; after a restart it is read from `characters`.
        if (PlayerCacheData* data = sObjectMgr.GetPlayerDataByGUID(guid))
            data->uiAccount = account;
        sWorld.UpdateRealmCharCount(account);

        PendingLogin login;
        login.charGuid = guid;
        login.recruiter = recruiter->GetObjectGuid();
        login.sinceMs = m_nowMs;
        m_pending.push_back(login);

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] '%s' (%u, level %u) recruited by '%s', %u trainer spells learned (up to level %u),"
            " class quests: %u done, %u not done, %u abilities taken away; professions %u (%u) + %u (%u), %u recipes.",
            bot->GetName(), guid, bot->GetLevel(), recruiter->GetName(), lessons.learned, trainedUpTo, past.done, past.notDone, past.revoked,
            professions.first, professions.firstValue, professions.second, professions.secondValue, professions.recipes);
        return true;
    }

    void CompanionMgr::LoginAsPartyBot(uint32 charGuid, Player* leader)
    {
        float x, y, z;
        leader->GetNearPoint(leader, x, y, z, 0, 3.0f, frand(0.0f, 6.0f));
        PartyBotAI* ai = new PartyBotAI(leader, leader->GetMapId(), leader->GetMap()->GetInstanceId(), x, y, z, leader->GetOrientation());
        if (!sPlayerBotMgr.AddBot(charGuid, false, ai, true))
        {
            delete ai;
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Companion] Could not log in companion %u.", charGuid);
        }
    }

    void CompanionMgr::DecidePastQuestsOfOnline()
    {
        for (auto itr = m_pastQuestsPending.begin(); itr != m_pastQuestsPending.end();)
        {
            Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, *itr));
            PartyBotAI* ai = bot && bot->IsInWorld() ? dynamic_cast<PartyBotAI*>(bot->AI()) : nullptr;
            if (!ai)
            {
                ++itr;
                continue;
            }

            PastQuests::Result const past = PastQuests::Decide(bot);
            ai->m_resetSpellData = true;   // spells taken away or learned: the rotation sees them
            CharacterDatabase.PExecute("UPDATE `custom_companion` SET `past_quests_decided` = 1 WHERE `char_guid` = %u", *itr);
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] '%s' (level %u): class quests before recruiting decided,"
                " %u done, %u not done, %u abilities taken away.", bot->GetName(), bot->GetLevel(), past.done, past.notDone, past.revoked);
            itr = m_pastQuestsPending.erase(itr);
        }
    }

    void CompanionMgr::DecideProfessionsOfOnline()
    {
        for (auto itr = m_professionsPending.begin(); itr != m_professionsPending.end();)
        {
            Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, *itr));
            PartyBotAI* ai = bot && bot->IsInWorld() ? dynamic_cast<PartyBotAI*>(bot->AI()) : nullptr;
            CompanionInfo const* info = Find(*itr);
            if (!ai || !info)
            {
                ++itr;
                continue;
            }

            PastProfessions::Result const professions = PastProfessions::Decide(bot, info->tags);
            ai->m_resetSpellData = true;
            CharacterDatabase.PExecute("UPDATE `custom_companion` SET `professions_decided` = 1 WHERE `char_guid` = %u", *itr);
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] '%s' (level %u): professions before recruiting given,"
                " %u (%u) + %u (%u), %u recipes.", bot->GetName(), bot->GetLevel(),
                professions.first, professions.firstValue, professions.second, professions.secondValue, professions.recipes);
            itr = m_professionsPending.erase(itr);
        }
    }

    void CompanionMgr::Update(uint32 diff)
    {
        m_nowMs += diff;
        if (!m_pastQuestsPending.empty())
            DecidePastQuestsOfOnline();
        if (!m_professionsPending.empty())
            DecideProfessionsOfOnline();
        if (m_pending.empty())
            return;

        for (auto itr = m_pending.begin(); itr != m_pending.end();)
        {
            uint32 const waited = m_nowMs - itr->sinceMs;
            ObjectGuid const charGuid(HIGHGUID_PLAYER, itr->charGuid);
            bool const stillHere = sPlayerBotMgr.HasBot(itr->charGuid) || sObjectAccessor.FindPlayerNotInWorld(charGuid);
            if (stillHere || waited < LOGIN_DELAY_MS)
            {
                if (waited >= LOGIN_TIMEOUT_MS)
                {
                    sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Companion] Companion %u did not log out in time, login skipped.", itr->charGuid);
                    itr = m_pending.erase(itr);
                }
                else
                    ++itr;
                continue;
            }

            // The recruiter left meanwhile: the companion stays saved, .partybot load brings it back.
            if (Player* leader = sObjectAccessor.FindPlayer(itr->recruiter))
                LoginAsPartyBot(itr->charGuid, leader);
            itr = m_pending.erase(itr);
        }
    }
}
