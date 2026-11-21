#include "Companions/CompanionMgr.h"
#include "Companions/Affinity/AffinitySources.h"
#include "Companions/Bench/BenchLeveling.h"
#include "Companions/Bench/Expedition.h"
#include "Companions/ClassQuests/PastQuests.h"
#include "Companions/PastProfessions.h"
#include "Companions/PastRoll.h"
#include "Companions/OfflineLife.h"
#include "Shared/BotChat.h"
#include "Shared/ClassTraining.h"
#include "Shared/DungeonClears.h"
#include "Life/Requests/Phrases.h"
#include "Life/Errands/Travel.h"
#include "Shared/TownGates.h"
#include "PlayerBotMgr.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "Group.h"
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
#include <map>

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

        // Presence (UpdatePresence): how often it looks, how long a companion is online before
        // its time away is counted (its AI refreshes what it knows of itself every 5 s), how
        // often last_seen is written.
        uint32 const PRESENCE_CHECK_MS = 5 * IN_MILLISECONDS;
        uint32 const ARRIVE_SETTLE_MS = 15 * IN_MILLISECONDS;
        uint32 const LAST_SEEN_MS = 60 * IN_MILLISECONDS;

        // A player asked to enter the world: his group's companions are logged in for this long
        // (one still logging out from his last session is tried again until then).
        uint32 const RETURN_WINDOW_MS = 60 * IN_MILLISECONDS;

        // Out of the party this long, it goes home (user 2026-11-21: 4 hours); looked at this often.
        uint32 const HOME_AFTER_SEC = 4 * HOUR;
        uint32 const HOME_CHECK_MS = 5 * MINUTE * IN_MILLISECONDS;

        // Bench leveling (UpdateBench): how often it looks at the owners; a companion that left
        // the world less than this ago is not written yet (its logout save may still be queued).
        uint32 const BENCH_CHECK_MS = MINUTE * IN_MILLISECONDS;
        uint32 const BENCH_SETTLE_SEC = 2 * MINUTE;

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
        m_skillUpPriceFactor = std::max(0.0f, sConfig.GetFloatDefault("Companion.SkillUp.PriceFactor", 1.0f));
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
            " `last_seen` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'unix time last seen online (OfflineLife), 0 = never'," 
            " `money_gifts` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'times it got affinity for money the player gave (Affinity/AffinitySources)',"
            " `staying` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1 = logged out with its leader while told to wait there',"
            " PRIMARY KEY (`char_guid`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='companions: recruited roster members (real characters)'");

        // Skill points of the day grown offline (OfflineLife), one row per companion and skill.
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `custom_companion_skillup` ("
            " `char_guid` INT UNSIGNED NOT NULL,"
            " `skill` SMALLINT UNSIGNED NOT NULL,"
            " `day` INT UNSIGNED NOT NULL COMMENT 'local day, year * 1000 + day of the year',"
            " `points` SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
            " PRIMARY KEY (`char_guid`, `skill`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='companions: profession points grown offline today'");

        // Max-level dungeon and raid clears with the player (Affinity/AffinitySources).
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `custom_companion_clear` ("
            " `char_guid` INT UNSIGNED NOT NULL,"
            " `segment` TINYINT UNSIGNED NOT NULL COMMENT 'Shared/DungeonClears::Segment',"
            " `clears` INT UNSIGNED NOT NULL DEFAULT 0,"
            " PRIMARY KEY (`char_guid`, `segment`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='companions: dungeon and raid clears with the player'");

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

        // Players who own companions: what bench leveling measured of them last (UpdateBench).
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `custom_companion_owner` ("
            " `owner_guid` INT UNSIGNED NOT NULL,"
            " `xp_seen` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'his total experience at the last look (bench leveling)',"
            " PRIMARY KEY (`owner_guid`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='companions: their owners, as the companions keep track of them'");

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
        std::unique_ptr<QueryResult> lastSeen(CharacterDatabase.Query("SHOW COLUMNS FROM `custom_companion` LIKE 'last_seen'"));
        if (!lastSeen)
            CharacterDatabase.DirectExecute("ALTER TABLE `custom_companion` ADD COLUMN `last_seen` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `professions_decided`");
        std::unique_ptr<QueryResult> moneyGifts(CharacterDatabase.Query("SHOW COLUMNS FROM `custom_companion` LIKE 'money_gifts'"));
        if (!moneyGifts)
            CharacterDatabase.DirectExecute("ALTER TABLE `custom_companion` ADD COLUMN `money_gifts` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `last_seen`");
        std::unique_ptr<QueryResult> staying(CharacterDatabase.Query("SHOW COLUMNS FROM `custom_companion` LIKE 'staying'"));
        if (!staying)
            CharacterDatabase.DirectExecute("ALTER TABLE `custom_companion` ADD COLUMN `staying` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `money_gifts`");
        std::unique_ptr<QueryResult> benchLevels(CharacterDatabase.Query("SHOW COLUMNS FROM `custom_companion` LIKE 'bench_levels'"));
        if (!benchLevels)
            CharacterDatabase.DirectExecute("ALTER TABLE `custom_companion` ADD COLUMN `bench_levels` TINYINT UNSIGNED NOT NULL DEFAULT 0"
                " COMMENT 'levels grown on the bench since last online, said when it arrives' AFTER `staying`");
        std::unique_ptr<QueryResult> expedition(CharacterDatabase.Query("SHOW COLUMNS FROM `custom_companion` LIKE 'expedition_secs'"));
        if (!expedition)
            CharacterDatabase.DirectExecute("ALTER TABLE `custom_companion` ADD COLUMN `expedition_secs` INT UNSIGNED NOT NULL DEFAULT 0"
                " COMMENT 'owner active time out on the bench since its last parcel' AFTER `bench_levels`");
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
        m_returns.clear();
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
            "SELECT c.`char_guid`, c.`member_id`, c.`recruiter_guid`, c.`recruited_at`, c.`reward_mode`, c.`past_quests_decided`, c.`affinity`, c.`professions_decided`, c.`last_seen`, c.`money_gifts`, c.`staying`, c.`bench_levels`, c.`expedition_secs` FROM `custom_companion` c"
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
                info.lastSeen = f[8].GetUInt32();
                info.moneyGifts = f[9].GetUInt32();
                info.staying = f[10].GetUInt8() != 0;
                info.benchLevels = f[11].GetUInt32();
                info.expeditionSecs = f[12].GetUInt32();
                m_companions[info.charGuid] = info;
            } while (result->NextRow());
        }

        m_ownerXpSeen.clear();
        m_benched.clear();
        Expedition::Load();
        std::unique_ptr<QueryResult> owners(CharacterDatabase.Query("SELECT `owner_guid`, `xp_seen` FROM `custom_companion_owner`"));
        if (owners)
        {
            do
            {
                Field* f = owners->Fetch();
                m_ownerXpSeen[f[0].GetUInt32()] = f[1].GetUInt32();
            } while (owners->NextRow());
        }

        std::unique_ptr<QueryResult> clears(CharacterDatabase.Query("SELECT `char_guid`, `segment`, `clears` FROM `custom_companion_clear`"));
        if (clears)
        {
            do
            {
                Field* f = clears->Fetch();
                auto itr = m_companions.find(f[0].GetUInt32());
                if (itr != m_companions.end())
                    itr->second.clears[f[1].GetUInt8()] = f[2].GetUInt32();
            } while (clears->NextRow());
        }
        BotShared::DungeonClears::SetListener(&AffinitySources::OnClear);

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

    bool CompanionMgr::CountMoneyGift(uint32 charGuid, uint32 max)
    {
        std::lock_guard<std::mutex> guard(m_affinityLock);
        auto itr = m_companions.find(charGuid);
        if (itr == m_companions.end() || itr->second.moneyGifts >= max)
            return false;
        CompanionInfo& info = itr->second;
        ++info.moneyGifts;
        if (!info.temporary)
            CharacterDatabase.PExecute("UPDATE `custom_companion` SET `money_gifts` = %u WHERE `char_guid` = %u", info.moneyGifts, charGuid);
        return true;
    }

    uint32 CompanionMgr::CountClear(uint32 charGuid, uint8 segment)
    {
        std::lock_guard<std::mutex> guard(m_affinityLock);
        auto itr = m_companions.find(charGuid);
        if (itr == m_companions.end())
            return 0;
        CompanionInfo& info = itr->second;
        uint32 const times = ++info.clears[segment];
        if (!info.temporary)
            CharacterDatabase.PExecute("REPLACE INTO `custom_companion_clear` (`char_guid`, `segment`, `clears`) VALUES (%u, %u, %u)",
                charGuid, uint32(segment), times);
        return times;
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
        m_present.erase(charGuid);
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
        info.lastSeen = info.recruitedAt;
        info.affinity = AffinitySources::StartAt(bot->GetLevel());
        CharacterDatabase.PExecute("REPLACE INTO `custom_companion` (`char_guid`, `member_id`, `recruiter_guid`, `recruited_at`, `past_quests_decided`, `affinity`, `professions_decided`, `last_seen`)"
            " VALUES (%u, %u, %u, %u, 1, %u, 1, %u)", info.charGuid, info.memberId, info.recruiterGuid, info.recruitedAt, info.affinity, info.lastSeen);
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

    void CompanionMgr::OnLoginRequest(uint32 charGuid)
    {
        if (!IsEnabled() || !m_available || !charGuid)
            return;
        ObjectGuid const leader(HIGHGUID_PLAYER, charGuid);
        for (Return const& r : m_returns)
            if (r.leader == leader)
                return;
        Return r;
        r.leader = leader;
        r.sinceMs = m_nowMs;
        m_returns.push_back(std::move(r));
    }

    void CompanionMgr::UpdateReturns()
    {
        for (auto itr = m_returns.begin(); itr != m_returns.end();)
        {
            if (m_nowMs - itr->sinceMs >= RETURN_WINDOW_MS)
            {
                itr = m_returns.erase(itr);
                continue;
            }
            // (groups are loaded at startup, offline members included)
            Group* group = sObjectMgr.GetGroupByMember(itr->leader);
            if (!group)
            {
                itr = m_returns.erase(itr);
                continue;
            }
            for (Group::MemberSlot const& slot : group->GetMemberSlots())
            {
                uint32 const guid = slot.guid.GetCounter();
                CompanionInfo const* info = Find(guid);
                if (slot.guid == itr->leader || !info || info->temporary || itr->sent.count(guid))
                    continue;
                // (still leaving from his last session: next time)
                if (sPlayerBotMgr.HasBot(guid) || sObjectAccessor.FindPlayerNotInWorld(slot.guid))
                    continue;
                itr->sent.insert(guid);
                PartyBotAI* ai = new PartyBotAI(itr->leader);
                if (!sPlayerBotMgr.AddBot(guid, false, ai, true))
                {
                    delete ai;
                    sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Companion] Could not log in companion %u with its leader %u.", guid, itr->leader.GetCounter());
                    continue;
                }
                sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Companion] %u comes back with its leader %u.", guid, itr->leader.GetCounter());
            }
            ++itr;
        }
    }

    void CompanionMgr::OnLeave(uint32 charGuid, bool withLeader, bool staying)
    {
        auto itr = m_companions.find(charGuid);
        if (itr == m_companions.end() || itr->second.temporary)
            return;
        bool const value = withLeader && staying;
        if (itr->second.staying == value)
            return;
        itr->second.staying = value;
        CharacterDatabase.PExecute("UPDATE `custom_companion` SET `staying` = %u WHERE `char_guid` = %u", value ? 1 : 0, charGuid);
    }

    std::string CompanionMgr::CallOver(Player* leader, uint32 charGuid, uint8 role, bool ownerOnly)
    {
        CompanionInfo const* info = Find(charGuid);
        if (!IsEnabled() || !m_available || !leader || !leader->IsInWorld() || !info || info->temporary)
            return "notcompanion";
        if (ownerOnly && info->recruiterGuid != leader->GetGUIDLow())
            return "notyours";
        ObjectGuid const guid(HIGHGUID_PLAYER, charGuid);
        if (sPlayerBotMgr.HasBot(charGuid) || sObjectAccessor.FindPlayerNotInWorld(guid))
            return "online";

        // Its place in his group: already his offline member (it comes back), else a free slot.
        Group* group = leader->GetGroup();
        bool const member = group && group->IsMember(guid);
        if (group && !member && group->IsFull())
            return "full";
        // (an offline member of someone else's group: it leaves that one)
        if (!member)
            if (Group* other = sObjectMgr.GetGroupByMember(guid))
                Player::RemoveFromGroup(other, guid);

        PartyBotAI* ai = new PartyBotAI(leader->GetObjectGuid());
        ai->m_role = CombatBotRoles(role);
        ai->m_roleLocked = CombatBotRoles(role) != ROLE_INVALID;
        if (!sPlayerBotMgr.AddBot(charGuid, false, ai, true))
        {
            delete ai;
            return "failed";
        }
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Companion] %s calls companion %u over.", leader->GetName(), charGuid);
        return "";
    }

    void CompanionMgr::SendHome()
    {
        uint32 const now = uint32(time(nullptr));
        for (auto& itr : m_companions)
        {
            CompanionInfo& info = itr.second;
            uint32 const away = info.lastSeen ? info.lastSeen : info.recruitedAt;
            if (info.temporary || info.home || !away || now < away + HOME_AFTER_SEC)
                continue;
            ObjectGuid const guid(HIGHGUID_PLAYER, info.charGuid);
            // (in the world, or with a group: with someone, not at home)
            if (sPlayerBotMgr.HasBot(info.charGuid) || sObjectAccessor.FindPlayerNotInWorld(guid) || sObjectMgr.GetGroupByMember(guid))
                continue;
            info.home = true;
            CharacterDatabase.PExecute("UPDATE `characters` c JOIN `character_homebind` h ON h.`guid` = c.`guid`"
                " SET c.`map` = h.`map`, c.`zone` = h.`zone`, c.`position_x` = h.`position_x`, c.`position_y` = h.`position_y`,"
                " c.`position_z` = h.`position_z`, c.`instance` = 0, c.`transport_guid` = 0"
                " WHERE c.`guid` = %u AND c.`online` = 0", info.charGuid);
        }
    }

    void CompanionMgr::UpdateBench()
    {
        struct Owner
        {
            Player* player = nullptr;       // null: not online
            Group const* group = nullptr;
            uint32 gained = 0;
            uint32 cap = 0;
            bool active = false;            // playing (not away): expeditions go on
        };
        if (!IsEnabled() || !m_available)
            return;
        std::unordered_map<uint32, Owner> owners;     // owner char guid -> what he gained since the last look

        for (auto const& itr : m_companions)
        {
            uint32 const ownerGuid = itr.second.recruiterGuid;
            if (itr.second.temporary || !ownerGuid || owners.count(ownerGuid))
                continue;
            Owner& owner = owners[ownerGuid];
            Player* player = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, ownerGuid));
            if (!player || !player->IsInWorld() || player->IsBot())
                continue;
            uint32 const total = BenchLeveling::TotalXp(player);
            auto seen = m_ownerXpSeen.find(ownerGuid);
            // (first look, or he went down a level: counting starts here)
            bool const counts = seen != m_ownerXpSeen.end() && total > seen->second;
            if (counts)
                owner.gained = total - seen->second;
            if (seen == m_ownerXpSeen.end() || seen->second != total)
            {
                m_ownerXpSeen[ownerGuid] = total;
                CharacterDatabase.PExecute("REPLACE INTO `custom_companion_owner` (`owner_guid`, `xp_seen`) VALUES (%u, %u)", ownerGuid, total);
            }
            owner.player = player;
            owner.group = player->GetGroup();
            owner.cap = BenchLeveling::CapFor(player->GetLevel());
            owner.active = !player->IsAFK();
        }

        uint32 const now = uint32(time(nullptr));
        uint32 const tickSecs = BENCH_CHECK_MS / IN_MILLISECONDS;
        for (auto& itr : m_companions)
        {
            CompanionInfo& info = itr.second;
            auto owner = owners.find(info.recruiterGuid);
            if (info.temporary || owner == owners.end() || !owner->second.player)
                continue;
            ObjectGuid const guid(HIGHGUID_PLAYER, info.charGuid);
            // On the bench: out of the world and not in his party. With him: no expedition.
            bool const online = sPlayerBotMgr.HasBot(info.charGuid) || sObjectAccessor.FindPlayerNotInWorld(guid);
            if (online)
                m_benched.erase(info.charGuid);
            if (online || (owner->second.group && owner->second.group->IsMember(guid)))
            {
                if (info.expeditionSecs)
                {
                    info.expeditionSecs = 0;
                    CharacterDatabase.PExecute("UPDATE `custom_companion` SET `expedition_secs` = 0 WHERE `char_guid` = %u", info.charGuid);
                }
                continue;
            }
            if (info.lastSeen && now < info.lastSeen + BENCH_SETTLE_SEC)
                continue;
            Benched* benched = BenchedOf(info);
            if (!benched)
                continue;

            // Experience (bench doc §2).
            BenchLeveling::Progress const before{ benched->level, benched->xp };
            BenchLeveling::Progress const after = owner->second.gained
                ? BenchLeveling::Grow(before, BenchLeveling::ShareOf(owner->second.gained), owner->second.cap)
                : before;
            if (after.level != before.level || after.xp != before.xp)
            {
                benched->level = after.level;
                benched->xp = after.xp;
                CharacterDatabase.PExecute("UPDATE `characters` SET `level` = %u, `xp` = %u WHERE `guid` = %u AND `online` = 0",
                    after.level, after.xp, info.charGuid);
            }
            if (after.level != before.level)
            {
                info.benchLevels += after.level - before.level;
                CharacterDatabase.PExecute("UPDATE `custom_companion` SET `bench_levels` = %u WHERE `char_guid` = %u",
                    std::min<uint32>(info.benchLevels, 255), info.charGuid);
                if (PlayerCacheData* data = sObjectMgr.GetPlayerDataByGUID(info.charGuid))
                    data->uiLevel = after.level;
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] %u grew on the bench to level %u (owner %u).",
                    info.charGuid, after.level, info.recruiterGuid);
            }

            // Expedition (bench doc §3): his active time only; stops while he has MAX_UNOPENED of its parcels.
            if (!owner->second.active)
                continue;
            uint32 const secs = std::min(info.expeditionSecs + tickSecs, Expedition::EVERY_SECS);
            if (secs >= Expedition::EVERY_SECS && Expedition::Unopened(info.charGuid, info.recruiterGuid) < Expedition::MAX_UNOPENED)
            {
                SendParcel(owner->second.player, info, *benched);
                info.expeditionSecs = 0;
            }
            else
                info.expeditionSecs = secs;
            CharacterDatabase.PExecute("UPDATE `custom_companion` SET `expedition_secs` = %u WHERE `char_guid` = %u",
                info.expeditionSecs, info.charGuid);
        }
    }

    CompanionMgr::Benched* CompanionMgr::BenchedOf(CompanionInfo const& info)
    {
        auto itr = m_benched.find(info.charGuid);
        if (itr != m_benched.end())
            return &itr->second;
        std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery(
            "SELECT `level`, `xp`, `zone` FROM `characters` WHERE `guid` = %u AND `online` = 0", info.charGuid));
        if (!row)
            return nullptr;
        Field* f = row->Fetch();
        Benched& benched = m_benched[info.charGuid];
        benched.level = f[0].GetUInt32();
        benched.xp = f[1].GetUInt32();
        benched.zone = f[2].GetUInt32();
        return &benched;
    }

    void CompanionMgr::SendParcel(Player* owner, CompanionInfo const& info, Benched const& benched)
    {
        // Its place among his benched companions, by recruit time (the profit part diminishes).
        Group const* group = owner->GetGroup();
        uint32 rank = 0;
        for (auto const& itr : m_companions)
        {
            CompanionInfo const& other = itr.second;
            if (other.charGuid == info.charGuid || other.temporary || other.recruiterGuid != info.recruiterGuid)
                continue;
            bool const earlier = other.recruitedAt < info.recruitedAt
                || (other.recruitedAt == info.recruitedAt && other.charGuid < info.charGuid);
            if (!earlier)
                continue;
            ObjectGuid const guid(HIGHGUID_PLAYER, other.charGuid);
            if (sPlayerBotMgr.HasBot(other.charGuid) || sObjectAccessor.FindPlayerNotInWorld(guid) || (group && group->IsMember(guid)))
                continue;
            ++rank;
        }
        Expedition::Parcel const parcel = Expedition::Pack(owner->GetLevel(), rank, info.charGuid, benched.level);
        std::string name;
        sObjectMgr.GetPlayerNameByGUID(ObjectGuid(HIGHGUID_PLAYER, info.charGuid), name);
        Life::Phrases::Vars vars;
        vars["leader"] = owner->GetName();
        vars["name"] = name;
        vars["zone"] = benched.zone ? Life::Travel::AreaName(benched.zone, owner) : std::string();
        std::string const subject = Life::Phrases::Say("parcel.subject", info.tags, vars);
        std::string const body = Life::Phrases::Say(benched.zone ? "parcel.body" : "parcel.body.nozone", info.tags, vars);
        if (Expedition::Send(owner, info.charGuid, parcel, subject, body))
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] '%s' mailed %s a parcel (%u copper, %u items, bench place %u).",
                name.c_str(), owner->GetName(), parcel.money, uint32(parcel.items.size()), rank + 1);
    }

    bool CompanionMgr::TakeStay(uint32 charGuid)
    {
        auto itr = m_companions.find(charGuid);
        if (itr == m_companions.end() || !itr->second.staying)
            return false;
        itr->second.staying = false;
        if (!itr->second.temporary)
            CharacterDatabase.PExecute("UPDATE `custom_companion` SET `staying` = 0 WHERE `char_guid` = %u", charGuid);
        return true;
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

    bool CompanionMgr::ProfessionsDecided(uint32 charGuid) const
    {
        return IsCompanion(charGuid)
            && std::find(m_professionsPending.begin(), m_professionsPending.end(), charGuid) == m_professionsPending.end();
    }

    std::map<uint32, uint32> CompanionMgr::SkillUpsToday(uint32 charGuid) const
    {
        std::map<uint32, uint32> points;
        CompanionInfo const* info = Find(charGuid);
        if (!info)
            return points;
        uint32 const today = OfflineLife::DayOf(time(nullptr));
        if (info->temporary)
            return info->temporaryDay == today ? info->temporarySkillUps : points;

        // Rows of an earlier day count nothing.
        std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
            "SELECT `skill`, `points` FROM `custom_companion_skillup` WHERE `char_guid` = %u AND `day` = %u", charGuid, today));
        if (rows)
        {
            do
            {
                Field* f = rows->Fetch();
                points[f[0].GetUInt32()] = f[1].GetUInt32();
            } while (rows->NextRow());
        }
        return points;
    }

    bool CompanionMgr::PretendAway(Player* bot, uint32 secs, OfflineLife::Result& result)
    {
        auto itr = bot ? m_companions.find(bot->GetGUIDLow()) : m_companions.end();
        PartyBotAI* ai = bot && bot->IsInWorld() ? dynamic_cast<PartyBotAI*>(bot->AI()) : nullptr;
        if (itr == m_companions.end() || !ai)
            return false;
        uint32 const now = uint32(time(nullptr));
        itr->second.lastSeen = secs < now ? now - secs : 1;
        // Counts as its arrival of this session: UpdatePresence does not run it again.
        m_arrivingSinceMs.erase(itr->first);
        m_present.insert(itr->first);
        result = Arrive(bot, ai, itr->second);
        return true;
    }

    bool CompanionMgr::RedecideProfessions(Player* bot, PastProfessions::Result& result)
    {
        CompanionInfo const* info = bot ? Find(bot->GetGUIDLow()) : nullptr;
        PartyBotAI* ai = bot && bot->IsInWorld() ? dynamic_cast<PartyBotAI*>(bot->AI()) : nullptr;
        if (!info || !ai)
            return false;
        result = PastProfessions::Decide(bot, info->tags);
        ai->m_resetSpellData = true;
        uint32 const guid = info->charGuid;
        m_professionsPending.erase(std::remove(m_professionsPending.begin(), m_professionsPending.end(), guid), m_professionsPending.end());
        if (!info->temporary)
            CharacterDatabase.PExecute("UPDATE `custom_companion` SET `professions_decided` = 1 WHERE `char_guid` = %u", guid);
        return true;
    }

    OfflineLife::Result CompanionMgr::Arrive(Player* bot, PartyBotAI* ai, CompanionInfo& info)
    {
        uint32 const now = uint32(time(nullptr));
        uint32 const offline = info.lastSeen && now > info.lastSeen ? now - info.lastSeen : 0;
        uint32 const today = OfflineLife::DayOf(time_t(now));

        // Everyone has a hearthstone (a new character's starting item; a temporary bot had none):
        // it really uses it on its way to the team (Life/Errands/Travel).
        if (!bot->HasItemCount(Life::Travel::HEARTHSTONE, 1, true))
            bot->StoreNewItemInBestSlots(Life::Travel::HEARTHSTONE, 1);

        std::map<uint32, uint32> points = SkillUpsToday(info.charGuid);
        OfflineLife::Result const result = OfflineLife::Apply(bot, ai->m_lessonContext, info.tags, offline, m_skillUpPriceFactor, points);
        info.lastSeen = now;
        if (info.temporary)
        {
            info.temporaryDay = today;
            info.temporarySkillUps = points;
        }
        else
        {
            for (OfflineLife::Gain const& g : result.gains)
                CharacterDatabase.PExecute("REPLACE INTO `custom_companion_skillup` (`char_guid`, `skill`, `day`, `points`) VALUES (%u, %u, %u, %u)",
                    info.charGuid, g.skill, today, points[g.skill]);
            CharacterDatabase.PExecute("UPDATE `custom_companion` SET `last_seen` = %u WHERE `char_guid` = %u", now, info.charGuid);
        }

        Player* leader = ai->GetPartyLeader();
        LocaleConstant const locale = leader && leader->GetSession() ? leader->GetSession()->GetSessionDbcLocale() : LOCALE_enUS;
        std::string const line = OfflineLife::Line(result, info.tags, locale);
        if (!line.empty())
            BotShared::BotChat::SayToParty(bot, line);

        // It grew on the bench meanwhile (UpdateBench): one line about it.
        if (info.benchLevels)
        {
            Life::Phrases::Vars vars;
            vars["level"] = std::to_string(bot->GetLevel());
            std::string const leveled = Life::Phrases::Say("bench.leveled", info.tags, vars);
            if (!leveled.empty())
                BotShared::BotChat::SayToParty(bot, leveled);
            info.benchLevels = 0;
            if (!info.temporary)
                CharacterDatabase.PExecute("UPDATE `custom_companion` SET `bench_levels` = 0 WHERE `char_guid` = %u", info.charGuid);
        }

        if (!result.gains.empty() || result.shortOfMoney)
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] '%s' back after %u min: %u professions grew for %u copper%s.",
                bot->GetName(), offline / MINUTE, uint32(result.gains.size()), result.spent, result.shortOfMoney ? ", short of money" : "");
        return result;
    }

    void CompanionMgr::UpdatePresence()
    {
        bool const writeLastSeen = m_nowMs - m_lastSeenWriteMs >= LAST_SEEN_MS;
        if (writeLastSeen)
            m_lastSeenWriteMs = m_nowMs;
        uint32 const now = uint32(time(nullptr));
        std::string seen;

        for (auto& itr : m_companions)
        {
            CompanionInfo& info = itr.second;
            if (info.temporary)
                continue;
            Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, info.charGuid));
            PartyBotAI* ai = bot && bot->IsInWorld() ? dynamic_cast<PartyBotAI*>(bot->AI()) : nullptr;
            if (!ai)
            {
                m_present.erase(info.charGuid);
                m_arrivingSinceMs.erase(info.charGuid);
                continue;
            }
            info.home = false;      // (out again later: away from home)
            m_benched.erase(info.charGuid);   // (its saved level changes while it plays)
            if (m_present.count(info.charGuid))
            {
                if (writeLastSeen)
                {
                    info.lastSeen = now;
                    seen += (seen.empty() ? "" : ",") + std::to_string(info.charGuid);
                }
                continue;
            }
            auto arriving = m_arrivingSinceMs.emplace(info.charGuid, m_nowMs).first;
            if (m_nowMs - arriving->second < ARRIVE_SETTLE_MS)
                continue;
            m_arrivingSinceMs.erase(arriving);
            m_present.insert(info.charGuid);
            Arrive(bot, ai, info);
        }

        if (!seen.empty())
            CharacterDatabase.PExecute("UPDATE `custom_companion` SET `last_seen` = %u WHERE `char_guid` IN (%s)", now, seen.c_str());
    }

    void CompanionMgr::Update(uint32 diff)
    {
        m_nowMs += diff;
        if (!m_pastQuestsPending.empty())
            DecidePastQuestsOfOnline();
        if (!m_professionsPending.empty())
            DecideProfessionsOfOnline();
        if (m_nowMs - m_presenceCheckMs >= PRESENCE_CHECK_MS)
        {
            m_presenceCheckMs = m_nowMs;
            UpdatePresence();
        }
        if (!m_returns.empty())
            UpdateReturns();
        if (m_nowMs - m_homeCheckMs >= HOME_CHECK_MS)
        {
            m_homeCheckMs = m_nowMs;
            SendHome();
        }
        if (m_nowMs - m_benchCheckMs >= BENCH_CHECK_MS)
        {
            m_benchCheckMs = m_nowMs;
            UpdateBench();
        }
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
