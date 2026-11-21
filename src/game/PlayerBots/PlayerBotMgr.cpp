#include "Common.h"
#include "Policies/SingletonImp.h"
#include "PlayerBotMgr.h"
#include "ObjectMgr.h"
#include "World.h"
#include "WorldSession.h"
#include "AccountMgr.h"
#include "Config/Config.h"
#include "Chat.h"
#include "Player.h"
#include "Group.h"
#include "PlayerBotAI.h"
#include "PartyBotAI.h"
#include "MapManager.h"
#include "Language.h"
#include "Utilities/Random.h"
#include "Spell.h"
#include "Bag.h"
#include "QuestDef.h"
#include "Population/PopulationMgr.h"
#include "Companions/CompanionMgr.h"
#include "Companions/Affinity/Gates.h"
#include "Life/Requests/RequestBoard.h"
#include "Shared/QuestMirror.h"
#include "Shared/ArtisanTrainers.h"
#include "Shared/QuestPlaces.h"
#include "Shared/ClassQuestChains.h"
#include "RoleCapability.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Coordination/Doctrine/HealerDamage.h"

#include <sstream>
#include <cstring>
#include <algorithm>
#include <map>

INSTANTIATE_SINGLETON_1(PlayerBotMgr);


PlayerBotMgr::PlayerBotMgr()
{
    m_totalChance = 0;
    m_maxAccountId = 0;

    // Config
    m_confMinRandomBots         = 3;
    m_confMaxRandomBots         = 10;
    m_confRandomBotsRefresh     = 60000;
    m_confUpdateDiff            = 10000;
    m_confEnableRandomBots      = false;
    m_confDebug                 = false;

    // Time
    m_elapsedTime = 0;
    m_lastBotsRefresh = 0;
    m_lastUpdate = 0;
}

PlayerBotMgr::~PlayerBotMgr()
{

}

void PlayerBotMgr::LoadConfig()
{
    m_confEnableRandomBots = sConfig.GetBoolDefault("RandomBot.Enable", false);
    m_confMinRandomBots = sConfig.GetIntDefault("RandomBot.MinBots", 3);
    m_confMaxRandomBots = sConfig.GetIntDefault("RandomBot.MaxBots", 10);
    m_confRandomBotsRefresh = sConfig.GetIntDefault("RandomBot.Refresh", 60000);
    m_confAllowSaving = sConfig.GetBoolDefault("PlayerBot.AllowSaving", false);
    m_confDebug = sConfig.GetBoolDefault("PlayerBot.Debug", false);
    m_confUpdateDiff = sConfig.GetIntDefault("PlayerBot.UpdateMs", 10000);

    sPopulationMgr.LoadConfig();    // PlayerBots/Population/
    sCompanionMgr.LoadConfig();     // PlayerBots/Companions/

    if (!sWorld.getConfig(CONFIG_BOOL_FORCE_LOGOUT_DELAY))
        m_tempBots.clear();
}

bool PlayerBotMgr::IsSavingAllowed(uint32 playerGuid) const
{
    // (a test scenario's temporary companion is never saved: CompanionMgr::MarkTemporary)
    if (playerGuid && sCompanionMgr.IsTemporary(playerGuid))
        return false;
    return m_confAllowSaving || (playerGuid && sCompanionMgr.IsCompanion(playerGuid));
}

void PlayerBotMgr::Load()
{
    // 1- Clean
    DeleteAll();
    m_bots.clear();
    m_tempBots.clear();
    m_totalChance = 0;

    // 2- Configuration
    LoadConfig();
    // Mirror quests of class quests (companions' quests in the player's log), before anyone logs in.
    static bool mirrorsBuilt = false;
    if (!mirrorsBuilt)
    {
        mirrorsBuilt = true;
        BotShared::ClassQuestChains::Load();
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> [QuestMirror] %u mirror quests made.", BotShared::QuestMirror::BuildAll());
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> [QuestMirror] %u Artisan trip quests made.", BotShared::ArtisanTrainers::BuildQuests());
        BotShared::QuestPlaces::Load();     // where to go for them (the leader's map flag)
    }
    sCompanionMgr.Load();

    // 3- Load usable account ID
    std::unique_ptr<QueryResult> result = LoginDatabase.PQuery(
                              "SELECT MAX(`id`)"
                              " FROM `account`");
    if (!result)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Playerbot: unable to load max account id.");
        return;
    }
    Field* fields = result->Fetch();
    m_maxAccountId = fields[0].GetUInt32() + 10000;

    // 4- LoadFromDB
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> [PlayerBotMgr] Loading Bots ...");
    result = CharacterDatabase.PQuery(
                 "SELECT char_guid, chance, ai"
                 " FROM playerbot");
    if (!result)
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Table `playerbot` is empty.");
    else
    {
        do
        {
            fields = result->Fetch();
            uint32 guid = fields[0].GetUInt32();
            uint32 acc = GenBotAccountId();
            uint32 chance = fields[1].GetUInt32();

            std::shared_ptr<PlayerBotEntry> entry = std::make_shared<PlayerBotEntry>(guid, acc, chance);
            entry->ai.reset(CreatePlayerBotAI(fields[2].GetCppString()));
            entry->ai->botEntry = entry.get();
            if (!sObjectMgr.GetPlayerNameByGUID(guid, entry->name))
                entry->name = "<Unknown>";
            entry->ai->OnBotEntryLoad(entry.get());
            m_bots.insert({ entry->playerGUID, entry });
            m_totalChance += chance;
        }
        while (result->NextRow());
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "%u bots loaded", m_bots.size());
    }

    // 5- Check config/DB
    if (m_confMinRandomBots >= m_bots.size() && !m_bots.empty())
        m_confMinRandomBots = m_bots.size() - 1;
    if (m_confMaxRandomBots > m_bots.size())
        m_confMaxRandomBots = m_bots.size();
    if (m_confMaxRandomBots <= m_confMinRandomBots)
        m_confMaxRandomBots = m_confMinRandomBots + 1;

    // 6- Start initial bots
    if (m_confEnableRandomBots)
    {
        for (uint32 i = 0; i < m_confMinRandomBots; i++)
            AddRandomBot();
    }

    // 7- Fill stats info
    m_stats.confMaxOnline = m_confMaxRandomBots;
    m_stats.confMinOnline = m_confMinRandomBots;
    m_stats.totalBots = m_bots.size();
    m_stats.confRandomBotsRefresh = m_confRandomBotsRefresh;
    m_stats.confUpdateDiff = m_confUpdateDiff;

    // 8- Show stats if debug
    if (m_confDebug)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PlayerBotMgr] Between %u and %u bots online", m_confMinRandomBots, m_confMaxRandomBots);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PlayerBotMgr] %u now loading", m_stats.loadingCount);
    }
}

void PlayerBotMgr::DeleteAll()
{
    m_stats.onlineCount = 0;
    m_stats.loadingCount = 0;

    for (auto i = m_bots.begin(); i != m_bots.end(); i++)
    {
        if (i->second->state != PB_STATE_OFFLINE)
        {
            OnBotLogout(i->second.get());
            m_totalChance += i->second->chance;
        }
    }
    m_tempBots.clear();

    if (m_confDebug)
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PlayerBotMgr] Deleting all bots [OK]");
}

void PlayerBotMgr::OnBotLogin(PlayerBotEntry *e)
{
    e->state = PB_STATE_ONLINE;
    if (m_confDebug)
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PlayerBot][Login]  '%s' GUID:%u Acc:%u", e->name.c_str(), e->playerGUID, e->accountId);
}
void PlayerBotMgr::OnBotLogout(PlayerBotEntry *e)
{
    e->state = PB_STATE_OFFLINE;
    if (m_confDebug)
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PlayerBot][Logout] '%s' GUID:%u Acc:%u", e->name.c_str(), e->playerGUID, e->accountId);
}

void PlayerBotMgr::OnPlayerInWorld(Player* player)
{
    if (PlayerBotEntry* e = player->GetSession()->GetBot())
    {
        player->SetAI(e->ai.get());
        e->ai->SetPlayer(player);
        e->ai->OnPlayerLogin();
    }
}

void PlayerBotMgr::Update(uint32 diff)
{
    // Population bots (PlayerBots/Population/): spawn queue, lifetimes, despawn requests.
    sPopulationMgr.Update(diff);
    // Recruited companions: logins as party bots after their temporary bot left.
    sCompanionMgr.Update(diff);

    // Temporary bots.
    std::map<uint32, uint32>::iterator it;
    for (it = m_tempBots.begin(); it != m_tempBots.end(); ++it)
    {
        if (it->second < diff)
            it->second = 0;
        else
            it->second -= diff;
    }
    it = m_tempBots.begin();
    while (it != m_tempBots.end())
    {
        if (!it->second)
        {
            // Update of "chatBot" too.
            for (auto iter = m_bots.begin(); iter != m_bots.end(); ++iter)
                if (iter->second->accountId == it->first)
                {
                    iter->second->state = PB_STATE_OFFLINE; // Will get logged out at next WorldSession::Update call
                    m_bots.erase(iter);
                    break;
                }
            m_tempBots.erase(it);
            it = m_tempBots.begin();
        }
        else
            ++it;
    }

    m_elapsedTime += diff;
    if (!((m_elapsedTime - m_lastUpdate) > m_confUpdateDiff))
        return; // No need to update
    m_lastUpdate = m_elapsedTime;

    for (auto iter = m_bots.begin(); iter != m_bots.end();)
    {
        if (!m_confEnableRandomBots && !iter->second->customBot)
        {
            ++iter;
            continue;
        }

        if (iter->second->state == PB_STATE_ONLINE)
        {
            if (iter->second->requestRemoval)
            {
                if (iter->second->ai && iter->second->ai->me)
                {
                    if (!iter->second->ai->me->IsAlive())
                    {
                        // don't leave permanent corpse
                        iter->second->ai->me->ResurrectPlayer(1.0f);
                        iter->second->ai->me->SpawnCorpseBones();
                    }
                    // A companion leaving with its leader stays in his group as an offline
                    // member and comes back with him (CompanionMgr::OnLoginRequest).
                    PartyBotAI* partyAI = dynamic_cast<PartyBotAI*>(iter->second->ai.get());
                    if (!partyAI || !partyAI->OnLeaving())
                        iter->second->ai->me->RemoveFromGroup();
                }

                DeleteBot(iter);

                // A client that logged into the same account has already replaced (and deleted)
                // the bot session: FindSession returns the client's session then, leave it alone.
                if (WorldSession* sess = sWorld.FindSession(iter->second->accountId))
                    if (sess->GetBot() == iter->second.get())
                        sess->LogoutPlayer(IsSavingAllowed(uint32(iter->first)));

                // A test scenario's companion mark and requests go with it: the guid of a
                // temporary bot is given out again.
                if (sCompanionMgr.ForgetTemporary(uint32(iter->first)))
                    sRequestBoard.Forget(ObjectGuid(HIGHGUID_PLAYER, uint32(iter->first)));

                iter->second->requestRemoval = false;

                if (iter->second->customBot)
                    iter = m_bots.erase(iter);
                else
                    ++iter;
                continue;
            }
        }

        // Connection of pending bots
        if (iter->second->state != PB_STATE_LOADING)
        {
            ++iter;
            continue;
        }

        WorldSession* sess = sWorld.FindSession(iter->second->accountId);

        if (!sess)
        {
            // This may happen : just wait for the World to add the session.
            ++iter;
            continue;
        }

        if (sess->GetBot() != iter->second.get())
        {
            // A client logged into the same account before the bot was loaded and replaced its session.
            sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[PlayerBotMgr] Session of account %u was taken over by a client, bot %u not loaded.",
                iter->second->accountId, uint32(iter->first));
            DeleteBot(iter);
            if (iter->second->customBot)
                iter = m_bots.erase(iter);
            else
                ++iter;
            continue;
        }

        if (iter->second->ai->OnSessionLoaded(iter->second.get(), sess))
        {
            OnBotLogin(iter->second.get());
            m_stats.loadingCount--;

            if (iter->second->isChatBot)
                m_stats.onlineChat++;
            else
                m_stats.onlineCount++;
        }
        else
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "PLAYERBOT: Unable to load session id %u", iter->second->accountId);
            DeleteBot(iter);

            if (iter->second->customBot)
                iter = m_bots.erase(iter);
            else
                ++iter;
            continue;
        }

        ++iter;
    }

    if (m_confEnableRandomBots)
    {
        uint32 updatesCount = (m_elapsedTime - m_lastBotsRefresh) / m_confRandomBotsRefresh;
        for (uint32 i = 0; i < updatesCount; ++i)
        {
            AddOrRemoveBot();
            m_lastBotsRefresh += m_confRandomBotsRefresh;
        }
    }
}

