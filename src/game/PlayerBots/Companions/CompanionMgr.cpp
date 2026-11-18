#include "Companions/CompanionMgr.h"
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
#include <ctime>

namespace Companions
{
    namespace
    {
        // The temporary bot's logout save goes first; give the database a moment before the login reads it.
        uint32 const LOGIN_DELAY_MS = 2000;
        // The temporary bot did not leave (or the login never happened): give up, the character stays saved.
        uint32 const LOGIN_TIMEOUT_MS = 60000;
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
            " PRIMARY KEY (`char_guid`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='companions: recruited roster members (real characters)'");

        std::unique_ptr<QueryResult> exists(CharacterDatabase.Query("SHOW TABLES LIKE 'custom_companion'"));
        if (!exists)
            return false;

        // Tables from stage S1 (before reward_mode).
        std::unique_ptr<QueryResult> rewardMode(CharacterDatabase.Query("SHOW COLUMNS FROM `custom_companion` LIKE 'reward_mode'"));
        if (!rewardMode)
            CharacterDatabase.DirectExecute("ALTER TABLE `custom_companion` ADD COLUMN `reward_mode` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `recruited_at`");
        return true;
    }

    void CompanionMgr::Load()
    {
        m_companions.clear();
        m_pending.clear();
        m_available = EnsureTable();
        if (!m_available)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Companion] Could not create table `custom_companion`, recruiting is off.");
            return;
        }

        // Rows whose character was deleted are left alone: the guid is never in the world again.
        std::unique_ptr<QueryResult> result(CharacterDatabase.Query(
            "SELECT c.`char_guid`, c.`member_id`, c.`recruiter_guid`, c.`recruited_at`, c.`reward_mode` FROM `custom_companion` c"
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
                m_companions[info.charGuid] = info;
            } while (result->NextRow());
        }

        // Personality: the roster member's tags.
        std::unique_ptr<QueryResult> tags(CharacterDatabase.Query(
            "SELECT c.`char_guid`, t.`tag` FROM `custom_companion` c"
            " JOIN `custom_popbot_member_tag` t ON t.`member_id` = c.`member_id` WHERE c.`member_id` <> 0"));
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
        if (!memberId)
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

    RewardMode CompanionMgr::GetRewardMode(uint32 charGuid) const
    {
        CompanionInfo const* info = Find(charGuid);
        return info ? info->rewardMode : RewardMode::Auto;
    }

    void CompanionMgr::SetRewardMode(uint32 charGuid, RewardMode mode)
    {
        auto itr = m_companions.find(charGuid);
        if (itr == m_companions.end())
            return;
        itr->second.rewardMode = mode;
        CharacterDatabase.PExecute("UPDATE `custom_companion` SET `reward_mode` = %u WHERE `char_guid` = %u", uint32(mode), charGuid);
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

        // What it learned before it met the player: the class trainer spells of its level, free.
        // From now on new ranks cost money at a trainer.
        BotShared::ClassTraining::Result const lessons = BotShared::ClassTraining::LearnAvailable(bot, false);

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
        info.tags = LoadTags(memberId);
        CharacterDatabase.PExecute("REPLACE INTO `custom_companion` (`char_guid`, `member_id`, `recruiter_guid`, `recruited_at`)"
            " VALUES (%u, %u, %u, %u)", info.charGuid, info.memberId, info.recruiterGuid, info.recruitedAt);
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

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] '%s' (%u, level %u) recruited by '%s', %u trainer spells learned.",
            bot->GetName(), guid, bot->GetLevel(), recruiter->GetName(), lessons.learned);
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

    void CompanionMgr::Update(uint32 diff)
    {
        m_nowMs += diff;
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
