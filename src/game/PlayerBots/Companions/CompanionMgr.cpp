#include "Companions/CompanionMgr.h"
#include "Companions/Affinity/AffinitySources.h"
#include "Companions/Bench/BenchLeveling.h"
#include "Companions/Bench/Expedition.h"
#include "Companions/ClassQuests/PastQuests.h"
#include "Companions/PastProfessions.h"
#include "Companions/PastRoll.h"
#include "Companions/OfflineLife.h"
#include "Companions/Slots/Slots.h"
#include "Shared/BotChat.h"
#include "Shared/ClassTraining.h"
#include "Shared/DungeonClears.h"
#include "Shared/QuestEvents.h"
#include "Shared/Phrases.h"
#include "Life/Errands/Travel.h"
#include "Shared/TownGates.h"
#include "Companions/Recruit/Trial.h"
#include "Companions/Recruit/FirstCompanion.h"
#include "Companions/Account/Naming.h"
#include "Population/PopulationMgr.h"
#include "PlayerBotMgr.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "Chat.h"
#include "Group.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "AccountMgr.h"
#include "World.h"
#include "WorldSession.h"
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

        // The account the character is saved on (the name cache: an online bot's session has a
        // generated id, PlayerBotMgr::AddBot), 0 = unknown.
        uint32 SavedAccountOf(uint32 charGuid)
        {
            PlayerCacheData const* data = sObjectMgr.GetPlayerDataByGUID(charGuid);
            return data ? data->uiAccount : 0;
        }

        // The character's faction from its race (the name cache, offline too), 0 = unknown.
        uint32 SavedTeamOf(uint32 charGuid)
        {
            PlayerCacheData const* data = sObjectMgr.GetPlayerDataByGUID(charGuid);
            return data ? uint32(Player::TeamForRace(uint8(data->uiRace))) : 0;
        }

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
        Slots::LoadConfig();
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
            " `slots_bought` SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'companion slots he bought (Slots/Slots)',"
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
        std::unique_ptr<QueryResult> parted(CharacterDatabase.Query("SHOW COLUMNS FROM `custom_companion` LIKE 'parted'"));
        if (!parted)
            CharacterDatabase.DirectExecute("ALTER TABLE `custom_companion` ADD COLUMN `parted` TINYINT UNSIGNED NOT NULL DEFAULT 0"
                " COMMENT '1 = a former companion: the player said goodbye (Slots, Part)' AFTER `expedition_secs`");
        std::unique_ptr<QueryResult> trial(CharacterDatabase.Query("SHOW COLUMNS FROM `custom_companion` LIKE 'trial'"));
        if (!trial)
            CharacterDatabase.DirectExecute("ALTER TABLE `custom_companion` ADD COLUMN `trial` TINYINT UNSIGNED NOT NULL DEFAULT 0"
                " COMMENT '1 = a trial partner (搭伙, recruiting doc), not a companion yet', ADD COLUMN `trial_quests` TINYINT UNSIGNED NOT NULL DEFAULT 0"
                " COMMENT 'quests done with the player while a trial partner' AFTER `parted`");
        std::unique_ptr<QueryResult> traits(CharacterDatabase.Query("SHOW COLUMNS FROM `custom_companion` LIKE 'trait_life'"));
        if (!traits)
            CharacterDatabase.DirectExecute("ALTER TABLE `custom_companion` ADD COLUMN `trait_life` VARCHAR(24) NOT NULL DEFAULT ''"
                " COMMENT 'friend trait key rolled at affinity 80 (Affinity/Traits), empty = not yet', ADD COLUMN `trait_combat` VARCHAR(24) NOT NULL DEFAULT ''"
                " COMMENT 'friend trait key rolled at affinity 90' AFTER `trial_quests`");
        std::unique_ptr<QueryResult> inAccount(CharacterDatabase.Query("SHOW COLUMNS FROM `custom_companion` LIKE 'in_account'"));
        if (!inAccount)
            CharacterDatabase.DirectExecute("ALTER TABLE `custom_companion` ADD COLUMN `in_account` TINYINT UNSIGNED NOT NULL DEFAULT 0"
                " COMMENT '1 = joined his player''s account at affinity 100 (account doc)', ADD COLUMN `join_asked` TINYINT UNSIGNED NOT NULL DEFAULT 0"
                " COMMENT '1 = asked the player once to join (Account/JoinOffer)' AFTER `trait_combat`");
        std::unique_ptr<QueryResult> firstOffered(CharacterDatabase.Query("SHOW COLUMNS FROM `custom_companion_owner` LIKE 'first_offered'"));
        if (!firstOffered)
            CharacterDatabase.DirectExecute("ALTER TABLE `custom_companion_owner` ADD COLUMN `first_offered` TINYINT UNSIGNED NOT NULL DEFAULT 0"
                " COMMENT '1 = his first companion came to him (Recruit/FirstCompanion)'");
        std::unique_ptr<QueryResult> slotsBought(CharacterDatabase.Query("SHOW COLUMNS FROM `custom_companion_owner` LIKE 'slots_bought'"));
        if (!slotsBought)
            CharacterDatabase.DirectExecute("ALTER TABLE `custom_companion_owner` ADD COLUMN `slots_bought` SMALLINT UNSIGNED NOT NULL DEFAULT 0"
                " COMMENT 'companion slots he bought (Slots/Slots)' AFTER `xp_seen`");
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
        m_trialEnds.clear();
        m_pending.clear();
        m_returns.clear();
        {
            std::lock_guard<std::mutex> guard(m_accountBotsLock);
            m_accountBots.clear();
        }
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
            "SELECT c.`char_guid`, c.`member_id`, c.`recruiter_guid`, c.`recruited_at`, c.`reward_mode`, c.`past_quests_decided`, c.`affinity`, c.`professions_decided`, c.`last_seen`, c.`money_gifts`, c.`staying`, c.`bench_levels`, c.`expedition_secs`, c.`parted`, c.`trial`, c.`trial_quests`, c.`trait_life`, c.`trait_combat`, c.`in_account`, c.`join_asked` FROM `custom_companion` c"
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
                info.parted = f[13].GetUInt8() != 0;
                info.trial = f[14].GetUInt8() != 0;
                info.trialQuests = f[15].GetUInt32();
                if (Traits::Info const* life = Traits::Find(f[16].GetCppString()))
                    info.traitLife = life->trait;
                if (Traits::Info const* combat = Traits::Find(f[17].GetCppString()))
                    info.traitCombat = combat->trait;
                info.inAccount = f[18].GetUInt8() != 0;
                info.joinAsked = f[19].GetUInt8() != 0;
                // (a trial ends when he leaves the world: the server stopped under him)
                if (info.trial)
                    m_trialEnds.push_back(info.charGuid);
                m_companions[info.charGuid] = info;
            } while (result->NextRow());
        }

        m_ownerXpSeen.clear();
        m_slotsBought.clear();
        m_benched.clear();
        Expedition::Load();
        m_firstOffered.clear();
        std::unique_ptr<QueryResult> owners(CharacterDatabase.Query("SELECT `owner_guid`, `xp_seen`, `slots_bought`, `first_offered` FROM `custom_companion_owner`"));
        if (owners)
        {
            do
            {
                Field* f = owners->Fetch();
                m_ownerXpSeen[f[0].GetUInt32()] = f[1].GetUInt32();
                if (uint32 const bought = f[2].GetUInt32())
                    m_slotsBought[f[0].GetUInt32()] = bought;
                if (f[3].GetUInt8())
                    m_firstOffered.insert(f[0].GetUInt32());
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
        // Recruiting (Recruit/): trial partners count quests and dungeons done with the player,
        // a new player's first quest brings his first companion.
        BotShared::DungeonClears::SetEndListener(&Trial::OnDungeonEnd);
        BotShared::QuestEvents::SetListeners(&FirstCompanion::OnQuestAccepted, &Trial::OnQuestRewarded);

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

    uint32 CompanionMgr::BondedCountOf(uint32 recruiterGuid) const
    {
        std::lock_guard<std::mutex> guard(m_affinityLock);
        uint32 count = 0;
        for (auto const& itr : m_companions)
            if (itr.second.recruiterGuid == recruiterGuid && !itr.second.trial && !itr.second.temporary)
                ++count;
        return count;
    }

    void CompanionMgr::TellBond(Player* player, Player* bot) const
    {
        if (!player || !bot)
            return;
        bool const first = BondedCountOf(player->GetGUIDLow()) == 1;
        BotShared::BotChat::SystemTo(player, "PBTRIAL;" + std::string(bot->GetName()) + ";bond;" + (first ? "1" : "0"));
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

    uint32 CompanionMgr::SetAffinity(uint32 charGuid, uint32 value)
    {
        std::lock_guard<std::mutex> guard(m_affinityLock);
        auto itr = m_companions.find(charGuid);
        if (itr == m_companions.end())
            return 0;
        CompanionInfo& info = itr->second;
        info.affinity = std::min<uint32>(MAX_AFFINITY, value);
        if (!info.temporary)
            CharacterDatabase.PExecute("UPDATE `custom_companion` SET `affinity` = %u WHERE `char_guid` = %u", info.affinity, charGuid);
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Companion] %u: affinity set to %u.", charGuid, info.affinity);
        return info.affinity;
    }

    TraitPair CompanionMgr::GetTraits(uint32 charGuid) const
    {
        std::lock_guard<std::mutex> guard(m_affinityLock);
        TraitPair pair;
        if (CompanionInfo const* info = Find(charGuid))
        {
            pair.life = info->traitLife;
            pair.combat = info->traitCombat;
        }
        return pair;
    }

    void CompanionMgr::SetTrait(uint32 charGuid, Traits::Tier tier, Traits::Trait trait)
    {
        std::lock_guard<std::mutex> guard(m_affinityLock);
        auto itr = m_companions.find(charGuid);
        if (itr == m_companions.end())
            return;
        CompanionInfo& info = itr->second;
        bool const life = tier == Traits::Tier::Life;
        (life ? info.traitLife : info.traitCombat) = trait;
        Traits::Info const* row = Traits::Of(trait);
        if (!info.temporary)
            CharacterDatabase.PExecute("UPDATE `custom_companion` SET `%s` = '%s' WHERE `char_guid` = %u",
                life ? "trait_life" : "trait_combat", row ? row->key : "", charGuid);
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Companion] %u: %s trait %s.", charGuid, life ? "life" : "combat", row ? row->key : "cleared");
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

    uint32 CompanionMgr::CountTrialQuest(uint32 charGuid)
    {
        std::lock_guard<std::mutex> guard(m_affinityLock);
        auto itr = m_companions.find(charGuid);
        if (itr == m_companions.end() || !itr->second.trial)
            return 0;
        CompanionInfo& info = itr->second;
        ++info.trialQuests;
        CharacterDatabase.PExecute("UPDATE `custom_companion` SET `trial_quests` = %u WHERE `char_guid` = %u", info.trialQuests, charGuid);
        return info.trialQuests;
    }

    void CompanionMgr::RaiseTrialQuests(uint32 charGuid, uint32 quests)
    {
        std::lock_guard<std::mutex> guard(m_affinityLock);
        auto itr = m_companions.find(charGuid);
        if (itr == m_companions.end() || !itr->second.trial || itr->second.trialQuests >= quests)
            return;
        itr->second.trialQuests = quests;
        CharacterDatabase.PExecute("UPDATE `custom_companion` SET `trial_quests` = %u WHERE `char_guid` = %u", quests, charGuid);
    }

    uint32 CompanionMgr::TrialQuests(uint32 charGuid) const
    {
        std::lock_guard<std::mutex> guard(m_affinityLock);
        CompanionInfo const* info = Find(charGuid);
        return info && info->trial ? info->trialQuests : 0;
    }

    void CompanionMgr::ScheduleTrialOffer(uint32 charGuid, uint32 atMs)
    {
        std::lock_guard<std::mutex> guard(m_affinityLock);
        auto itr = m_companions.find(charGuid);
        if (itr == m_companions.end() || !itr->second.trial)
            return;
        uint32& due = itr->second.trialOfferAtMs;
        if (!due || atMs < due)
            due = std::max<uint32>(atMs, 1);
    }

    bool CompanionMgr::TakeTrialOffer(uint32 charGuid, uint32 nowMs)
    {
        std::lock_guard<std::mutex> guard(m_affinityLock);
        auto itr = m_companions.find(charGuid);
        if (itr == m_companions.end() || !itr->second.trial)
            return false;
        uint32& due = itr->second.trialOfferAtMs;
        if (!due || nowMs < due)
            return false;
        due = 0;
        return true;
    }

    uint32 CompanionMgr::Promote(Player* bot)
    {
        if (!bot)
            return 0;
        uint32 const guid = bot->GetGUIDLow();
        std::lock_guard<std::mutex> guard(m_affinityLock);
        auto itr = m_companions.find(guid);
        if (itr == m_companions.end() || !itr->second.trial)
            return 0;
        CompanionInfo& info = itr->second;
        info.trial = false;
        info.trialOfferAtMs = 0;
        info.affinity = std::max<uint32>(AffinitySources::StartAt(bot->GetLevel()), 1);
        CharacterDatabase.PExecute("UPDATE `custom_companion` SET `trial` = 0, `affinity` = %u WHERE `char_guid` = %u", info.affinity, guid);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] Trial partner '%s' (%u) is a companion now, affinity %u.",
            bot->GetName(), guid, info.affinity);
        return info.affinity;
    }

    void CompanionMgr::OnTrialLeave(uint32 charGuid)
    {
        if (!IsTrial(charGuid))
            return;
        if (std::find(m_trialEnds.begin(), m_trialEnds.end(), charGuid) == m_trialEnds.end())
            m_trialEnds.push_back(charGuid);
    }

    void CompanionMgr::UpdateTrialEnds()
    {
        for (auto itr = m_trialEnds.begin(); itr != m_trialEnds.end();)
        {
            uint32 const charGuid = *itr;
            CompanionInfo const* info = Find(charGuid);
            if (!info || !info->trial)
            {
                itr = m_trialEnds.erase(itr);
                continue;
            }
            ObjectGuid const guid(HIGHGUID_PLAYER, charGuid);
            // (still logging out)
            if (sPlayerBotMgr.HasBot(charGuid) || sObjectAccessor.FindPlayerNotInWorld(guid) || sObjectAccessor.FindPlayer(guid))
            {
                ++itr;
                continue;
            }
            uint32 const memberId = info->memberId;
            uint32 level = 0;
            if (PlayerCacheData const* data = sObjectMgr.GetPlayerDataByGUID(charGuid))
                level = data->uiLevel;
            if (Group* group = sObjectMgr.GetGroupByMember(guid))
                Player::RemoveFromGroup(group, guid);
            DeleteCharacter(charGuid);
            if (memberId)
                sPopulationMgr.ReturnFromTrial(memberId, uint8(std::min<uint32>(level, 255)));
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] Trial partner %u (roster member %u) went his own way: character deleted.",
                charGuid, memberId);
            itr = m_trialEnds.erase(itr);
        }
    }

    void CompanionMgr::DeleteCharacter(uint32 charGuid)
    {
        {
            std::lock_guard<std::mutex> guard(m_affinityLock);   // (OfRecruiter on other threads)
            m_companions.erase(charGuid);
        }
        m_benched.erase(charGuid);
        m_present.erase(charGuid);
        m_arrivingSinceMs.erase(charGuid);
        for (char const* table : { "custom_companion", "custom_companion_skillup", "custom_companion_clear", "custom_soulbot_request" })
            CharacterDatabase.PExecute("DELETE FROM `%s` WHERE `char_guid` = %u", table, charGuid);
        Player::DeleteFromDB(ObjectGuid(HIGHGUID_PLAYER, charGuid), m_accountId, true, true);
    }

    bool CompanionMgr::FirstOffered(uint32 ownerGuid) const
    {
        return m_firstOffered.count(ownerGuid) != 0;
    }

    void CompanionMgr::MarkFirstOffered(uint32 ownerGuid)
    {
        if (!m_firstOffered.insert(ownerGuid).second)
            return;
        CharacterDatabase.PExecute("INSERT INTO `custom_companion_owner` (`owner_guid`, `first_offered`) VALUES (%u, 1)"
            " ON DUPLICATE KEY UPDATE `first_offered` = 1", ownerGuid);
    }

    bool CompanionMgr::Recruit(Player* bot, Player* recruiter, uint32 memberId, bool trial)
    {
        if (!IsEnabled() || !m_available || !bot || !recruiter || !bot->IsInWorld() || !recruiter->IsInWorld())
            return false;
        if (!bot->IsBot() || !bot->IsSavingDisabled() || recruiter->IsBot())
            return false;

        uint32 const guid = bot->GetGUIDLow();
        if (IsCompanion(guid) || !HasFreeSlot(recruiter))
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
        // (a trial partner's affinity starts when he is promoted)
        info.trial = trial;
        info.affinity = trial ? 0 : AffinitySources::StartAt(bot->GetLevel());
        CharacterDatabase.PExecute("REPLACE INTO `custom_companion` (`char_guid`, `member_id`, `recruiter_guid`, `recruited_at`, `past_quests_decided`, `affinity`, `professions_decided`, `last_seen`, `trial`)"
            " VALUES (%u, %u, %u, %u, 1, %u, 1, %u, %u)", info.charGuid, info.memberId, info.recruiterGuid, info.recruitedAt, info.affinity, info.lastSeen,
            trial ? 1 : 0);
        {
            std::lock_guard<std::mutex> guard(m_affinityLock);   // (OfRecruiter on other threads)
            m_companions[guid] = info;
        }

        // The name cache holds the temporary session's account; after a restart it is read from `characters`.
        if (PlayerCacheData* data = sObjectMgr.GetPlayerDataByGUID(guid))
            data->uiAccount = account;
        sWorld.UpdateRealmCharCount(account);

        PendingLogin login;
        login.charGuid = guid;
        login.recruiter = recruiter->GetObjectGuid();
        login.sinceMs = m_nowMs;
        m_pending.push_back(login);
        // (a trial partner is told when he is promoted: Trial::Bond)
        if (!trial)
            TellBond(recruiter, bot);

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] '%s' (%u, level %u) %s '%s', %u trainer spells learned (up to level %u),"
            " class quests: %u done, %u not done, %u abilities taken away; professions %u (%u) + %u (%u), %u recipes.",
            bot->GetName(), guid, bot->GetLevel(), trial ? "joins as a trial partner of" : "recruited by", recruiter->GetName(),
            lessons.learned, trainedUpTo, past.done, past.notDone, past.revoked,
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
        LeaveAltsParty(charGuid);
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
            uint32 const leaderAccount = SavedAccountOf(itr->leader.GetCounter());
            uint32 const leaderTeam = SavedTeamOf(itr->leader.GetCounter());
            std::vector<ObjectGuid> enemies;
            for (Group::MemberSlot const& slot : group->GetMemberSlots())
            {
                uint32 const guid = slot.guid.GetCounter();
                CompanionInfo const* info = Find(guid);
                // (an account character: his own one that was his bot here, account doc §3)
                bool const accountChar = !info && leaderAccount && SavedAccountOf(guid) == leaderAccount;
                if (slot.guid == itr->leader || (!info && !accountChar) || (info && info->temporary) || itr->sent.count(guid))
                    continue;
                // (of the other faction, from before bots kept to their side: he leaves the group)
                if (leaderTeam && SavedTeamOf(guid) != leaderTeam)
                {
                    if (!sPlayerBotMgr.HasBot(guid) && !sObjectAccessor.FindPlayerNotInWorld(slot.guid))
                        enemies.push_back(slot.guid);
                    continue;
                }
                // (still leaving from his last session: next time)
                if (sPlayerBotMgr.HasBot(guid) || sObjectAccessor.FindPlayerNotInWorld(slot.guid))
                    continue;
                itr->sent.insert(guid);
                if (accountChar)
                    MarkAccountBot(guid);
                PartyBotAI* ai = new PartyBotAI(itr->leader);
                if (!sPlayerBotMgr.AddBot(guid, false, ai, true))
                {
                    ForgetAccountBot(guid);
                    delete ai;
                    sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[Companion] Could not log in companion %u with its leader %u.", guid, itr->leader.GetCounter());
                    continue;
                }
                sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Companion] %u comes back with its leader %u.", guid, itr->leader.GetCounter());
            }
            // (after the loop: removing changes the member slots, and may disband the group)
            for (ObjectGuid const& enemy : enemies)
            {
                Player::RemoveFromGroup(group, enemy);
                sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Companion] %u leaves the group of %u: the other faction.", enemy.GetCounter(), itr->leader.GetCounter());
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
        if (!IsEnabled() || !m_available || !leader || !leader->IsInWorld())
            return "notcompanion";
        // A companion, or one of the leader's own characters (account doc §3).
        bool const ownChar = !info && IsOfAccount(leader, charGuid);
        if ((!info && !ownChar) || (info && (info->temporary || info->trial)))
            return "notcompanion";
        if (ownerOnly && !MayManage(leader, charGuid))
            return "notyours";
        if (info && info->parted)
            return "former";
        if (!SameSide(leader, charGuid))
            return "enemy";
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

        if (ownChar)
            MarkAccountBot(charGuid);
        PartyBotAI* ai = new PartyBotAI(leader->GetObjectGuid());
        ai->m_role = CombatBotRoles(role);
        ai->m_roleLocked = CombatBotRoles(role) != ROLE_INVALID;
        if (!sPlayerBotMgr.AddBot(charGuid, false, ai, true))
        {
            ForgetAccountBot(charGuid);
            delete ai;
            return "failed";
        }
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Companion] %s calls companion %u over.", leader->GetName(), charGuid);
        return "";
    }

    bool CompanionMgr::OnInviteOffline(Player* inviter, std::string const& name)
    {
        if (!inviter || inviter->IsBot())
            return false;
        uint32 const charGuid = sObjectMgr.GetPlayerGuidByName(name).GetCounter();
        if (!charGuid)
            return false;
        std::string const error = CallOver(inviter, charGuid, uint8(ROLE_INVALID), true);
        if (error == "notcompanion" || error == "notyours" || error == "former")
            return false;
        // (the client's own "wrong faction" message, as for a player of the other side)
        if (error == "enemy")
        {
            inviter->GetSession()->SendPartyResult(PARTY_OP_INVITE, name, ERR_PLAYER_WRONG_FACTION);
            return true;
        }
        ChatHandler handler(inviter);
        if (error.empty())
            handler.PSendSysMessage("PBOK;%s;callover", name.c_str());
        else
            handler.PSendSysMessage("PBERR;%s;%s", name.c_str(), error.c_str());
        return true;
    }

    bool CompanionMgr::IsOfAccount(Player const* owner, uint32 charGuid)
    {
        if (!owner || owner->IsBot() || !owner->GetSession() || !charGuid || charGuid == owner->GetGUIDLow())
            return false;
        uint32 const account = owner->GetSession()->GetAccountId();
        return account && SavedAccountOf(charGuid) == account;
    }

    bool CompanionMgr::SameSide(Player const* leader, uint32 charGuid)
    {
        if (!leader)
            return false;
        if (leader->IsGameMaster())
            return true;
        return SavedTeamOf(charGuid) == uint32(leader->GetTeam());
    }

    bool CompanionMgr::SameSide(Player const* leader, Player const* bot)
    {
        return leader && bot && (leader->IsGameMaster() || bot->GetTeam() == leader->GetTeam());
    }

    bool CompanionMgr::MayManage(Player const* owner, uint32 charGuid) const
    {
        if (!owner)
            return false;
        CompanionInfo const* info = Find(charGuid);
        if (info && !info->inAccount)
            return info->recruiterGuid == owner->GetGUIDLow();
        return IsOfAccount(owner, charGuid);
    }

    bool CompanionMgr::IsAccountBot(uint32 charGuid) const
    {
        std::lock_guard<std::mutex> guard(m_accountBotsLock);
        return m_accountBots.count(charGuid) != 0;
    }

    void CompanionMgr::MarkAccountBot(uint32 charGuid)
    {
        std::lock_guard<std::mutex> guard(m_accountBotsLock);
        m_accountBots.insert(charGuid);
    }

    void CompanionMgr::ForgetAccountBot(uint32 charGuid)
    {
        std::lock_guard<std::mutex> guard(m_accountBotsLock);
        m_accountBots.erase(charGuid);
    }

    std::vector<CompanionMgr::AccountCharacter> CompanionMgr::AccountCharactersOf(Player const* owner) const
    {
        std::vector<AccountCharacter> list;
        if (!owner || owner->IsBot() || !owner->GetSession())
            return list;
        std::vector<PlayerCacheData const*> chars;
        sObjectMgr.GetPlayerDataForAccount(owner->GetSession()->GetAccountId(), chars);
        for (PlayerCacheData const* data : chars)
        {
            // (an alt of the other faction does not know those of this side, nor they him)
            if (data->uiGuid == owner->GetGUIDLow() || !SameSide(owner, data->uiGuid))
                continue;
            AccountCharacter entry;
            entry.charGuid = data->uiGuid;
            entry.kind = IsInAccount(data->uiGuid) ? AccountKind::Companion : AccountKind::Own;
            list.push_back(entry);
        }
        return list;
    }

    void CompanionMgr::MoveToAccount(uint32 charGuid, uint32 fromAccount, uint32 toAccount)
    {
        CharacterDatabase.PExecute("UPDATE `characters` SET `account` = %u WHERE `guid` = %u", toAccount, charGuid);
        // (online: his next save goes to the new account, Player::SaveToDB)
        if (Player* bot = sObjectAccessor.FindPlayerNotInWorld(ObjectGuid(HIGHGUID_PLAYER, charGuid)))
            if (bot->IsBot())
                bot->EnableSaving(toAccount);
        if (PlayerCacheData* data = sObjectMgr.GetPlayerDataByGUID(charGuid))
            data->uiAccount = toAccount;
        if (fromAccount)
            sWorld.UpdateRealmCharCount(fromAccount);
        sWorld.UpdateRealmCharCount(toAccount);
    }

    bool CompanionMgr::AccountHasRoom(Player const* owner) const
    {
        if (!owner || !owner->GetSession())
            return false;
        std::vector<PlayerCacheData const*> chars;
        sObjectMgr.GetPlayerDataForAccount(owner->GetSession()->GetAccountId(), chars);
        return chars.size() < sWorld.getConfig(CONFIG_UINT32_CHARACTERS_PER_REALM);
    }

    std::string CompanionMgr::JoinAccount(Player* owner, uint32 charGuid, std::string& newName)
    {
        auto itr = m_companions.find(charGuid);
        if (!owner || owner->IsBot() || !owner->GetSession() || itr == m_companions.end() || itr->second.temporary || itr->second.trial)
            return "notcompanion";
        CompanionInfo& info = itr->second;
        if (info.inAccount)
            return "inaccount";
        if (info.recruiterGuid != owner->GetGUIDLow())
            return "notyours";
        if (!SameSide(owner, charGuid))
            return "enemy";
        if (GetAffinity(charGuid) < MAX_AFFINITY)
            return "locked";
        if (!AccountHasRoom(owner))
            return "accountfull";
        std::string oldName;
        sObjectMgr.GetPlayerNameByGUID(ObjectGuid(HIGHGUID_PLAYER, charGuid), oldName);
        if (!newName.empty())
        {
            std::string const refused = Naming::Check(charGuid, newName);
            if (!refused.empty())
                return refused;
        }

        uint32 const account = owner->GetSession()->GetAccountId();
        MoveToAccount(charGuid, ResolveAccount(), account);
        if (!newName.empty() && newName != oldName)
            Naming::Rename(owner, charGuid, info.memberId, oldName, newName);
        info.inAccount = true;
        info.joinAsked = true;
        info.staying = false;
        info.expeditionSecs = 0;
        m_benched.erase(charGuid);
        CharacterDatabase.PExecute("UPDATE `custom_companion` SET `in_account` = 1, `join_asked` = 1, `staying` = 0, `expedition_secs` = 0"
            " WHERE `char_guid` = %u", charGuid);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] Companion %u joined the account of %s (%u).", charGuid, owner->GetName(), account);
        return "";
    }

    void CompanionMgr::MarkJoinAsked(uint32 charGuid)
    {
        auto itr = m_companions.find(charGuid);
        if (itr == m_companions.end() || itr->second.joinAsked)
            return;
        itr->second.joinAsked = true;
        if (!itr->second.temporary)
            CharacterDatabase.PExecute("UPDATE `custom_companion` SET `join_asked` = 1 WHERE `char_guid` = %u", charGuid);
    }

    std::string CompanionMgr::SendBack(Player* owner, uint32 charGuid, bool& parted)
    {
        parted = false;
        if (!owner)
            return "notcompanion";
        if (charGuid == owner->GetGUIDLow())
            return "self";
        auto itr = m_companions.find(charGuid);
        if (itr == m_companions.end() || itr->second.temporary)
            return "notcompanion";
        CompanionInfo& info = itr->second;
        if (!info.inAccount)
            return "notinaccount";
        if (!IsOfAccount(owner, charGuid))
            return "notyours";
        // (he would become the companion of an alt of the other faction)
        if (!SameSide(owner, charGuid))
            return "enemy";
        uint32 const account = ResolveAccount();
        if (!account)
            return "failed";

        // (before he counts: an account character takes no slot)
        parted = !HasFreeSlot(owner);
        MoveToAccount(charGuid, SavedAccountOf(charGuid), account);
        info.inAccount = false;
        info.recruiterGuid = owner->GetGUIDLow();
        info.parted = parted;
        info.staying = false;
        // (no offline life nor bench growth for the time in the account)
        info.lastSeen = uint32(time(nullptr));
        CharacterDatabase.PExecute("UPDATE `custom_companion` SET `in_account` = 0, `recruiter_guid` = %u, `parted` = %u, `staying` = 0,"
            " `last_seen` = %u WHERE `char_guid` = %u", info.recruiterGuid, parted ? 1 : 0, info.lastSeen, charGuid);

        ObjectGuid const guid(HIGHGUID_PLAYER, charGuid);
        Player* bot = sObjectAccessor.FindPlayer(guid);
        PartyBotAI* ai = bot && bot->IsInWorld() ? dynamic_cast<PartyBotAI*>(bot->AI()) : nullptr;
        if (parted)
        {
            // As a goodbye (Part): online he walks off, else he leaves the group he is in.
            if (ai)
                ai->Dismiss();
            else if (Group* group = sObjectMgr.GetGroupByMember(guid))
                Player::RemoveFromGroup(group, guid);
        }
        else if (!ai)
        {
            // (an offline member of another character's party: his place is with his owner now)
            if (Group* group = sObjectMgr.GetGroupByMember(guid))
                if (!owner->GetGroup() || owner->GetGroup() != group)
                    Player::RemoveFromGroup(group, guid);
        }
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] %s sent account character %u back as a %s.", owner->GetName(), charGuid,
            parted ? "former companion" : "companion");
        return "";
    }

    void CompanionMgr::LeaveAltsParty(uint32 charGuid)
    {
        ObjectGuid const guid(HIGHGUID_PLAYER, charGuid);
        Group* group = sObjectMgr.GetGroupByMember(guid);
        if (!group || group->GetLeaderGuid() == guid)
            return;
        ObjectGuid const leader = group->GetLeaderGuid();
        uint32 const account = SavedAccountOf(charGuid);
        if (!account || SavedAccountOf(leader.GetCounter()) != account || sObjectAccessor.FindPlayerNotInWorld(leader))
            return;
        Player::RemoveFromGroup(group, guid);
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Companion] %u logs in: leaves the party of his account's character %u.", charGuid, leader.GetCounter());
    }

    CompanionMgr::SlotCount CompanionMgr::SlotsOf(Player const* owner) const
    {
        SlotCount count;
        if (!owner)
            return count;
        uint32 const ownerGuid = owner->GetGUIDLow();
        for (auto const& itr : m_companions)
            if (itr.second.recruiterGuid == ownerGuid && !itr.second.temporary && !itr.second.parted && !itr.second.inAccount)
                ++count.used;
        auto const bought = m_slotsBought.find(ownerGuid);
        count.total = std::min(Slots::FreeAt(owner->GetLevel()) + (bought != m_slotsBought.end() ? bought->second : 0), Slots::Max());
        count.nextFreeLevel = Slots::NextFreeLevel(owner->GetLevel());
        count.canBuy = !count.nextFreeLevel && count.total < Slots::Max();
        count.price = count.canBuy ? Slots::PriceOf(count.total + 1) : 0;
        return count;
    }

    bool CompanionMgr::HasFreeSlot(Player const* owner) const
    {
        SlotCount const count = SlotsOf(owner);
        return count.used < count.total;
    }

    std::string CompanionMgr::BuySlot(Player* owner)
    {
        if (!owner)
            return "notyet";
        SlotCount const count = SlotsOf(owner);
        if (count.nextFreeLevel)
            return "notyet";
        if (!count.canBuy)
            return "maxslots";
        if (owner->GetMoney() < count.price)
            return "nomoney";

        uint32 const ownerGuid = owner->GetGUIDLow();
        owner->ModifyMoney(-int32(count.price));
        owner->SaveInventoryAndGoldToDB();
        uint32 const bought = ++m_slotsBought[ownerGuid];
        CharacterDatabase.PExecute("INSERT INTO `custom_companion_owner` (`owner_guid`, `slots_bought`) VALUES (%u, %u)"
            " ON DUPLICATE KEY UPDATE `slots_bought` = VALUES(`slots_bought`)", ownerGuid, bought);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] %s bought companion slot %u for %u copper.",
            owner->GetName(), count.total + 1, count.price);
        return "";
    }

    std::string CompanionMgr::Part(Player* owner, uint32 charGuid)
    {
        auto itr = m_companions.find(charGuid);
        if (!owner || itr == m_companions.end() || itr->second.temporary)
            return "notcompanion";
        CompanionInfo& info = itr->second;
        if (info.inAccount)
            return "inaccount";
        if (info.recruiterGuid != owner->GetGUIDLow())
            return "notyours";
        if (info.parted)
            return "former";

        // A trial partner just goes his way (no former companion: OnTrialLeave deletes him).
        if (info.trial)
        {
            ObjectGuid const guid(HIGHGUID_PLAYER, charGuid);
            Player* bot = sObjectAccessor.FindPlayer(guid);
            PartyBotAI* ai = bot && bot->IsInWorld() ? dynamic_cast<PartyBotAI*>(bot->AI()) : nullptr;
            if (ai)
                ai->Dismiss();
            else
                OnTrialLeave(charGuid);
            return "";
        }

        info.parted = true;
        info.staying = false;
        info.expeditionSecs = 0;
        m_benched.erase(charGuid);
        CharacterDatabase.PExecute("UPDATE `custom_companion` SET `parted` = 1, `staying` = 0, `expedition_secs` = 0 WHERE `char_guid` = %u", charGuid);

        // With him: says goodbye and walks off (presence P5). Elsewhere: no longer in his group.
        ObjectGuid const guid(HIGHGUID_PLAYER, charGuid);
        Player* bot = sObjectAccessor.FindPlayer(guid);
        PartyBotAI* ai = bot && bot->IsInWorld() ? dynamic_cast<PartyBotAI*>(bot->AI()) : nullptr;
        if (ai)
            ai->Dismiss();
        else if (Group* group = sObjectMgr.GetGroupByMember(guid))
            Player::RemoveFromGroup(group, guid);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] %s said goodbye to companion %u.", owner->GetName(), charGuid);
        return "";
    }

    std::string CompanionMgr::Rehire(Player* owner, uint32 charGuid)
    {
        auto itr = m_companions.find(charGuid);
        if (!owner || itr == m_companions.end() || itr->second.temporary)
            return "notcompanion";
        CompanionInfo& info = itr->second;
        if (info.inAccount)
            return "inaccount";
        if (info.recruiterGuid != owner->GetGUIDLow())
            return "notyours";
        if (!info.parted)
            return "notformer";
        if (!HasFreeSlot(owner))
            return "noslot";

        info.parted = false;
        // (the bench counts from now: no levels nor parcels for the time apart)
        info.lastSeen = uint32(time(nullptr));
        CharacterDatabase.PExecute("UPDATE `custom_companion` SET `parted` = 0 WHERE `char_guid` = %u", charGuid);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] %s invited former companion %u back.", owner->GetName(), charGuid);
        return "";
    }

    std::string CompanionMgr::Forget(Player* owner, uint32 charGuid, uint32& memberId, uint32& level)
    {
        memberId = 0;
        level = 0;
        auto itr = m_companions.find(charGuid);
        if (!owner || itr == m_companions.end() || itr->second.temporary)
            return "notcompanion";
        if (itr->second.inAccount)
            return "inaccount";
        if (itr->second.recruiterGuid != owner->GetGUIDLow())
            return "notyours";
        if (!itr->second.parted)
            return "notformer";
        ObjectGuid const guid(HIGHGUID_PLAYER, charGuid);
        // (still walking off after the goodbye)
        if (sPlayerBotMgr.HasBot(charGuid) || sObjectAccessor.FindPlayerNotInWorld(guid) || sObjectAccessor.FindPlayer(guid))
            return "leaving";

        memberId = itr->second.memberId;
        if (PlayerCacheData const* data = sObjectMgr.GetPlayerDataByGUID(charGuid))
            level = data->uiLevel;
        DeleteCharacter(charGuid);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] %s parted for good with former companion %u (roster member %u): character deleted.",
            owner->GetName(), charGuid, memberId);
        return "";
    }

    void CompanionMgr::SendHome()
    {
        uint32 const now = uint32(time(nullptr));
        for (auto& itr : m_companions)
        {
            CompanionInfo& info = itr.second;
            uint32 const away = info.lastSeen ? info.lastSeen : info.recruitedAt;
            if (info.temporary || info.inAccount || info.home || !away || now < away + HOME_AFTER_SEC)
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
                CharacterDatabase.PExecute("INSERT INTO `custom_companion_owner` (`owner_guid`, `xp_seen`) VALUES (%u, %u)"
                    " ON DUPLICATE KEY UPDATE `xp_seen` = VALUES(`xp_seen`)", ownerGuid, total);
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
            if (info.temporary || info.parted || info.inAccount || owner == owners.end() || !owner->second.player)
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
            // (远行, a friend trait: more often)
            uint32 const every = GetTraits(info.charGuid).life == Traits::Trait::Wanderer
                ? Expedition::EVERY_SECS * 3 / 4 : Expedition::EVERY_SECS;
            uint32 const secs = std::min(info.expeditionSecs + tickSecs, every);
            // (past his first PARCEL_SENDERS benched ones: nobody else sends)
            uint32 const rank = secs >= every ? BenchRank(owner->second.player, info) : 0;
            if (secs >= every && rank < PARCEL_SENDERS
                && Expedition::Unopened(info.charGuid, info.recruiterGuid) < Expedition::MAX_UNOPENED)
            {
                SendParcel(owner->second.player, info, *benched, rank);
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

    uint32 CompanionMgr::BenchRank(Player const* owner, CompanionInfo const& info) const
    {
        Group const* group = owner->GetGroup();
        uint32 rank = 0;
        for (auto const& itr : m_companions)
        {
            CompanionInfo const& other = itr.second;
            if (other.charGuid == info.charGuid || other.temporary || other.parted || other.inAccount || other.recruiterGuid != info.recruiterGuid)
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
        return rank;
    }

    void CompanionMgr::SendParcel(Player* owner, CompanionInfo const& info, Benched const& benched, uint32 rank)
    {
        // (the profit part diminishes with `rank`, its place among his benched companions)
        Expedition::Parcel const parcel = Expedition::Pack(owner->GetLevel(), rank, info.charGuid, benched.level);
        std::string name;
        sObjectMgr.GetPlayerNameByGUID(ObjectGuid(HIGHGUID_PLAYER, info.charGuid), name);
        BotShared::Phrases::Vars vars;
        vars["leader"] = owner->GetName();
        vars["name"] = name;
        vars["zone"] = benched.zone ? Life::Travel::AreaName(benched.zone, owner) : std::string();
        std::string const subject = BotShared::Phrases::Say(BotShared::I18n::Of(owner), "parcel.subject", info.tags, vars);
        std::string const body = BotShared::Phrases::Say(BotShared::I18n::Of(owner), benched.zone ? "parcel.body" : "parcel.body.nozone", info.tags, vars);
        if (Expedition::Send(owner, info.charGuid, parcel, subject, body))
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] '%s' mailed %s a parcel (%u copper, %u items, surprise %u, bench place %u).",
                name.c_str(), owner->GetName(), parcel.money, uint32(parcel.items.size()), parcel.surprise, rank + 1);
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
        std::string const line = OfflineLife::Line(result, info.tags, locale, BotShared::I18n::OfParty(bot));
        if (!line.empty())
            BotShared::BotChat::SayToParty(bot, line);

        // It grew on the bench meanwhile (UpdateBench): one line about it.
        if (info.benchLevels)
        {
            BotShared::Phrases::Vars vars;
            vars["level"] = std::to_string(bot->GetLevel());
            std::string const leveled = BotShared::Phrases::Say(BotShared::I18n::OfParty(bot), "bench.leveled", info.tags, vars);
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
            // (an account character has no offline life: the player may have been playing him)
            if (info.temporary || info.inAccount)
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
        if (!m_trialEnds.empty())
            UpdateTrialEnds();
        FirstCompanion::Update();
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

            // The recruiter left meanwhile: the companion stays saved, .partybot load brings it back
            // (a trial partner goes his way: the trial is over).
            if (Player* leader = sObjectAccessor.FindPlayer(itr->recruiter))
                LoginAsPartyBot(itr->charGuid, leader);
            else
                OnTrialLeave(itr->charGuid);
            itr = m_pending.erase(itr);
        }
    }
}