/*
Toutes les X minutes, ajoute ou enleve un bot.
*/
bool PlayerBotMgr::AddOrRemoveBot()
{
    uint32 alea = urand(m_confMinRandomBots, m_confMaxRandomBots);
    /*
    10 --- --- --- --- --- --- --- --- --- --- 20 bots
                NumActuel
    [alea ici : remove    ][    ici, add    ]
    */
    if (alea > m_stats.onlineCount)
        return AddRandomBot();
    else
        return DeleteRandomBot();

}

bool PlayerBotMgr::AddBot(PlayerBotAI* ai, uint32 reuseGuid)
{
    // The previous bot with this guid has not left yet.
    if (reuseGuid && m_bots.find(reuseGuid) != m_bots.end())
    {
        delete ai;
        return false;
    }

    // Find a correct accountid ?
    std::shared_ptr<PlayerBotEntry> e = std::make_shared<PlayerBotEntry>();
    e->ai.reset(ai);
    e->accountId = GenBotAccountId();
    e->playerGUID = reuseGuid ? reuseGuid : sObjectMgr.GeneratePlayerLowGuid();
    e->customBot = true;
    ai->botEntry = e.get();
    m_bots.insert({ e->playerGUID, e });
    return AddBot(e->playerGUID, false);
}

bool PlayerBotMgr::AddBot(uint32 playerGUID, bool chatBot, PlayerBotAI* pAI, bool fakeAccount)
{
    uint32 accountId = 0;
    auto iter = m_bots.find(playerGUID);
    if (iter == m_bots.end())
    {
        accountId = sObjectMgr.GetPlayerAccountIdByGUID(playerGUID);
        // Sessions are stored per account id. A generated id lets several characters
        // of the same account be online as bots. Player::SaveToDB keeps the real account.
        if (accountId && fakeAccount)
            accountId = GenBotAccountId();
    }
    else
        accountId = iter->second->accountId;

    if (!accountId)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "Player account %u not found...", playerGUID);
        return false;
    }

    if (sWorld.FindSession(accountId))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[PlayerBotMgr] Account %u is already online!", accountId);
        return false;
    }

    std::shared_ptr<PlayerBotEntry> e;
    if (iter != m_bots.end())
    {
        e = iter->second;

        if (pAI) // new AI
        {
            e->ai.reset(pAI);
            e->customBot = true;
        }
    }
    else
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[PlayerBotMgr] Adding temporary PlayerBot with GUID %u.", playerGUID);
        e = std::make_shared<PlayerBotEntry>();
        e->state        = PB_STATE_LOADING;
        e->playerGUID   = playerGUID;
        e->chance       = 10;
        e->accountId    = accountId;
        e->isChatBot    = chatBot;
        if (pAI)
        {
            e->ai.reset(pAI);
            e->customBot = true;
        }
        else
        {
            e->ai.reset(new PlayerBotAI(nullptr));
            e->customBot = false;
        }
        m_bots.insert({ playerGUID , e });
    }

    e->ai->botEntry = e.get();
    e->state = PB_STATE_LOADING;
    WorldSession* session = new WorldSession(accountId, nullptr, sAccountMgr.GetSecurity(accountId), 0, LOCALE_enUS);
    session->SetBot(e);
    sWorld.AddSession(session);
    m_stats.loadingCount++;
    if (chatBot)
        AddTempBot(accountId, 20000);

    return true;
}

bool PlayerBotMgr::AddRandomBot()
{
    uint32 alea = urand(0, m_totalChance);
    bool done = false;
    for (auto it = m_bots.begin(); it != m_bots.end() && !done; it++)
    {
        if (it->second->state != PB_STATE_OFFLINE)
            continue;
        if (it->second->customBot)
            continue;
        uint32 chance = it->second->chance;

        if (chance >= alea)
        {
            AddBot(it->first);
            done = true;
        }
        alea -= chance;
    }
    return done;
}

void PlayerBotMgr::AddTempBot(uint32 account, uint32 time)
{
    m_tempBots[account] = time;
}

void PlayerBotMgr::RefreshTempBot(uint32 account)
{
    if (m_tempBots.find(account) != m_tempBots.end())
    {
        uint32& delay = m_tempBots[account];
        if (delay < 1000)
            delay = 1000;
    }
}

bool PlayerBotMgr::DeleteBot(uint32 playerGUID)
{
    auto iter = m_bots.find(playerGUID);
    if (iter == m_bots.end())
        return false;

    return DeleteBot(iter);
}

bool PlayerBotMgr::DeleteBot(std::map<uint64, std::shared_ptr<PlayerBotEntry>>::iterator iter)
{
    if (iter->second->state == PB_STATE_LOADING)
        m_stats.loadingCount--;
    else if (iter->second->state == PB_STATE_ONLINE)
        m_stats.onlineCount--;

    OnBotLogout(iter->second.get());
    return true;
}

bool PlayerBotMgr::DeleteRandomBot()
{
    if (m_stats.onlineCount < 1)
        return false;
    uint32 idDelete = urand(0, m_stats.onlineCount);
    uint32 onlinePassed = 0;
    std::map<uint32, PlayerBotEntry*>::iterator iter;
    for (auto iter = m_bots.begin(); iter != m_bots.end(); iter++)
    {
        if (!iter->second->customBot && !iter->second->isChatBot && iter->second->state == PB_STATE_ONLINE)
        {
            onlinePassed++;
            if (onlinePassed == idDelete)
            {
                OnBotLogout(iter->second.get());
                m_stats.onlineCount--;
                return true;
            }
        }
    }
    return false;
}

uint8 SelectRandomRaceForClass(uint8 playerClass, Team playerTeam)
{
    std::vector<uint32> validRaces;
    for (uint32 raceId = 1; raceId < MAX_RACES; ++raceId)
    {
        if (playerTeam == ALLIANCE)
        {
            if (!((1 << (raceId - 1)) & RACEMASK_ALLIANCE))
                continue;
        }
        else if (playerTeam == HORDE)
        {
            if (!((1 << (raceId - 1)) & RACEMASK_HORDE))
                continue;
        }

        if (sObjectMgr.GetPlayerInfo(raceId, playerClass))
            validRaces.push_back(raceId);
    }

    if (validRaces.empty())
        return 0;

    return SelectRandomContainerElement(validRaces);
}

bool PlayerBotMgr::ForceAccountConnection(WorldSession* sess)
{
    if (sess->GetBot())
        return sess->GetBot()->state != PB_STATE_OFFLINE;

    // Temporary bots.
    return m_tempBots.find(sess->GetAccountId()) != m_tempBots.end();
}

bool PlayerBotMgr::IsPermanentBot(uint32 playerGUID)
{
    auto iter = m_bots.find(playerGUID);
    return iter != m_bots.end();
}

bool PlayerBotMgr::IsChatBot(uint32 playerGuid)
{
    auto iter = m_bots.find(playerGuid);
    return iter != m_bots.end() && iter->second->isChatBot;
}

void PlayerBotMgr::AddAllBots()
{
    for (auto it = m_bots.begin(); it != m_bots.end(); it++)
    {
        if (!it->second->isChatBot && it->second->state == PB_STATE_OFFLINE)
            AddBot(it->first);
    }
}

bool ChatHandler::HandleBotReloadCommand(char * args)
{
    sPlayerBotMgr.Load();
    SendSysMessage("PlayerBot system reloaded");
    return true;
}

bool ChatHandler::HandleBotAddRandomCommand(char * args)
{
    uint32 count = 1;
    char* sCount = strtok((char*)args, " ");
    if (sCount)
        count = uint32(atoi(sCount));
    for (uint32 i = 0; i < count; ++i)
        sPlayerBotMgr.AddRandomBot();
    PSendSysMessage("%u bots added", count);
    return true;
}

bool ChatHandler::HandleBotStopCommand(char * args)
{
    sPlayerBotMgr.DeleteAll();
    SendSysMessage("All the bots have been unloaded.");
    return true;
}

bool ChatHandler::HandleBotAddAllCommand(char * args)
{
    sPlayerBotMgr.AddAllBots();
    SendSysMessage("All bots have been loaded.");
    return true;
}

bool ChatHandler::HandleBotAddCommand(char* args)
{
    uint32 guid = 0;
    char *charname = nullptr;
    if (*args)
    {
        charname = strtok((char*)args, " ");
        if (charname && strcmp(charname, "") == 0)
            return false;

        guid = sObjectMgr.GetPlayerGuidByName(charname).GetCounter();
        if (!guid)
        {
            PSendSysMessage("Player not found : '%s'", charname);
            SetSentErrorMessage(true);
            return false;
        }
    }
    if (!guid || !sPlayerBotMgr.AddBot(guid))
    {
        SendSysMessage("[PlayerBotMgr] Unable to load bot.");
        return true;
    }
    PSendSysMessage("[PlayerBotMgr] Bot added : '%s', GUID=%u", charname ? charname : "NULL", guid);
    return true;
}

bool ChatHandler::HandleBotDeleteCommand(char * args)
{
    char *charname = strtok((char*)args, " ");

    if (!charname || strcmp(charname, "") == 0)
    {
        SendSysMessage("Syntax : $playerName");
        SetSentErrorMessage(true);
        return false;
    }
    uint32 lowGuid = sObjectMgr.GetPlayerGuidByName(charname).GetCounter();
    if (!lowGuid)
    {
        PSendSysMessage("Unable to find player: '%s'", charname);
        SetSentErrorMessage(true);
        return false;
    }
    if (sPlayerBotMgr.DeleteBot(lowGuid))
    {
        PSendSysMessage("Bot %s (GUID:%u) disconnected.", charname, lowGuid);
        return true;
    }
    else
    {
        PSendSysMessage("Bot %s (GUID:%u) : unable to disconnect.", charname, lowGuid);
        SetSentErrorMessage(true);
        return false;
    }
}

bool ChatHandler::HandleBotInfoCommand(char * args)
{
    uint32 online = sWorld.GetActiveSessionCount();

    PlayerBotStats stats = sPlayerBotMgr.GetStats();
    SendSysMessage("-- PlayerBot stats --");
    PSendSysMessage("Min:%u Max:%u Total:%u", stats.confMinOnline, stats.confMaxOnline, stats.totalBots);
    PSendSysMessage("Loading : %u, Online : %u, Chat : %u", stats.loadingCount, stats.onlineCount, stats.onlineChat);
    PSendSysMessage("%up + %ub = %u",
                    (online - stats.onlineCount), stats.onlineCount, online);
    return true;
}

bool ChatHandler::HandleBotStartCommand(char * args)
{
    sPlayerBotMgr.Start();
    return true;
}

bool ChatHandler::PartyBotAddRequirementCheck(Player const* pPlayer, Player const* pTarget)
{
    if (pPlayer->IsTaxiFlying())
    {
        SendSysMessage("Cannot add bots while flying.");
        return false;
    }

    // Spawning bots inside BG will cause server crash on BG end.
    if (pPlayer->InBattleGround())
    {
        SendSysMessage("Cannot add bots inside battlegrounds.");
        return false;
    }

    if (pPlayer->GetGroup() && (pPlayer->GetGroup()->IsFull() || sWorld.getConfig(CONFIG_UINT32_PARTY_BOT_MAX_BOTS) &&
        (pPlayer->GetGroup()->GetMembersCount() - 1 >= sWorld.getConfig(CONFIG_UINT32_PARTY_BOT_MAX_BOTS))))
    {
        SendSysMessage("Cannot add more bots. Group is full.");
        return false;
    }

    if (Map const* pMap = pPlayer->GetMap())
    {
        if (pMap->IsDungeon() &&
            pMap->GetPlayers().getSize() >= pMap->GetMapEntry()->maxPlayers)
        {
            SendSysMessage("Cannot add more bots. Instance is full.");
            return false;
        }
    }

    if (pTarget && pTarget->GetTeam() != pPlayer->GetTeam())
    {
        SendSysMessage("Cannot clone enemy faction characters.");
        return false;
    }

    // Restrictions when the command is made public to avoid abuse.
    if (GetSession()->GetSecurity() <= SEC_PLAYER && !sWorld.getConfig(CONFIG_BOOL_PARTY_BOT_SKIP_CHECKS))
    {
        if (pPlayer->IsDead())
        {
            SendSysMessage("Cannot add bots while dead.");
            return false;
        }

        if (pPlayer->IsInCombat())
        {
            SendSysMessage("Cannot add bots while in combat.");
            return false;
        }

        if (pPlayer->GetMap()->IsDungeon())
        {
            SendSysMessage("Cannot add bots while inside instances.");
            return false;
        }

        // Clone command.
        if (pTarget)
        {
            if (pTarget->IsDead())
            {
                SendSysMessage("Cannot clone dead characters.");
                return false;
            }

            if (pTarget->IsInCombat())
            {
                SendSysMessage("Cannot clone characters that are in combat.");
                return false;
            }

            if (pTarget->GetLevel() > pPlayer->GetLevel() + 10)
            {
                SendSysMessage("Cannot clone higher level characters.");
                return false;
            }
        }
    }

    return true;
}

bool ChatHandler::HandlePartyBotAddCommand(char* args)
{
    Player* pPlayer = m_session->GetPlayer();
    if (!pPlayer)
        return false;

    if (!PartyBotAddRequirementCheck(pPlayer, nullptr))
    {
        SetSentErrorMessage(true);
        return false;
    }

    if (!args)
    {
        SendSysMessage("Incorrect syntax. Expected role or class.");
        SetSentErrorMessage(true);
        return false;
    }

    uint8 botClass = 0;
    uint32 botLevel = pPlayer->GetLevel();
    CombatBotRoles botRole = ROLE_INVALID;

    if (char* arg1 = ExtractArg(&args))
    {
        std::string option = arg1;
        if (option == "warrior")
            botClass = CLASS_WARRIOR;
        else if (option == "paladin" && pPlayer->GetTeam() == ALLIANCE)
            botClass = CLASS_PALADIN;
        else if (option == "hunter")
            botClass = CLASS_HUNTER;
        else if (option == "rogue")
            botClass = CLASS_ROGUE;
        else if (option == "priest")
            botClass = CLASS_PRIEST;
        else if (option == "shaman" && pPlayer->GetTeam() == HORDE)
            botClass = CLASS_SHAMAN;
        else if (option == "mage")
            botClass = CLASS_MAGE;
        else if (option == "warlock")
            botClass = CLASS_WARLOCK;
        else if (option == "druid")
            botClass = CLASS_DRUID;
        else if (option == "dps")
        {
            botClass = PickRandomValue(CLASS_WARRIOR, CLASS_HUNTER, CLASS_ROGUE, CLASS_MAGE, CLASS_WARLOCK);
            botRole = CombatBotBaseAI::IsMeleeDamageClass(botClass) ? ROLE_MELEE_DPS : ROLE_RANGE_DPS;
        }
        else if (option == "healer")
        {
            std::vector<uint32> dpsClasses = { CLASS_PRIEST, CLASS_DRUID };
            if (pPlayer->GetTeam() == HORDE)
                dpsClasses.push_back(CLASS_SHAMAN);
            else
                dpsClasses.push_back(CLASS_PALADIN);
            botClass = SelectRandomContainerElement(dpsClasses);
            botRole = ROLE_HEALER;
        }
        else if (option == "tank")
        {
            botClass = CLASS_WARRIOR;
            botRole = ROLE_TANK;
        }

        // Prevent setting a custom level for bots unless the account is a GM or skipping checks is enabled.
        if (GetSession()->GetSecurity() > SEC_PLAYER || sWorld.getConfig(CONFIG_BOOL_PARTY_BOT_SKIP_CHECKS))
            ExtractUInt32(&args, botLevel);
    }

    if (!botClass)
    {
        SendSysMessage("Incorrect syntax. Expected role or class.");
        SetSentErrorMessage(true);
        return false;
    }

    uint8 botRace = SelectRandomRaceForClass(botClass, pPlayer->GetTeam());
    if (!botRace)
    {
        SendSysMessage("Unable to select race for bot.");
        SetSentErrorMessage(true);
        return false;
    }

    float x, y, z;
    pPlayer->GetNearPoint(pPlayer, x, y, z, 0, 5.0f, frand(0.0f, 6.0f));

    PartyBotAI* ai = new PartyBotAI(pPlayer, nullptr, botRole, botRace, botClass, botLevel, pPlayer->GetMapId(), pPlayer->GetMap()->GetInstanceId(), x, y, z, pPlayer->GetOrientation());
    if (sPlayerBotMgr.AddBot(ai))
        SendSysMessage("New party bot added.");
    else
    {
        delete ai;
        SendSysMessage("Error spawning bot.");
        SetSentErrorMessage(true);
        return false;
    }

    return true;
}

bool ChatHandler::HandlePartyBotCloneCommand(char* args)
{
    Player* pPlayer = m_session->GetPlayer();
    if (!pPlayer)
        return false;

    Player* pTarget = GetSelectedPlayer();
    if (!pTarget)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    if (!PartyBotAddRequirementCheck(pPlayer, pTarget))
    {
        SetSentErrorMessage(true);
        return false;
    }

    uint8 botRace = pTarget->GetRace();
    uint8 botClass = pTarget->GetClass();

    float x, y, z;
    pPlayer->GetNearPoint(pPlayer, x, y, z, 0, 5.0f, frand(0.0f, 6.0f));

    PartyBotAI* ai = new PartyBotAI(pPlayer, pTarget, ROLE_INVALID, botRace, botClass, pPlayer->GetLevel(), pPlayer->GetMapId(), pPlayer->GetMap()->GetInstanceId(), x, y, z, pPlayer->GetOrientation());
    if (sPlayerBotMgr.AddBot(ai))
        SendSysMessage("New party bot added.");
    else
    {
        delete ai;
        SendSysMessage("Error spawning bot.");
        SetSentErrorMessage(true);
        return false;
    }

    return true;
}

// Role keywords used by .partybot load / setrole and the PBCHR reply.
// "auto" -> ROLE_INVALID with isAuto = true (AutoAssignRole picks one from class and talents).
static bool ParsePartyBotRole(std::string const& str, uint8 classId, CombatBotRoles& role)
{
    role = ROLE_INVALID;
    if (str == "auto")
        return true;
    if (str == "tank")
        role = ROLE_TANK;
    else if (str == "healer")
        role = ROLE_HEALER;
    else if (str == "meleedps")
        role = ROLE_MELEE_DPS;
    else if (str == "rangedps")
        role = ROLE_RANGE_DPS;
    else if (str == "dps")
        role = CombatBotBaseAI::IsMeleeDamageClass(classId) ? ROLE_MELEE_DPS : ROLE_RANGE_DPS;
    return role != ROLE_INVALID;
}

static char const* PartyBotRoleName(CombatBotRoles role)
{
    switch (role)
    {
        case ROLE_TANK:      return "tank";
        case ROLE_HEALER:    return "healer";
        case ROLE_MELEE_DPS: return "meleedps";
        case ROLE_RANGE_DPS: return "rangedps";
        default:             return "auto";
    }
}

// .partybot load <name> [auto|tank|healer|dps|meleedps|rangedps]
bool ChatHandler::HandlePartyBotLoadCommand(char* args)
{
    Player* pPlayer = m_session->GetPlayer();
    if (!pPlayer)
        return false;

    std::string name = ExtractPlayerNameFromLink(&args);
    if (name.empty())
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    ObjectGuid guid = sObjectMgr.GetPlayerGuidByName(name).GetCounter();
    if (!guid)
    {
        SendSysMessage(LANG_PLAYER_NOT_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    CombatBotRoles role = ROLE_INVALID;
    if (char* roleArg = ExtractArg(&args))
    {
        if (!ParsePartyBotRole(roleArg, sObjectMgr.GetPlayerClassByGUID(guid), role))
        {
            SendSysMessage("Unknown role. Expected auto, tank, healer, dps, meleedps or rangedps.");
            SetSentErrorMessage(true);
            return false;
        }
    }

    if (sObjectAccessor.FindPlayerNotInWorld(guid))
    {
        SendSysMessage("Player is already online!");
        SetSentErrorMessage(true);
        return false;
    }

    // A companion is somewhere in the world: it logs in there and comes (CompanionMgr::CallOver).
    if (sCompanionMgr.IsCompanion(guid.GetCounter()))
    {
        std::string const error = sCompanionMgr.CallOver(pPlayer, guid.GetCounter(), uint8(role), false);
        if (!error.empty())
        {
            PSendSysMessage("Companion %s cannot come: %s.", name.c_str(), error.c_str());
            SetSentErrorMessage(true);
            return false;
        }
        PSendSysMessage("Loading %s as party bot (it comes from where it is).", name.c_str());
        return true;
    }

    float x, y, z;
    pPlayer->GetNearPoint(pPlayer, x, y, z, 0, 5.0f, frand(0.0f, 6.0f));

    PartyBotAI* pAI = new PartyBotAI(pPlayer, pPlayer->GetMapId(), pPlayer->GetMap()->GetInstanceId(), x, y, z, pPlayer->GetOrientation());
    pAI->m_role = role; // ROLE_INVALID -> auto assigned in the first UpdateAI
    pAI->m_roleLocked = role != ROLE_INVALID;

    if (!sPlayerBotMgr.AddBot(guid, false, pAI, true))
    {
        delete pAI;
        SendSysMessage("Error spawning bot.");
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage("Loading %s as party bot.", name.c_str());
    return true;
}

// .partybot setrole <auto|tank|healer|dps|meleedps|rangedps> [name]
// Without a name: uses the selected bot and replies in plain text.
// With a name (addon): replies PBOK;<name>;role;<role> or PBERR.
bool ChatHandler::HandlePartyBotSetRoleCommand(char* args)
{
    char* roleArg = ExtractArg(&args);
    if (!roleArg)
        return false;
    std::string roleStr = roleArg;

    bool const byName = args && *args;
    Player* pTarget = nullptr;
    std::string name;

    // Your own character (the addon's role button on "me"): the Commander's party assignment
    // and tank checks use it instead of guessing from talents. "auto" removes the choice.
    Player* pUser = m_session ? m_session->GetPlayer() : nullptr;
    if (pUser && byName)
    {
        std::string self = args;
        while (!self.empty() && self.back() == ' ')
            self.pop_back();
        if (normalizePlayerName(self) && self == pUser->GetName())
        {
            CombatBotRoles role = ROLE_INVALID;
            if (!ParsePartyBotRole(roleStr, pUser->GetClass(), role))
            {
                PSendSysMessage("PBERR;%s;usage", self.c_str());
                SetSentErrorMessage(true);
                return false;
            }
            sCommanderMgr.SetPlayerRole(pUser->GetObjectGuid(), role);
            CombatBotRoles const actual = role != ROLE_INVALID ? role : RoleCapabilities::Assess(pUser).Best();
            PSendSysMessage("PBOK;%s;role;%s;%s", self.c_str(), roleStr.c_str(), PartyBotRoleName(actual));
            return true;
        }
    }

    if (byName)
    {
        pTarget = ExtractPartyBotTarget(&args, name);
        if (!pTarget)
            return false;
    }
    else
    {
        pTarget = GetSelectedPlayer();
        if (!pTarget)
        {
            SendSysMessage(LANG_NO_CHAR_SELECTED);
            SetSentErrorMessage(true);
            return false;
        }
    }

    CombatBotRoles role = ROLE_INVALID;
    if (!ParsePartyBotRole(roleStr, pTarget->GetClass(), role))
    {
        if (byName)
        {
            PSendSysMessage("PBERR;%s;usage", name.c_str());
            SetSentErrorMessage(true);
        }
        return false;
    }

    if (pTarget->AI())
    {
        if (PartyBotAI* pAI = dynamic_cast<PartyBotAI*>(pTarget->AI()))
        {
            pAI->SetRole(role);     // auto: unlocked, the Commander party assignment may change it
            pAI->ResetSpellData();
            pAI->PopulateSpellData();
            pAI->m_resetSpellData = false;
            if (byName)
                PSendSysMessage("PBOK;%s;role;%s;%s", name.c_str(), roleStr.c_str(), PartyBotRoleName(pAI->m_role));
            else
                PSendSysMessage("%s is now a %s.", pTarget->GetName(), PartyBotRoleName(pAI->m_role));
            return true;
        }
    }

    SendSysMessage("Target is not a party bot.");
    SetSentErrorMessage(true);
    return false;
}

// .partybot healdps <0-3|auto> [name]: the healer damage tier of a party bot (Commander
// Coordination/Doctrine/HealerDamage.h): 0 none, 1 free attacks, 2 spells while the mana is
// high, 3 a hybrid; auto = the Commander's choice. The selected bot without a name.
bool ChatHandler::HandlePartyBotHealDpsCommand(char* args)
{
    char* tierArg = ExtractArg(&args);
    if (!tierArg)
    {
        SendSysMessage("Usage: .partybot healdps <0-3|auto> [name]");
        SetSentErrorMessage(true);
        return false;
    }
    std::string const tierStr = tierArg;
    int8 tier = -1;
    if (tierStr != "auto")
    {
        if (tierStr.size() != 1 || tierStr[0] < '0' || tierStr[0] >= char('0' + Commander::HEALER_DAMAGE_TIERS))
        {
            SendSysMessage("Usage: .partybot healdps <0-3|auto> [name]");
            SetSentErrorMessage(true);
            return false;
        }
        tier = int8(tierStr[0] - '0');
    }

    bool const byName = args && *args;
    std::string name;
    Player* pTarget = ExtractPartyBotTarget(&args, name);
    if (!pTarget)
        return false;
    PartyBotAI* pAI = dynamic_cast<PartyBotAI*>(pTarget->AI());
    if (!pAI)
        return false;

    pAI->m_healerDamageTier = tier;
    if (byName)
        PSendSysMessage("PBOK;%s;healdps;%s", name.c_str(), tierStr.c_str());
    else if (tier < 0)
        PSendSysMessage("%s: healer damage tier automatic.", pTarget->GetName());
    else
        PSendSysMessage("%s: healer damage tier %d (%s).", pTarget->GetName(), int32(tier),
            Commander::HealerDamageAssigner::Name(uint8(tier)));
    return true;
}

bool ChatHandler::HandlePartyBotAttackStartCommand(char* args)
{
    Player* pPlayer = GetSession()->GetPlayer();
    Unit* pTarget = GetSelectedUnit();
    if (!pTarget || (pTarget == pPlayer))
    {
        SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    Group* pGroup = pPlayer->GetGroup();
    if (!pGroup)
    {
        SendSysMessage("You are not in a group.");
        SetSentErrorMessage(true);
        return false;
    }

    for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        if (Player* pMember = itr->getSource())
        {
            if (pMember == pPlayer)
                continue;

            if (pMember->AI())
            {
                if (PartyBotAI* pAI = dynamic_cast<PartyBotAI*>(pMember->AI()))
                {
                    if (pMember->IsValidAttackTarget(pTarget))
                    {
                        pAI->ClearMovementHold();   // a leftover Commander hold would block the chase
                        pAI->AttackStart(pTarget);
                    }
                }
            }
        }
    }
    // Kept by the Commander's target assignment until it dies or another order comes
    // (TeamPlan::orders); the AttackStart above only saves waiting for the next refresh.
    sCommanderMgr.OrderAttack(pPlayer, pTarget->GetObjectGuid(), false);

    PSendSysMessage("All party bots are now attacking %s.", pTarget->GetName());
    return true;
}

void StopPartyBotAttackHelper(PartyBotAI* pAI, Player* pBot)
{
    pAI->ClearMovementHold();
    pBot->AttackStop(true);
    pBot->InterruptNonMeleeSpells(false);
    if (!pBot->IsStopped())
        pBot->StopMoving();
    if (pBot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
        pBot->GetMotionMaster()->Clear();
    if (pAI->m_updateTimer.GetExpiry() < 3000)
        pAI->m_updateTimer.Reset(3000);
}

bool ChatHandler::HandlePartyBotAttackStopCommand(char* args)
{
    Player* pPlayer = GetSession()->GetPlayer();
    Unit* pTarget = GetSelectedUnit();
    if (!pTarget || (pTarget == pPlayer))
    {
        SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    Group* pGroup = pPlayer->GetGroup();
    if (!pGroup)
    {
        SendSysMessage("You are not in a group.");
        SetSentErrorMessage(true);
        return false;
    }

    for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        if (Player* pMember = itr->getSource())
        {
            if (pMember == pPlayer)
                continue;

            if (pMember->AI())
            {
                if (PartyBotAI* pAI = dynamic_cast<PartyBotAI*>(pMember->AI()))
                {
                    if (pMember->GetVictim() == pTarget)
                        StopPartyBotAttackHelper(pAI, pMember);
                }
            }
        }
    }
    // Not attacked again for the rest of the fight (user, 2026-11-07; was: 3 s pause).
    sCommanderMgr.OrderLeaveAlone(pPlayer, pTarget->GetObjectGuid());

    PSendSysMessage("All party bots have stopped attacking %s.", pTarget->GetName());
    return true;
}

bool ChatHandler::HandlePartyBotAoECommand(char* args)
{
    Player* pPlayer = GetSession()->GetPlayer();
    Unit* pTarget = GetSelectedUnit();
    if (!pTarget || !pPlayer->IsValidAttackTarget(pTarget, true))
    {
        SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    Group* pGroup = pPlayer->GetGroup();
    if (!pGroup)
    {
        SendSysMessage("You are not in a group.");
        SetSentErrorMessage(true);
        return false;
    }

    for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        if (Player* pMember = itr->getSource())
        {
            if (pMember == pPlayer)
                continue;

            if (pMember->AI())
            {
                if (PartyBotAI* pAI = dynamic_cast<PartyBotAI*>(pMember->AI()))
                {
                    for (auto const& pSpell : pAI->m_spells.raw.spells)
                    {
                        if (pSpell && pSpell->IsAreaOfEffectSpell() &&
                           !pSpell->IsPositiveSpell() &&
                            pSpell->IsTargetInRange(pMember, pTarget))
                        {
                            if (pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL) &&
                               !pMember->GetCurrentSpell(CURRENT_GENERIC_SPELL)->m_spellInfo->IsAreaOfEffectSpell())
                                pMember->InterruptSpell(CURRENT_GENERIC_SPELL);

                            if (pMember->CastSpell(pTarget, pSpell, false) == SPELL_CAST_OK)
                                break;
                        }
                    }
                }
            }
        }
    }

    PSendSysMessage("All party bots are casting AoE spells at %s.", pTarget->GetName());
    return true;
}

bool ChatHandler::HandlePartyBotStartCastingCommand(char * args)
{
    return HandlePartyBotToggleCastingCommand(true);
}

bool ChatHandler::HandlePartyBotStopCastingCommand(char * args)
{
    return HandlePartyBotToggleCastingCommand(false);
}

bool ChatHandler::HandlePartyBotToggleCastingCommand(bool allowCasting)
{
    Player* pPlayer = GetSession()->GetPlayer();
    Player* pTarget = GetSelectedPlayer();

    if (pTarget && (pTarget != pPlayer))
    {
        if (pTarget->AI())
        {
            if (PartyBotAI* pAI = dynamic_cast<PartyBotAI*>(pTarget->AI()))
            {
                if (allowCasting)
                {
                    pAI->m_preventCasting = false;
                    PSendSysMessage("%s will be allowed to cast spells.", pTarget->GetName());
                }
                else
                {
                    pAI->m_preventCasting = true;
                    pTarget->InterruptNonMeleeSpells(false);
                    PSendSysMessage("%s will no longer cast spells.", pTarget->GetName());
                }
                return true;
            }
        }
        SendSysMessage("Target is not a party bot.");
        SetSentErrorMessage(true);
        return false;
    }

    Group* pGroup = pPlayer->GetGroup();
    if (!pGroup)
    {
        SendSysMessage("You are not in a group.");
        SetSentErrorMessage(true);
        return false;
    }

    for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        if (Player* pMember = itr->getSource())
        {
            if (pMember == pPlayer)
                continue;

            if (pMember->AI())
            {
                if (PartyBotAI* pAI = dynamic_cast<PartyBotAI*>(pMember->AI()))
                {
                    if (allowCasting)
                    {
                        pAI->m_preventCasting = false;
                    }
                    else
                    {
                        pAI->m_preventCasting = true;
                        pTarget->InterruptNonMeleeSpells(false);
                    }
                }
            }
        }
    }

    if (allowCasting)
        SendSysMessage("All bots are now allowed to cast spells again.");
    else
        SendSysMessage("All bots are now forbidden from casting spells.");

    return true;
}

static std::map<std::string, RaidTargetIcon> raidTargetIcons =
{
    { "star",     RAID_TARGET_ICON_STAR     },
    { "circle",   RAID_TARGET_ICON_CIRCLE   },
    { "diamond",  RAID_TARGET_ICON_DIAMOND  },
    { "triangle", RAID_TARGET_ICON_TRIANGLE },
    { "moon",     RAID_TARGET_ICON_MOON     },
    { "square",   RAID_TARGET_ICON_SQUARE   },
    { "cross",    RAID_TARGET_ICON_CROSS    },
    { "skull",    RAID_TARGET_ICON_SKULL    },
};

bool ChatHandler::HandlePartyBotFocusMarkCommand(char* args)
{
    std::string mark = args;
    auto itrMark = raidTargetIcons.find(mark);
    if (itrMark == raidTargetIcons.end())
    {
        SendSysMessage("Unknown target mark. Valid names are: star, circle, diamond, triangle, moon, square, cross, skull");
        SetSentErrorMessage(true);
        return false;
    }

    Player* pPlayer = GetSession()->GetPlayer();
    Player* pTarget = GetSelectedPlayer();

    if (pTarget && (pTarget != pPlayer))
    {
        if (pTarget->AI())
        {
            if (PartyBotAI* pAI = dynamic_cast<PartyBotAI*>(pTarget->AI()))
            {
                if (std::find(pAI->m_marksToFocus.begin(), pAI->m_marksToFocus.end(), itrMark->second) != pAI->m_marksToFocus.end())
                {
                    PSendSysMessage("%s already have focus %s.", pTarget->GetName(), args);
                    return false;
                }

                PSendSysMessage("%s will focus %s.", pTarget->GetName(), args);
                pAI->m_marksToFocus.push_back(itrMark->second);
                return true;
            }
        }
        SendSysMessage("Target is not a party bot.");
        SetSentErrorMessage(true);
        return false;
    }

    Group* pGroup = pPlayer->GetGroup();
    if (!pGroup)
    {
        SendSysMessage("You are not in a group.");
        SetSentErrorMessage(true);
        return false;
    }

    for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        if (Player* pMember = itr->getSource())
        {
            if (pMember == pPlayer)
                continue;

            if (pMember->AI())
            {
                if (PartyBotAI* pAI = dynamic_cast<PartyBotAI*>(pMember->AI()))
                {
                    if (std::find(pAI->m_marksToFocus.begin(), pAI->m_marksToFocus.end(), itrMark->second) != pAI->m_marksToFocus.end())
                    {
                        // Already have focus mark
                        continue;
                    }
                    pAI->m_marksToFocus.push_back(itrMark->second);
                }
            }
        }
    }

    PSendSysMessage("All party bots will focus %s.", args);
    return true;
}

bool ChatHandler::HandlePartyBotClearMarksCommand(char* args)
{
    Player* pPlayer = GetSession()->GetPlayer();
    Player* pTarget = GetSelectedPlayer();

    if (pTarget && (pTarget != pPlayer))
    {
        if (pTarget->AI())
        {
            if (PartyBotAI* pAI = dynamic_cast<PartyBotAI*>(pTarget->AI()))
            {
                PSendSysMessage("All mark assignments cleared for %s.", pTarget->GetName());
                pAI->m_marksToFocus.clear();
                return true;
            }
        }
        SendSysMessage("Target is not a party bot.");
        SetSentErrorMessage(true);
        return false;
    }

    Group* pGroup = pPlayer->GetGroup();
    if (!pGroup)
    {
        SendSysMessage("You are not in a group.");
        SetSentErrorMessage(true);
        return false;
    }

    for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        if (Player* pMember = itr->getSource())
        {
            if (pMember == pPlayer)
                continue;

            if (pMember->AI())
            {
                if (PartyBotAI* pAI = dynamic_cast<PartyBotAI*>(pMember->AI()))
                {
                    pAI->m_marksToFocus.clear();
                }
            }
        }
    }

    SendSysMessage("Mark assignments cleared for all bots.");
    return true;
}

bool HandlePartyBotComeToMeHelper(Player* pBot, Player* pPlayer)
{
    if (pBot->AI() && pBot->IsAlive() && pBot->IsInMap(pPlayer) && !pBot->HasUnitState(UNIT_STATE_NO_FREE_MOVE))
    {
        if (PartyBotAI* pAI = dynamic_cast<PartyBotAI*>(pBot->AI()))
        {
            if (pBot->GetVictim())
                StopPartyBotAttackHelper(pAI, pBot);

            if (pBot->GetStandState() != UNIT_STAND_STATE_STAND)
                pBot->SetStandState(UNIT_STAND_STATE_STAND);

            // Otherwise the hold keeps the bot from following once it has arrived.
            pAI->ClearMovementHold();
            pBot->InterruptSpellsWithInterruptFlags(SPELL_INTERRUPT_FLAG_MOVEMENT);
            pBot->MonsterMove(pPlayer->GetPositionX(), pPlayer->GetPositionY(), pPlayer->GetPositionZ());
            return true;
        }
    }

    return false;
}

bool ChatHandler::HandlePartyBotComeToMeCommand(char* args)
{
    Player* pPlayer = GetSession()->GetPlayer();
    Player* pTarget = GetSelectedPlayer();

    bool ok = false;

    if (pTarget && pTarget != pPlayer)
    {
        if (ok = HandlePartyBotComeToMeHelper(pTarget, pPlayer))
            PSendSysMessage("%s is coming to your position.", pTarget->GetName());
        else
            PSendSysMessage("%s is not a party bot or it cannot move.", pTarget->GetName());
        return ok;
    }
    else if (Group* pGroup = pPlayer->GetGroup())
    {
        bool ok = false;
        for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            if (Player* pMember = itr->getSource())
            {
                if (pMember == pPlayer)
                    continue;

                ok = HandlePartyBotComeToMeHelper(pMember, pPlayer) || ok;
            }
        }

        if (ok)
            SendSysMessage("All party bots are coming to your position.");
        else
            SendSysMessage("There are no party bots in the group or they cannot move.");
        return ok;
    }

    SendSysMessage("You are not in a group.");
    SetSentErrorMessage(true);
    return false;
}

bool HandlePartyBotUseGObjectHelper(Player* pTarget, GameObject* pGo)
{
    if (pTarget->AI())
    {
        if (PartyBotAI* pAI = dynamic_cast<PartyBotAI*>(pTarget->AI()))
        {
            if (pTarget->IsWithinDist(pGo, INTERACTION_DISTANCE))
            {
                pGo->Use(pTarget);
                return true;
            }
        }
    }

    return false;
}

bool ChatHandler::HandlePartyBotUseGObjectCommand(char* args)
{
    Player* pPlayer = GetSession()->GetPlayer();
    Player* pTarget = GetSelectedPlayer();

    GameObject* pGo = getSelectedGameObject();
    if (!pGo)
    {
        SendSysMessage(LANG_COMMAND_NOGAMEOBJECTFOUND);
        return false;
    }

    bool ok = false;

    if (pTarget && pTarget != pPlayer)
    {
        if (ok = HandlePartyBotUseGObjectHelper(pTarget, pGo))
            PSendSysMessage("%s has used the object.", pTarget->GetName());
        else
            PSendSysMessage("%s is not in range or is not a party bot.", pTarget->GetName());
        return ok;
    }
    else if (Group* pGroup = pPlayer->GetGroup())
    {
        for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            if (Player* pMember = itr->getSource())
                ok = HandlePartyBotUseGObjectHelper(pMember, pGo) || ok;
        }

        if (ok)
            SendSysMessage("All party bots in range have used the object.");
        else
            SendSysMessage("There are no party bots in range of the object.");
        return ok;
    }

    SendSysMessage("You are not in a group.");
    SetSentErrorMessage(true);
    return false;
}

bool HandlePartyBotPauseApplyHelper(Player* pTarget, uint32 duration)
{
    if (pTarget->AI())
    {
        if (PartyBotAI* pAI = dynamic_cast<PartyBotAI*>(pTarget->AI()))
        {
            // Pause: the bot stays. Unpause: it follows again right away.
            pAI->ClearMovementHold();
            pAI->m_updateTimer.Reset(duration);

            if (duration)
            {
                pTarget->StopMoving();
                pTarget->GetMotionMaster()->MoveIdle();
            }

            return true;
        }
    }

    return false;
}

bool ChatHandler::HandlePartyBotPauseHelper(char* args, bool pause)
{
    bool all = false;
    uint32 duration = 0;
    if (char* arg1 = ExtractArg(&args))
    {
        if (!(all = (strcmp(arg1, "all") == 0)) && pause)
            duration = atoi(arg1);

        if (char* arg2 = ExtractArg(&args))
        {
            if (!duration && pause)
                duration = atoi(arg2);
            else if (!all)
                all = strcmp(arg2, "all") == 0;
        }
    }

    if (pause && !duration)
        duration = 5 * MINUTE * IN_MILLISECONDS;

    if (all)
    {
        Player* pPlayer = GetSession()->GetPlayer();
        Group* pGroup = pPlayer->GetGroup();
        if (!pGroup)
        {
            SendSysMessage("You are not in a group.");
            SetSentErrorMessage(true);
            return false;
        }

        bool success = false;
        for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            if (Player* pMember = itr->getSource())
            {
                if (pMember == pPlayer)
                    continue;

                if (HandlePartyBotPauseApplyHelper(pMember, duration))
                    success = true;
            }
        }

        if (success)
        {
            if (pause)
                PSendSysMessage("All party bots paused for %u seconds.", (duration / IN_MILLISECONDS));
            else
                SendSysMessage("All party bots unpaused.");
        }
        else
            SendSysMessage("No party bots in group.");
    }
    else
    {
        Player* pTarget = GetSelectedPlayer();
        if (!pTarget)
        {
            SendSysMessage(LANG_NO_CHAR_SELECTED);
            SetSentErrorMessage(true);
            return false;
        }

        if (HandlePartyBotPauseApplyHelper(pTarget, duration))
        {
            if (pause)
                PSendSysMessage("%s paused for %u seconds.", pTarget->GetName(), (duration / IN_MILLISECONDS));
            else
                PSendSysMessage("%s unpaused.", pTarget->GetName());
        }

        else
            SendSysMessage("Target is not a party bot.");
    }

    return true;
}

bool ChatHandler::HandlePartyBotPauseCommand(char* args)
{
    return HandlePartyBotPauseHelper(args, true);
}

bool ChatHandler::HandlePartyBotUnpauseCommand(char* args)
{
    return HandlePartyBotPauseHelper(args, false);
}

bool ChatHandler::HandlePartyBotPullCommand(char* args)
{
    Player* pPlayer = GetSession()->GetPlayer();
    Unit* pTarget = GetSelectedUnit();
    if (!pTarget || !pPlayer->IsValidAttackTarget(pTarget, true))
    {
        SendSysMessage(LANG_SELECT_CHAR_OR_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    Group* pGroup = pPlayer->GetGroup();
    if (!pGroup)
    {
        SendSysMessage("You are not in a group.");
        SetSentErrorMessage(true);
        return false;
    }

    uint32 duration;
    if (!ExtractUInt32(&args, duration))
        duration = 10 * IN_MILLISECONDS;

    for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        if (Player* pMember = itr->getSource())
        {
            if (pMember == pPlayer)
                continue;

            if (pMember->AI())
            {
                if (PartyBotAI* pAI = dynamic_cast<PartyBotAI*>(pMember->AI()))
                {
                    if (pAI->m_role == ROLE_MELEE_DPS || pAI->m_role == ROLE_RANGE_DPS)
                    {
                        HandlePartyBotPauseApplyHelper(pMember, duration);
                        continue;
                    }
                    else if (pAI->m_role == ROLE_TANK)
                    {
                        if (pMember->IsValidAttackTarget(pTarget))
                        {
                            pAI->ClearMovementHold();
                            pAI->AttackStart(pTarget);
                        }
                    }
                }
            }
        }
    }
    sCommanderMgr.OrderAttack(pPlayer, pTarget->GetObjectGuid(), true);

    PSendSysMessage("Tank party bots are pulling %s, DPS party bots are paused for %d seconds.", pTarget->GetName(), (duration / IN_MILLISECONDS));
    return true;
}

bool ChatHandler::HandlePartyBotUnequipCommand(char* args)
{
    Player* pTarget = GetSelectedPlayer();
    if (!pTarget)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    char* cId = ExtractKeyFromLink(&args, "Hitem");
    if (!cId)
        return false;

    uint32 itemId = 0;
    if (!ExtractUInt32(&cId, itemId))
        return false;

    uint32 count = pTarget->GetItemCount(itemId, false);
    if (!count)
    {
        SendSysMessage("Target does not have that item.");
        SetSentErrorMessage(true);
        return false;
    }

    if (pTarget->AI())
    {
        if (PartyBotAI* pAI = dynamic_cast<PartyBotAI*>(pTarget->AI()))
        {
            // temporary bots are never saved, just delete the item
            if (pTarget->IsSavingDisabled())
            {
                pTarget->DestroyItemCount(itemId, count, true);
                return true;
            }

            // persistent character: move equipped item to bags, or mail it to the bot itself if bags are full
            bool found = false;
            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            {
                Item* pItem = pTarget->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                if (!pItem || pItem->GetEntry() != itemId)
                    continue;

                InventoryResult msg = pTarget->CanUnequipItem(pItem->GetPos(), false);
                if (msg != EQUIP_ERR_OK)
                {
                    PSendSysMessage("Cannot unequip item now (error %u).", uint32(msg));
                    SetSentErrorMessage(true);
                    return false;
                }

                pTarget->AutoUnequipItemFromSlot(slot);
                found = true;
            }

            if (!found)
            {
                SendSysMessage("Target does not have that item equipped.");
                SetSentErrorMessage(true);
                return false;
            }

            pTarget->SaveInventoryAndGoldToDB();
            return true;
        }
    }

    SendSysMessage("Target is not a party bot.");
    SetSentErrorMessage(true);
    return false;
}

// ---------------------------------------------------------------------------
// Inventory commands used by the PartyBotBags addon.
// Replies are system messages in a machine readable form, ';' separated:
//   PBINV;<name>;BEGIN
//   PBINV;<name>;B;<bag 0-4>;<size>;<id:count|0>,...   (bag 0 = backpack)
//   PBINV;<name>;END
//   PBOK;<name>;equip;<itemId>
//   PBOK;<name>;unequip;<itemId>;<mailed 0/1>
//   PBERR;<name>;<code>[;<detail>]
// ---------------------------------------------------------------------------

// A companion allows it by now (Companions/Affinity/Gates); else PBERR;<name>;locked;<needed>;<now>.
// Plain party bots allow everything.
bool ChatHandler::PartyBotGateAllows(Player* pBot, std::string const& name, uint8 gate)
{
    Companions::Gates::Gate const g = Companions::Gates::Gate(gate);
    if (Companions::Gates::Allows(pBot->GetGUIDLow(), g))
        return true;
    PSendSysMessage("PBERR;%s;locked;%u;%u", name.c_str(), Companions::Gates::Of(g).affinity,
        sCompanionMgr.GetAffinity(pBot->GetGUIDLow()));
    SetSentErrorMessage(true);
    return false;
}

static char const* PartyBotInventoryErrorCode(InventoryResult msg)
{
    switch (msg)
    {
        case EQUIP_ERR_CANT_EQUIP_LEVEL_I:
            return "level";
        case EQUIP_ERR_CANT_EQUIP_SKILL:
        case EQUIP_ERR_NO_REQUIRED_PROFICIENCY:
            return "skill";
        case EQUIP_ERR_YOU_CAN_NEVER_USE_THAT_ITEM:
        case EQUIP_ERR_YOU_CAN_NEVER_USE_THAT_ITEM2:
            return "class";
        case EQUIP_ERR_NOT_IN_COMBAT:
            return "combat";
        case EQUIP_ERR_ITEM_CANT_BE_EQUIPPED:
        case EQUIP_ERR_ITEM_DOESNT_GO_TO_SLOT:
            return "notequip";
        case EQUIP_ERR_INVENTORY_FULL:
        case EQUIP_ERR_BAG_FULL:
            return "bagsfull";
        case EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE:
        case EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE2:
        case EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE3:
            return "noslot";
        default:
            return "other";
    }
}

Player* ChatHandler::ExtractPartyBotTarget(char** args, std::string& name)
{
    Player* pBot = nullptr;
    if (*args && **args)
    {
        name = ExtractPlayerNameFromLink(args);
        if (!name.empty())
            pBot = sObjectMgr.GetPlayer(name.c_str());
    }
    else if ((pBot = GetSelectedPlayer()))
        name = pBot->GetName();

    if (!pBot || !pBot->IsInWorld())
    {
        PSendSysMessage("PBERR;%s;notfound", name.c_str());
        SetSentErrorMessage(true);
        return nullptr;
    }

    name = pBot->GetName();
    if (!pBot->AI() || !dynamic_cast<PartyBotAI*>(pBot->AI()))
    {
        PSendSysMessage("PBERR;%s;notbot", name.c_str());
        SetSentErrorMessage(true);
        return nullptr;
    }

    return pBot;
}

// .partybot bags [name]
bool ChatHandler::HandlePartyBotBagsCommand(char* args)
{
    std::string name;
    Player* pBot = ExtractPartyBotTarget(&args, name);
    if (!pBot || !PartyBotGateAllows(pBot, name, uint8(Companions::Gates::Gate::ManageBags)))
        return false;

    PSendSysMessage("PBINV;%s;BEGIN", name.c_str());

    for (uint8 bagIndex = 0; bagIndex <= 4; ++bagIndex)
    {
        std::ostringstream ss;
        ss << "PBINV;" << name << ";B;" << uint32(bagIndex) << ";";

        if (bagIndex == 0)
        {
            ss << uint32(INVENTORY_SLOT_ITEM_END - INVENTORY_SLOT_ITEM_START) << ";";
            for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
            {
                if (i != INVENTORY_SLOT_ITEM_START)
                    ss << ",";
                if (Item* pItem = pBot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                    ss << pItem->GetEntry() << ":" << pItem->GetCount();
                else
                    ss << "0";
            }
        }
        else
        {
            Bag* pBag = (Bag*)pBot->GetItemByPos(INVENTORY_SLOT_BAG_0, INVENTORY_SLOT_BAG_START + bagIndex - 1);
            uint32 size = pBag ? pBag->GetBagSize() : 0;
            ss << size << ";";
            for (uint32 j = 0; j < size; ++j)
            {
                if (j)
                    ss << ",";
                if (Item* pItem = pBag->GetItemByPos(uint8(j)))
                    ss << pItem->GetEntry() << ":" << pItem->GetCount();
                else
                    ss << "0";
            }
        }

        SendSysMessage(ss.str().c_str());
    }

    // Its money, and whether the player may take some (.partybot takemoney; Gates TakeMoney).
    Companions::Gates::Gate const takeMoney = Companions::Gates::Gate::TakeMoney;
    PSendSysMessage("PBINV;%s;M;%u;%u;%u", name.c_str(), pBot->GetMoney(),
        uint32(Companions::Gates::Allows(pBot->GetGUIDLow(), takeMoney)), Companions::Gates::Of(takeMoney).affinity);

    PSendSysMessage("PBINV;%s;END", name.c_str());
    return true;
}

// .partybot takemoney <name> <copper>   the command user takes that much of the bot's money
bool ChatHandler::HandlePartyBotTakeMoneyCommand(char* args)
{
    Player* pPlayer = m_session ? m_session->GetPlayer() : nullptr;
    if (!pPlayer)
        return false;

    std::string name;
    Player* pBot = ExtractPartyBotTarget(&args, name);
    if (!pBot || !PartyBotGateAllows(pBot, name, uint8(Companions::Gates::Gate::TakeMoney)))
        return false;

    uint32 copper = 0;
    if (!ExtractUInt32(&args, copper) || !copper)
    {
        PSendSysMessage("PBERR;%s;usage;.partybot takemoney <name> <copper>", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }
    if (copper > pBot->GetMoney())
    {
        PSendSysMessage("PBERR;%s;nomoney;%u", name.c_str(), pBot->GetMoney());
        SetSentErrorMessage(true);
        return false;
    }
    if (uint64(pPlayer->GetMoney()) + copper > pPlayer->GetMaxMoney())
    {
        PSendSysMessage("PBERR;%s;maxmoney", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    pBot->ModifyMoney(-int32(copper));
    pPlayer->ModifyMoney(int32(copper));
    pBot->SaveInventoryAndGoldToDB();
    pPlayer->SaveInventoryAndGoldToDB();
    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Companion] %s took %u copper from %s.", pPlayer->GetName(), copper, name.c_str());
    PSendSysMessage("PBOK;%s;takemoney;%u;%u", name.c_str(), copper, pBot->GetMoney());
    return true;
}

// .partybot equipslot <name> <bag 0-4> <slot>   (slot is 0 based inside the bag)
bool ChatHandler::HandlePartyBotEquipSlotCommand(char* args)
{
    std::string name;
    Player* pBot = ExtractPartyBotTarget(&args, name);
    if (!pBot || !PartyBotGateAllows(pBot, name, uint8(Companions::Gates::Gate::ManageBags)))
        return false;

    uint32 bagIndex = 0;
    uint32 slotIndex = 0;
    if (!ExtractUInt32(&args, bagIndex) || !ExtractUInt32(&args, slotIndex) || bagIndex > 4)
    {
        PSendSysMessage("PBERR;%s;usage;.partybot equipslot <name> <bag 0-4> <slot>", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    uint8 bag = INVENTORY_SLOT_BAG_0;
    uint8 slot = 0;
    if (bagIndex == 0)
    {
        if (slotIndex >= uint32(INVENTORY_SLOT_ITEM_END - INVENTORY_SLOT_ITEM_START))
        {
            PSendSysMessage("PBERR;%s;empty", name.c_str());
            SetSentErrorMessage(true);
            return false;
        }
        slot = uint8(INVENTORY_SLOT_ITEM_START + slotIndex);
    }
    else
    {
        bag = uint8(INVENTORY_SLOT_BAG_START + bagIndex - 1);
        slot = uint8(slotIndex);
    }

    Item* pItem = pBot->GetItemByPos(bag, slot);
    if (!pItem)
    {
        PSendSysMessage("PBERR;%s;empty", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    uint32 itemId = pItem->GetEntry();
    uint16 dest = 0;
    InventoryResult msg = pBot->CanEquipItem(NULL_SLOT, dest, pItem, !pItem->IsBag());
    if (msg != EQUIP_ERR_OK)
    {
        PSendSysMessage("PBERR;%s;%s;%u", name.c_str(), PartyBotInventoryErrorCode(msg), uint32(msg));
        SetSentErrorMessage(true);
        return false;
    }

    WorldPackets::Item::AutoEquipItem packet;
    packet.srcbag = bag;
    packet.srcslot = slot;
    pBot->GetSession()->HandleAutoEquipItemOpcode(packet);

    // equipment slots and bag slots are both directly in bag 0, below INVENTORY_SLOT_BAG_END
    if (pItem->IsInBag() || pItem->GetSlot() >= INVENTORY_SLOT_BAG_END)
    {
        PSendSysMessage("PBERR;%s;other", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    pBot->SaveInventoryAndGoldToDB();
    PSendSysMessage("PBOK;%s;equip;%u", name.c_str(), itemId);
    return true;
}

// .partybot unequipslot <name> <equipment slot 0-18>
bool ChatHandler::HandlePartyBotUnequipSlotCommand(char* args)
{
    std::string name;
    Player* pBot = ExtractPartyBotTarget(&args, name);
    if (!pBot || !PartyBotGateAllows(pBot, name, uint8(Companions::Gates::Gate::ManageBags)))
        return false;

    uint32 slotIndex = 0;
    if (!ExtractUInt32(&args, slotIndex) || slotIndex >= EQUIPMENT_SLOT_END)
    {
        PSendSysMessage("PBERR;%s;usage;.partybot unequipslot <name> <slot 0-18>", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    Item* pItem = pBot->GetItemByPos(INVENTORY_SLOT_BAG_0, uint8(slotIndex));
    if (!pItem)
    {
        PSendSysMessage("PBERR;%s;empty", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    InventoryResult msg = pBot->CanUnequipItem(pItem->GetPos(), false);
    if (msg != EQUIP_ERR_OK)
    {
        PSendSysMessage("PBERR;%s;%s;%u", name.c_str(), PartyBotInventoryErrorCode(msg), uint32(msg));
        SetSentErrorMessage(true);
        return false;
    }

    ItemPosCountVec dest;
    uint8 bagSlot = 0;
    bool fitsInBags = pBot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, pItem, bagSlot, false) == EQUIP_ERR_OK;

    // temporary bots are never saved, do not create mail for them
    if (!fitsInBags && pBot->IsSavingDisabled())
    {
        PSendSysMessage("PBERR;%s;bagsfull", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    uint32 itemId = pItem->GetEntry();
    pBot->AutoUnequipItemFromSlot(slotIndex); // to bags, or mail to the bot itself when full
    pBot->SaveInventoryAndGoldToDB();
    PSendSysMessage("PBOK;%s;unequip;%u;%u", name.c_str(), itemId, fitsInBags ? 0u : 1u);
    return true;
}

// bag index used by the addon: 0 = backpack, 1-4 = bags, 5 = equipment (slot 0-18)
static bool ResolvePartyBotItemPos(uint32 bagIndex, uint32 slotIndex, uint8& bag, uint8& slot)
{
    if (bagIndex == 0)
    {
        if (slotIndex >= uint32(INVENTORY_SLOT_ITEM_END - INVENTORY_SLOT_ITEM_START))
            return false;
        bag = INVENTORY_SLOT_BAG_0;
        slot = uint8(INVENTORY_SLOT_ITEM_START + slotIndex);
        return true;
    }
    if (bagIndex <= 4)
    {
        if (slotIndex >= MAX_BAG_SIZE)
            return false;
        bag = uint8(INVENTORY_SLOT_BAG_START + bagIndex - 1);
        slot = uint8(slotIndex);
        return true;
    }
    if (bagIndex == 5)
    {
        if (slotIndex >= EQUIPMENT_SLOT_END)
            return false;
        bag = INVENTORY_SLOT_BAG_0;
        slot = uint8(slotIndex);
        return true;
    }
    return false;
}

// .partybot useslot <name> <bag 0-4> <slot>   bot uses the item on itself (potion, food, scroll...)
bool ChatHandler::HandlePartyBotUseSlotCommand(char* args)
{
    std::string name;
    Player* pBot = ExtractPartyBotTarget(&args, name);
    if (!pBot || !PartyBotGateAllows(pBot, name, uint8(Companions::Gates::Gate::ManageBags)))
        return false;

    uint32 bagIndex = 0;
    uint32 slotIndex = 0;
    uint8 bag = 0;
    uint8 slot = 0;
    if (!ExtractUInt32(&args, bagIndex) || !ExtractUInt32(&args, slotIndex) || bagIndex > 4 ||
        !ResolvePartyBotItemPos(bagIndex, slotIndex, bag, slot))
    {
        PSendSysMessage("PBERR;%s;usage;.partybot useslot <name> <bag 0-4> <slot>", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    Item* pItem = pBot->GetItemByPos(bag, slot);
    if (!pItem)
    {
        PSendSysMessage("PBERR;%s;empty", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    ItemPrototype const* pProto = pItem->GetProto();
    uint32 itemId = pItem->GetEntry();

    // first on-use spell; items that only work while equipped (trinkets) are not handled here
    uint8 spellSlot = MAX_ITEM_PROTO_SPELLS;
    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        if (pProto->Spells[i].SpellId && pProto->Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_ON_USE)
        {
            spellSlot = i;
            break;
        }
    }
    SpellEntry const* pSpellEntry = spellSlot < MAX_ITEM_PROTO_SPELLS ? sSpellMgr.GetSpellEntry(pProto->Spells[spellSlot].SpellId) : nullptr;
    if (!pSpellEntry || pProto->InventoryType != INVTYPE_NON_EQUIP)
    {
        PSendSysMessage("PBERR;%s;notusable", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    if (!pBot->IsAlive())
    {
        PSendSysMessage("PBERR;%s;dead", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    InventoryResult msg = pBot->CanUseItem(pItem);
    if (msg != EQUIP_ERR_OK)
    {
        PSendSysMessage("PBERR;%s;%s;%u", name.c_str(), PartyBotInventoryErrorCode(msg), uint32(msg));
        SetSentErrorMessage(true);
        return false;
    }

    if (pBot->IsInCombat() && pSpellEntry->IsNonCombatSpell())
    {
        PSendSysMessage("PBERR;%s;combat", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    if (!pBot->IsSpellReady(pSpellEntry, pProto))
    {
        PSendSysMessage("PBERR;%s;cooldown", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    uint32 countBefore = pBot->GetItemCount(itemId);

    WorldPackets::Spell::UseItem packet;
    packet.bagIndex = bag;
    packet.slot = slot;
    packet.spellSlot = spellSlot;
    packet.targets.setUnitTarget(pBot);
    pBot->GetSession()->HandleUseItemOpcode(packet);
    // pItem may be deleted now (last charge used)

    // the cast went through if the item was consumed or its cooldown started
    if (pBot->GetItemCount(itemId) >= countBefore && pBot->IsSpellReady(pSpellEntry, pProto))
    {
        PSendSysMessage("PBERR;%s;castfail", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    pBot->SaveInventoryAndGoldToDB();
    PSendSysMessage("PBOK;%s;use;%u", name.c_str(), itemId);
    return true;
}

// .partybot giveslot <name> <bag 0-5> <slot>   move an item from the bot to the command user's bags
bool ChatHandler::HandlePartyBotGiveSlotCommand(char* args)
{
    Player* pPlayer = m_session ? m_session->GetPlayer() : nullptr;
    if (!pPlayer)
        return false;

    std::string name;
    Player* pBot = ExtractPartyBotTarget(&args, name);
    if (!pBot || !PartyBotGateAllows(pBot, name, uint8(Companions::Gates::Gate::ManageBags)))
        return false;

    uint32 bagIndex = 0;
    uint32 slotIndex = 0;
    uint8 bag = 0;
    uint8 slot = 0;
    if (!ExtractUInt32(&args, bagIndex) || !ExtractUInt32(&args, slotIndex) ||
        !ResolvePartyBotItemPos(bagIndex, slotIndex, bag, slot))
    {
        PSendSysMessage("PBERR;%s;usage;.partybot giveslot <name> <bag 0-5> <slot>", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    Item* pItem = pBot->GetItemByPos(bag, slot);
    if (!pItem)
    {
        PSendSysMessage("PBERR;%s;empty", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    if (pItem->IsInTrade())
    {
        PSendSysMessage("PBERR;%s;other", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    if (pItem->IsBag() && !((Bag*)pItem)->IsEmpty())
    {
        PSendSysMessage("PBERR;%s;bagnotempty", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    // equipped items: same checks as unequipping (combat, etc.)
    if (bag == INVENTORY_SLOT_BAG_0 && slot < INVENTORY_SLOT_BAG_END)
    {
        InventoryResult msg = pBot->CanUnequipItem(pItem->GetPos(), false);
        if (msg != EQUIP_ERR_OK)
        {
            PSendSysMessage("PBERR;%s;%s;%u", name.c_str(), PartyBotInventoryErrorCode(msg), uint32(msg));
            SetSentErrorMessage(true);
            return false;
        }
    }

    ItemPosCountVec dest;
    uint8 bagSlot = 0;
    if (pPlayer->CanStoreItem(NULL_BAG, NULL_SLOT, dest, pItem, bagSlot, false) != EQUIP_ERR_OK)
    {
        PSendSysMessage("PBERR;%s;mybagsfull", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    uint32 itemId = pItem->GetEntry();
    uint32 count = pItem->GetCount();

    // same steps as a trade (TradeHandler): remove from bot, store into player.
    // Items of temporary bots were never saved, so they are new rows for the player.
    pBot->MoveItemFromInventory(bag, slot, true);
    pPlayer->MoveItemToInventory(dest, pItem, true, !pBot->IsSavingDisabled());
    // pItem may be merged into an existing stack and deleted now

    pBot->SaveInventoryAndGoldToDB();
    pPlayer->SaveInventoryAndGoldToDB();

    PSendSysMessage("PBOK;%s;give;%u;%u", name.c_str(), itemId, count);
    return true;
}

// .partybot takeslot <name> <bag 0-5> <slot>   move an item from the command user to the bot's bags
// (bag/slot refer to the command user's inventory, same numbering as giveslot)
bool ChatHandler::HandlePartyBotTakeSlotCommand(char* args)
{
    Player* pPlayer = m_session ? m_session->GetPlayer() : nullptr;
    if (!pPlayer)
        return false;

    std::string name;
    Player* pBot = ExtractPartyBotTarget(&args, name);
    if (!pBot || !PartyBotGateAllows(pBot, name, uint8(Companions::Gates::Gate::ManageBags)))
        return false;

    uint32 bagIndex = 0;
    uint32 slotIndex = 0;
    uint8 bag = 0;
    uint8 slot = 0;
    if (!ExtractUInt32(&args, bagIndex) || !ExtractUInt32(&args, slotIndex) ||
        !ResolvePartyBotItemPos(bagIndex, slotIndex, bag, slot))
    {
        PSendSysMessage("PBERR;%s;usage;.partybot takeslot <name> <bag 0-5> <slot>", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    Item* pItem = pPlayer->GetItemByPos(bag, slot);
    if (!pItem)
    {
        PSendSysMessage("PBERR;%s;empty", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    if (pItem->IsSoulBound())
    {
        PSendSysMessage("PBERR;%s;bound", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    if (pItem->IsInTrade())
    {
        PSendSysMessage("PBERR;%s;other", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    if (pItem->IsBag() && !((Bag*)pItem)->IsEmpty())
    {
        PSendSysMessage("PBERR;%s;bagnotempty", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    if (bag == INVENTORY_SLOT_BAG_0 && slot < INVENTORY_SLOT_BAG_END)
    {
        InventoryResult msg = pPlayer->CanUnequipItem(pItem->GetPos(), false);
        if (msg != EQUIP_ERR_OK)
        {
            PSendSysMessage("PBERR;%s;%s;%u", name.c_str(), PartyBotInventoryErrorCode(msg), uint32(msg));
            SetSentErrorMessage(true);
            return false;
        }
    }

    ItemPosCountVec dest;
    uint8 bagSlot = 0;
    if (pBot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, pItem, bagSlot, false) != EQUIP_ERR_OK)
    {
        PSendSysMessage("PBERR;%s;botbagsfull", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    uint32 itemId = pItem->GetEntry();
    uint32 count = pItem->GetCount();

    // same steps as a trade (TradeHandler)
    pPlayer->MoveItemFromInventory(bag, slot, true);
    if (pBot->IsSavingDisabled())
    {
        // temporary bot: item must not stay in the DB, or it could be duplicated
        pItem->DeleteFromInventoryDB();
        pItem->DeleteAllFromDB();
    }
    pBot->MoveItemToInventory(dest, pItem, true, true);
    // pItem may be merged into an existing stack and deleted now

    pPlayer->SaveInventoryAndGoldToDB();
    pBot->SaveInventoryAndGoldToDB();

    PSendSysMessage("PBOK;%s;take;%u;%u", name.c_str(), itemId, count);
    return true;
}

// Accounts that own at least one character, for the addon's character picker.
// .partybot accounts [page]   (page from 1)
//   PBACC;BEGIN;<page>;<pages>;<total>
//   PBACC;A;<accountId>;<characters>;<is mine 0/1>;<username>
//   PBACC;END
bool ChatHandler::HandlePartyBotAccountsCommand(char* args)
{
    uint32 const perPage = 12;
    uint32 page = 1;
    ExtractOptUInt32(&args, page, 1);
    if (!page)
        page = 1;

    uint32 total = 0;
    if (std::unique_ptr<QueryResult> countResult = CharacterDatabase.Query("SELECT COUNT(DISTINCT `account`) FROM `characters`"))
        total = countResult->Fetch()[0].GetUInt32();

    uint32 const pages = std::max<uint32>(1, (total + perPage - 1) / perPage);
    if (page > pages)
        page = pages;

    std::vector<std::pair<uint32, uint32>> accounts; // account id, character count
    if (std::unique_ptr<QueryResult> result = CharacterDatabase.PQuery(
            "SELECT `account`, COUNT(*) FROM `characters` GROUP BY `account` ORDER BY `account` LIMIT %u, %u",
            (page - 1) * perPage, perPage))
    {
        do
        {
            Field* fields = result->Fetch();
            accounts.emplace_back(fields[0].GetUInt32(), fields[1].GetUInt32());
        } while (result->NextRow());
    }

    std::map<uint32, std::string> usernames;
    if (!accounts.empty())
    {
        std::ostringstream ids;
        for (size_t i = 0; i < accounts.size(); ++i)
            ids << (i ? "," : "") << accounts[i].first;
        if (std::unique_ptr<QueryResult> result = LoginDatabase.PQuery(
                "SELECT `id`, `username` FROM `account` WHERE `id` IN (%s)", ids.str().c_str()))
        {
            do
            {
                Field* fields = result->Fetch();
                usernames[fields[0].GetUInt32()] = fields[1].GetCppString();
            } while (result->NextRow());
        }
    }

    uint32 const myAccount = GetSession() ? GetSession()->GetAccountId() : 0;
    PSendSysMessage("PBACC;BEGIN;%u;%u;%u", page, pages, total);
    for (auto const& acc : accounts)
    {
        auto itr = usernames.find(acc.first);
        std::string const username = itr != usernames.end() ? itr->second : "?";
        PSendSysMessage("PBACC;A;%u;%u;%u;%s", acc.first, acc.second, acc.first == myAccount ? 1 : 0, username.c_str());
    }
    SendSysMessage("PBACC;END");
    return true;
}

// Characters of one account, with their current bot state.
// .partybot chars <accountId>
//   PBCHR;<accountId>;BEGIN;<count>
//   PBCHR;<accountId>;C;<race>;<class>;<level>;<state>;<role>;<name>
//   PBCHR;<accountId>;END
// state: off (offline) | loading | me (the command user) | mine (bot in the user's party)
//        | bot (someone else's party bot) | on (online, played by a person or another bot type)
// role: current role for mine/bot, else "auto"
bool ChatHandler::HandlePartyBotCharsCommand(char* args)
{
    uint32 accountId = 0;
    if (!ExtractUInt32(&args, accountId) || !accountId)
    {
        SendSysMessage("PBERR;;usage");
        SetSentErrorMessage(true);
        return false;
    }

    std::vector<PlayerCacheData const*> chars;
    sObjectMgr.GetPlayerDataForAccount(accountId, chars);
    std::sort(chars.begin(), chars.end(), [](PlayerCacheData const* a, PlayerCacheData const* b)
    {
        return a->sName < b->sName;
    });

    Player* pUser = GetSession() ? GetSession()->GetPlayer() : nullptr;

    PSendSysMessage("PBCHR;%u;BEGIN;%u", accountId, uint32(chars.size()));
    for (PlayerCacheData const* data : chars)
    {
        char const* state = "off";
        char const* role = "auto";
        uint32 level = data->uiLevel;

        if (Player* pChar = ObjectAccessor::FindPlayerNotInWorld(ObjectGuid(HIGHGUID_PLAYER, data->uiGuid)))
        {
            level = pChar->GetLevel();
            PartyBotAI* pAI = pChar->AI() ? dynamic_cast<PartyBotAI*>(pChar->AI()) : nullptr;
            if (!pChar->IsInWorld())
                state = "loading";
            else if (pChar == pUser)
                state = "me";
            else if (pAI)
                state = (pUser && pAI->m_leaderGuid == pUser->GetObjectGuid()) ? "mine" : "bot";
            else
                state = "on";

            if (pAI && pChar->IsInWorld())
                role = PartyBotRoleName(pAI->m_role);
            else if (pChar == pUser && pChar->IsInWorld())
            {
                // Your own role: the one you chose, else what the talents and gear say.
                CombatBotRoles own = CombatBotRoles(sCommanderMgr.GetPlayerRole(pChar->GetObjectGuid()));
                if (own == ROLE_INVALID)
                    own = RoleCapabilities::Assess(pChar).Best();
                role = PartyBotRoleName(own);
            }
        }

        PSendSysMessage("PBCHR;%u;C;%u;%u;%u;%s;%s;%s", accountId, data->uiRace, data->uiClass, level, state, role, data->sName.c_str());
    }
    PSendSysMessage("PBCHR;%u;END", accountId);
    return true;
}

// .partybot remove [name]   (without a name: the selected bot)
bool ChatHandler::HandlePartyBotRemoveCommand(char* args)
{
    if (args && *args)
    {
        std::string name;
        Player* pBot = ExtractPartyBotTarget(&args, name);
        if (!pBot)
            return false;
        static_cast<PartyBotAI*>(pBot->AI())->botEntry->requestRemoval = true;
        PSendSysMessage("PBOK;%s;remove", name.c_str());
        return true;
    }

    Player* pTarget = GetSelectedPlayer();
    if (!pTarget)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    if (pTarget->AI())
    {
        if (PartyBotAI* pAI = dynamic_cast<PartyBotAI*>(pTarget->AI()))
        {
            pAI->botEntry->requestRemoval = true;
            return true;
        }
    }

    SendSysMessage("Target is not a party bot.");
    SetSentErrorMessage(true);
    return false;
}

// ---------------------------------------------------------------------------
// Quests for persistent party bots
//
// Bots take the quests their group's real player takes and make progress like
// real group members (kill credit is already shared by the core). Quest drops
// the player loots are copied to bots that still need them (not to companions: they
// loot their own). Turning in is left to the player, who logs in the bot's own account;
// companions turn in themselves (Life/Errands/QuestTurnIn.h).
//
// Replies to the player are system messages parsed by the PartyBotBags addon:
//   PBQUEST;<bot>;accept;<questId>;<title>
//   PBQUEST;<bot>;skip;<questId>;<reason>;<title>
//   PBQUEST;<bot>;item;<itemId>;<count>
//   PBQUEST;<bot>;itemfail;<itemId>
// ---------------------------------------------------------------------------

static bool IsPersistentPartyBot(Player* pPlayer)
{
    return pPlayer && pPlayer->IsInWorld() && pPlayer->IsBot() && !pPlayer->IsSavingDisabled() &&
           pPlayer->AI() && dynamic_cast<PartyBotAI*>(pPlayer->AI());
}

// Why a bot cannot take a quest. nullptr means it can.
// "have" means the quest is already in its log, which is not reported.
static char const* GetBotQuestSkipReason(Player* pBot, Quest const* pQuest)
{
    if (!pQuest->IsRepeatable() && pBot->GetQuestRewardStatus(pQuest->GetQuestId()))
        return "done";
    if (!pBot->SatisfyQuestStatus(pQuest, false))
        return "have";
    if ((pQuest->GetMaxLevel() && pQuest->GetMaxLevel() < pBot->GetLevel()) || !pBot->SatisfyQuestLevel(pQuest, false))
        return "level";
    if (!pBot->SatisfyQuestClass(pQuest, false))
        return "class";
    if (!pBot->SatisfyQuestRace(pQuest, false))
        return "race";
    if (!pBot->SatisfyQuestPreviousQuest(pQuest, false) || !pBot->SatisfyQuestPrevChain(pQuest, false) ||
        !pBot->SatisfyQuestNextChain(pQuest, false) || !pBot->SatisfyQuestExclusiveGroup(pQuest, false) ||
        !pBot->SatisfyQuestBreadcrumbQuest(pQuest, false) || !pBot->SatisfyQuestDependentBreadcrumbQuests(pQuest, false))
        return "prev";
    if (!pBot->SatisfyQuestSkill(pQuest, false))
        return "skill";
    if (!pBot->SatisfyQuestReputation(pQuest, false))
        return "rep";
    if (!pBot->CanTakeQuest(pQuest, false))
        return "other";
    if (!pBot->SatisfyQuestLog(false))
        return "logfull";
    if (!pBot->CanAddQuest(pQuest, false))
        return "bagsfull"; // no room for the item the quest gives at start
    return nullptr;
}

// Same as an accepted shared quest (HandleQuestConfirmAccept): no quest giver,
// so the giver's accept scripts do not run a second time.
static void BotAddQuest(Player* pBot, Quest const* pQuest)
{
    pBot->AddQuest(pQuest, nullptr);

    if (pBot->CanCompleteQuest(pQuest->GetQuestId()))
        pBot->CompleteQuest(pQuest->GetQuestId());
}

void PlayerBotMgr::OnPlayerAcceptedQuest(Player* pPlayer, Quest const* pQuest)
{
    if (!pPlayer || !pQuest || pPlayer->IsBot())
        return;

    Group* pGroup = pPlayer->GetGroup();
    if (!pGroup)
        return;

    ChatHandler handler(pPlayer);
    for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* pBot = itr->getSource();
        if (pBot == pPlayer || !IsPersistentPartyBot(pBot))
            continue;

        char const* reason = nullptr;
        if (!pBot->IsInMap(pPlayer) || !pBot->IsAtGroupRewardDistance(pPlayer))
            reason = "far";
        else
            reason = GetBotQuestSkipReason(pBot, pQuest);

        if (reason && strcmp(reason, "have") == 0)
            continue;

        if (reason)
        {
            handler.PSendSysMessage("PBQUEST;%s;skip;%u;%s;%s", pBot->GetName(), pQuest->GetQuestId(), reason, pQuest->GetTitle().c_str());
            continue;
        }

        BotAddQuest(pBot, pQuest);
        handler.PSendSysMessage("PBQUEST;%s;accept;%u;%s", pBot->GetName(), pQuest->GetQuestId(), pQuest->GetTitle().c_str());
    }
}

void PlayerBotMgr::OnQuestSharedWithBot(Player* pSharer, Player* pBot, Quest const* pQuest)
{
    if (!pSharer || !pQuest || pSharer->IsBot() || !IsPersistentPartyBot(pBot))
        return;

    // what the client sends when the player clicks "Accept" in the shared quest window,
    // so all the normal share checks apply and the sharer gets the normal reply
    WorldPackets::Quest::QuestgiverAcceptQuest packet;
    packet.guid = pSharer->GetObjectGuid();
    packet.quest = pQuest->GetQuestId();
    pBot->GetSession()->HandleQuestgiverAcceptQuestOpcode(packet);

    if (pBot->IsCurrentQuest(pQuest->GetQuestId()))
        ChatHandler(pSharer).PSendSysMessage("PBQUEST;%s;accept;%u;%s", pBot->GetName(), pQuest->GetQuestId(), pQuest->GetTitle().c_str());
}

void PlayerBotMgr::OnPlayerLootedQuestItem(Player* pPlayer, uint32 itemId, uint32 count, int32 randomPropertyId, WorldObject const* pSource)
{
    if (!pPlayer || pPlayer->IsBot() || !count)
        return;

    Group* pGroup = pPlayer->GetGroup();
    if (!pGroup)
        return;

    if (!pSource)
        pSource = pPlayer;

    ChatHandler handler(pPlayer);
    for (GroupReference* itr = pGroup->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* pBot = itr->getSource();
        if (pBot == pPlayer || !IsPersistentPartyBot(pBot))
            continue;
        // Companions loot their own copy from the corpse (Life/Errands/LootRun.h).
        if (sCompanionMgr.IsCompanion(pBot->GetGUIDLow()))
            continue;

        // same range as kill credit, only bots that still need the item for a quest in their log
        if (!pBot->IsInMap(pPlayer) || !pBot->IsAtGroupRewardDistance(pSource))
            continue;
        if (!pBot->HasQuestForItem(itemId))
            continue;

        // quest items usually have a max count, so try fewer if the full stack does not fit
        uint32 stored = 0;
        for (uint32 n = count; n > 0; --n)
        {
            ItemPosCountVec dest;
            if (pBot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, n) == EQUIP_ERR_OK)
            {
                // StoreNewItem also updates the quest counters
                if (pBot->StoreNewItem(dest, itemId, true, randomPropertyId))
                    stored = n;
                break;
            }
        }

        if (stored)
            handler.PSendSysMessage("PBQUEST;%s;item;%u;%u", pBot->GetName(), itemId, stored);
        else
            handler.PSendSysMessage("PBQUEST;%s;itemfail;%u", pBot->GetName(), itemId);
    }
}
