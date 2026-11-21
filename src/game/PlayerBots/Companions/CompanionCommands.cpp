/*
 * Companions: chat commands (soul companions doc §5).
 *
 *   .partybot reply <id> <option>             the player's answer to a bot's request (the
 *                                             PartyBot addon's popup sends it)
 *   .partybot rewardmode <name> auto|ask      who picks the companion's quest rewards
 *   .partybot gate add [name]                 a town gate where the player stands (companions
 *                 remove                      wait there after a town trip, Shared/TownGates)
 *                 list                        (remove: the nearest within 30 yd; list: this zone)
 *   .partybot mirror show <id>                dev: a mirror quest in your own quest log
 *                    count <id> <objective> <n>    (Shared/QuestMirror; objective 1..4)
 *                    remove <id>
 *                    follow <bot> <questId>   dev: your log follows a bot's quest (placeholder,
 *                    unfollow <bot>                then its mirror; ClassQuests/MirrorSync)
 *   .partybot classquests [bot]               dev: the class quest chains for a bot (done or not, asks)
 *   .partybot classquests visit [bot]         dev: counts as a capital visit now (ClassQuestFlow)
 *   .partybot classquests class <classId>     dev: all chains of a class (Shared/ClassQuestChains)
 *   .partybot callover <guid>                 one of them comes to you (CompanionMgr::CallOver)
 *   .partybot companions                      the companions you recruited (addon Companions panel):
 *     PBCMP;BEGIN;<count>
 *     PBCMP;GATE;<affinity>;<class mask, 0 = any>;<key>;<ready 1, 0 = not built yet>;<label>   what
 *            affinity allows (Affinity/Gates, all of them by affinity: the addon also shows the
 *            ones not built yet, as goals)
 *     PBCMP;C;<guid>;<race>;<class>;<gender>;<level>;<xp>;<next level xp, 0 = max>;<talents a/b/c>;
 *            <affinity 0-100>;<state 0 at his place (not in the world), 1 in the world, 2 in your
 *            group (also while coming back with you)>;<zone id: where he is>;<recruited at>;
 *            <tag,tag,...>;<name>
 *     PBCMP;BLOCK;<guid>;<gate key>;<text>   after its C line, online only: an open gate's service
 *            cannot be had now (Talk services' Blockers: "缺传送门符文")
 *     PBCMP;TRAIT;<guid>;<affinity it came at, 80 / 90>;<key>;<rarity 0 common, 1 rare, 2 epic>;
 *            <label>;<text>   after its C line: a friend trait he has (Affinity/Traits)
 *     PBCMP;SLOTS;<used>;<total>;<max>;<level of the next free one, 0 = all open>;<can buy 0/1>;
 *            <copper of the next one to buy>;<levels the free ones open at, l,l,...>
 *            (after BEGIN; companion slots doc)
 *     PBCMP;END
 *     PBCMP;ACC;<guid>;<1 a companion who joined your account, 2 a character you made>   after its C
 *            line: one of your account's characters (companion account doc §5; affinity 0 for
 *            your own ones), called over like a companion, takes no slot
 *     C lines with state 3 are former companions (故人): no slot, not called over.
 *   .partybot farewell <guid>    道别: he becomes a former companion (leaves you if with you)
 *   .partybot rehire <guid>      a former companion comes back (needs a free slot; comes over if he can)
 *   .partybot forget <guid>      永别: a former companion's character is deleted, he lives on as a stranger
 *   .partybot buyslot            buys the next companion slot (all free ones open)
 *   .partybot joinaccount <guid> a companion at affinity 100 joins your account (companion account doc)
 *   .partybot sendback <guid>    one who joined your account goes back to being your companion
 *                                (a former one without a free slot)
 */

#include "Chat.h"
#include "Player.h"
#include "Group.h"
#include "Companions/CompanionMgr.h"
#include "Companions/Affinity/Gates.h"
#include "Companions/Slots/Slots.h"
#include "Companions/Talk/TalkService.h"
#include "Companions/Talk/Talk.h"
#include "Population/PopulationMgr.h"
#include "Life/Requests/RequestBoard.h"
#include "Shared/QuestMirror.h"
#include "Shared/ClassQuestChains.h"
#include "Shared/TownGates.h"
#include "Shared/TalentSpread.h"
#include "PartyBotAI.h"
#include "PlayerBotMgr.h"
#include "Shared/BotChat.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "QuestDef.h"
#include "World.h"
#include "WorldSession.h"
#include "Database/DatabaseEnv.h"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

bool ChatHandler::HandlePartyBotCompanionsCommand(char* /*args*/)
{
    Player* player = GetSession() ? GetSession()->GetPlayer() : nullptr;
    if (!player)
        return false;

    struct Row
    {
        uint32 race = 0, cls = 0, gender = 0, level = 0, xp = 0, nextXp = 0, zone = 0, state = 0;
        BotShared::TalentSpread::Points talents = { 0, 0, 0 };
        std::string name;
        bool known = false;
        bool online = false;
        std::vector<Companions::Talk::Blocker> blockers;   // online only
    };

    std::vector<Companions::CompanionInfo> companions = sCompanionMgr.OfRecruiter(player->GetGUIDLow());
    // (a trial partner is no companion yet: not on the panel, recruiting doc §2.2; one who joined
    // an account is listed with that account's characters below)
    companions.erase(std::remove_if(companions.begin(), companions.end(),
        [](Companions::CompanionInfo const& info) { return info.trial || info.inAccount; }), companions.end());
    // The other characters of his account (companion account doc §5): who joined it, his own ones.
    std::unordered_map<uint32, uint32> accountKinds;
    for (Companions::CompanionMgr::AccountCharacter const& character : sCompanionMgr.AccountCharactersOf(player))
    {
        Companions::CompanionInfo info;
        if (Companions::CompanionInfo const* known = sCompanionMgr.Find(character.charGuid))
            info = *known;
        info.charGuid = character.charGuid;
        if (character.kind == Companions::CompanionMgr::AccountKind::Own)
            info.affinity = 0;
        else
            info.affinity = sCompanionMgr.GetAffinity(character.charGuid);
        accountKinds[character.charGuid] = uint32(character.kind);
        companions.push_back(std::move(info));
    }
    uint32 const maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
    std::unordered_map<uint32, Row> rows;
    std::string offline;    // guids for the database, "1,2,3"

    for (Companions::CompanionInfo const& info : companions)
    {
        Row& row = rows[info.charGuid];
        Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, info.charGuid));
        if (!bot || !bot->IsInWorld())
        {
            offline += (offline.empty() ? "" : ",") + std::to_string(info.charGuid);
            // (his group's member, about to come back with him: CompanionMgr::OnLoginRequest)
            if (info.parted)
                row.state = 3;
            else if (player->GetGroup() && player->GetGroup()->IsMember(ObjectGuid(HIGHGUID_PLAYER, info.charGuid)))
                row.state = 2;
            continue;
        }
        row.known = true;
        row.online = true;
        row.race = bot->GetRace();
        row.cls = bot->GetClass();
        row.gender = bot->GetGender();
        row.level = bot->GetLevel();
        row.xp = bot->GetUInt32Value(PLAYER_XP);
        row.nextXp = row.level >= maxLevel ? 0 : bot->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
        row.zone = bot->GetZoneId();
        row.talents = BotShared::TalentSpread::Of(bot);
        row.state = info.parted ? 3 : player->GetGroup() && bot->GetGroup() == player->GetGroup() ? 2 : 1;
        row.name = bot->GetName();
        for (std::unique_ptr<Companions::Talk::Service> const& service : Companions::Talk::Services())
            service->Blockers(bot, BotShared::I18n::Of(player), row.blockers);
    }

    if (!offline.empty())
    {
        std::unique_ptr<QueryResult> chars(CharacterDatabase.PQuery(
            "SELECT `guid`, `name`, `race`, `class`, `gender`, `level`, `xp`, `zone` FROM `characters` WHERE `guid` IN (%s)", offline.c_str()));
        if (chars)
        {
            do
            {
                Field* f = chars->Fetch();
                Row& row = rows[f[0].GetUInt32()];
                row.known = true;
                row.name = f[1].GetCppString();
                row.race = f[2].GetUInt8();
                row.cls = f[3].GetUInt8();
                row.gender = f[4].GetUInt8();
                row.level = f[5].GetUInt8();
                row.xp = f[6].GetUInt32();
                row.nextXp = row.level >= maxLevel ? 0 : sObjectMgr.GetXPForLevel(row.level);
                row.zone = f[7].GetUInt32();
            } while (chars->NextRow());
        }

        std::unordered_map<uint32, std::unordered_set<uint32>> spells;
        std::unique_ptr<QueryResult> learned(CharacterDatabase.PQuery(
            "SELECT `guid`, `spell` FROM `character_spell` WHERE `disabled` = 0 AND `guid` IN (%s)", offline.c_str()));
        if (learned)
        {
            do
            {
                Field* f = learned->Fetch();
                spells[f[0].GetUInt32()].insert(f[1].GetUInt32());
            } while (learned->NextRow());
        }
        for (auto& itr : rows)
            if (!itr.second.online && itr.second.known)
                itr.second.talents = BotShared::TalentSpread::Of(uint8(itr.second.cls), spells[itr.first]);
    }

    uint32 count = 0;
    for (auto const& itr : rows)
        count += itr.second.known ? 1 : 0;
    PSendSysMessage("PBCMP;BEGIN;%u", count);
    Companions::CompanionMgr::SlotCount const slots = sCompanionMgr.SlotsOf(player);
    std::string freeLevels;
    for (uint32 level : Companions::Slots::FreeLevels())
        freeLevels += (freeLevels.empty() ? "" : ",") + std::to_string(level);
    PSendSysMessage("PBCMP;SLOTS;%u;%u;%u;%u;%u;%u;%s", slots.used, slots.total, Companions::Slots::Max(),
        slots.nextFreeLevel, slots.canBuy ? 1 : 0, slots.price, freeLevels.c_str());
    BotShared::I18n::Lang const lang = BotShared::I18n::Of(player);
    for (Companions::Gates::Info const& gate : Companions::Gates::All())
        PSendSysMessage("PBCMP;GATE;%u;%u;%s;%u;%s", gate.affinity, gate.classMask, gate.key, gate.ready ? 1u : 0u,
            Companions::Gates::Label(gate, lang).c_str());

    for (Companions::CompanionInfo const& info : companions)
    {
        Row const& row = rows[info.charGuid];
        if (!row.known)
            continue;
        std::string tags;
        for (std::string const& tag : info.tags)
            tags += (tags.empty() ? "" : ",") + tag;
        std::ostringstream ss;
        ss << "PBCMP;C;" << info.charGuid << ";" << row.race << ";" << row.cls << ";" << row.gender << ";"
           << row.level << ";" << row.xp << ";" << row.nextXp << ";"
           << row.talents[0] << "/" << row.talents[1] << "/" << row.talents[2] << ";"
           << info.affinity << ";" << row.state << ";" << row.zone << ";" << info.recruitedAt << ";"
           << tags << ";" << row.name;
        SendSysMessage(ss.str().c_str());
        auto const kind = accountKinds.find(info.charGuid);
        if (kind != accountKinds.end())
            PSendSysMessage("PBCMP;ACC;%u;%u", info.charGuid, kind->second);
        for (Companions::Talk::Blocker const& b : row.blockers)
            PSendSysMessage("PBCMP;BLOCK;%u;%s;%s", info.charGuid, b.gateKey.c_str(), b.text.c_str());
        Companions::TraitPair const traits = sCompanionMgr.GetTraits(info.charGuid);
        for (Companions::Traits::Trait trait : { traits.life, traits.combat })
            if (Companions::Traits::Info const* t = Companions::Traits::Of(trait))
                PSendSysMessage("PBCMP;TRAIT;%u;%u;%s;%u;%s;%s", info.charGuid, Companions::Traits::TierAt(t->tier), t->key,
                    uint32(t->rarity), Companions::Traits::Label(*t, lang).c_str(), Companions::Traits::TextFor(*t, info.charGuid, lang).c_str());
    }

    SendSysMessage("PBCMP;END");
    return true;
}

// .partybot callover <guid> [role]: one of your companions (or of your account's characters)
// comes to you (the companions panel); role: auto, tank, healer, meleedps, rangedps (default auto).
// PBOK;<name>;callover | PBERR;<name>;<notcompanion|notyours|online|full|failed|usage>
bool ChatHandler::HandlePartyBotCallOverCommand(char* args)
{
    Player* player = GetSession() ? GetSession()->GetPlayer() : nullptr;
    uint32 guid = 0;
    if (!player || !ExtractUInt32(&args, guid))
    {
        SendSysMessage("PBERR;;usage;.partybot callover <guid> [role]");
        SetSentErrorMessage(true);
        return false;
    }
    std::string name;
    sObjectMgr.GetPlayerNameByGUID(ObjectGuid(HIGHGUID_PLAYER, guid), name);
    CombatBotRoles role = ROLE_INVALID;
    if (char* roleArg = ExtractLiteralArg(&args))
        if (!ParsePartyBotRole(roleArg, sObjectMgr.GetPlayerClassByGUID(ObjectGuid(HIGHGUID_PLAYER, guid)), role))
        {
            PSendSysMessage("PBERR;%s;usage", name.c_str());
            SetSentErrorMessage(true);
            return false;
        }
    std::string const error = sCompanionMgr.CallOver(player, guid, uint8(role), true);
    if (!error.empty())
    {
        PSendSysMessage("PBERR;%s;%s", name.c_str(), error.c_str());
        SetSentErrorMessage(true);
        return false;
    }
    PSendSysMessage("PBOK;%s;callover", name.c_str());
    return true;
}

// `.partybot <farewell|rehire|forget> <guid>`: the player, the companion's guid and name. False:
// usage sent.
bool ChatHandler::ExtractCompanionSlotTarget(char** args, char const* usage, Player*& player, uint32& guid, std::string& name)
{
    player = GetSession() ? GetSession()->GetPlayer() : nullptr;
    if (!player || !ExtractUInt32(args, guid))
    {
        PSendSysMessage("PBERR;;usage;%s", usage);
        SetSentErrorMessage(true);
        return false;
    }
    sObjectMgr.GetPlayerNameByGUID(ObjectGuid(HIGHGUID_PLAYER, guid), name);
    return true;
}

// .partybot farewell <guid>: PBOK;<name>;farewell | PBERR;<name>;<notcompanion|notyours|former>
bool ChatHandler::HandlePartyBotFarewellCommand(char* args)
{
    Player* player = nullptr;
    uint32 guid = 0;
    std::string name;
    if (!ExtractCompanionSlotTarget(&args, ".partybot farewell <guid>", player, guid, name))
        return false;
    std::string const error = sCompanionMgr.Part(player, guid);
    if (!error.empty())
    {
        PSendSysMessage("PBERR;%s;%s", name.c_str(), error.c_str());
        SetSentErrorMessage(true);
        return false;
    }
    PSendSysMessage("PBOK;%s;farewell", name.c_str());
    return true;
}

// .partybot rehire <guid>: PBOK;<name>;rehire;<1 on his way, 0 on the bench (cannot come now)>
//                          | PBERR;<name>;<notcompanion|notyours|notformer|noslot>
bool ChatHandler::HandlePartyBotRehireCommand(char* args)
{
    Player* player = nullptr;
    uint32 guid = 0;
    std::string name;
    if (!ExtractCompanionSlotTarget(&args, ".partybot rehire <guid>", player, guid, name))
        return false;
    std::string const error = sCompanionMgr.Rehire(player, guid);
    if (!error.empty())
    {
        PSendSysMessage("PBERR;%s;%s", name.c_str(), error.c_str());
        SetSentErrorMessage(true);
        return false;
    }
    bool const coming = sCompanionMgr.CallOver(player, guid, ROLE_INVALID, true).empty();
    PSendSysMessage("PBOK;%s;rehire;%u", name.c_str(), coming ? 1 : 0);
    return true;
}

// .partybot forget <guid>: PBOK;<name>;forget | PBERR;<name>;<notcompanion|notyours|notformer|leaving>
bool ChatHandler::HandlePartyBotForgetCommand(char* args)
{
    Player* player = nullptr;
    uint32 guid = 0;
    std::string name;
    if (!ExtractCompanionSlotTarget(&args, ".partybot forget <guid>", player, guid, name))
        return false;
    uint32 memberId = 0, level = 0;
    std::string const error = sCompanionMgr.Forget(player, guid, memberId, level);
    if (!error.empty())
    {
        PSendSysMessage("PBERR;%s;%s", name.c_str(), error.c_str());
        SetSentErrorMessage(true);
        return false;
    }
    if (memberId)
        sPopulationMgr.ReturnToRoster(memberId, uint8(std::min<uint32>(level, 255)));
    PSendSysMessage("PBOK;%s;forget", name.c_str());
    return true;
}

// .partybot joinaccount <guid>: PBOK;<name>;joinaccount
//                               | PBERR;<name>;<notcompanion|notyours|inaccount|locked|accountfull>
// (online and in your group he says his line, as after the talk window's yes)
bool ChatHandler::HandlePartyBotJoinAccountCommand(char* args)
{
    Player* player = nullptr;
    uint32 guid = 0;
    std::string name;
    if (!ExtractCompanionSlotTarget(&args, ".partybot joinaccount <guid>", player, guid, name))
        return false;
    std::string const error = sCompanionMgr.JoinAccount(player, guid);
    if (!error.empty())
    {
        PSendSysMessage("PBERR;%s;%s", name.c_str(), error.c_str());
        SetSentErrorMessage(true);
        return false;
    }
    Player* bot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, guid));
    if (bot && bot->IsInWorld() && bot->GetGroup() && bot->GetGroup() == player->GetGroup())
    {
        std::string const line = Companions::Talk::Line(Companions::Talk::ContextOf(player, bot), "account.join.done");
        if (!line.empty())
            BotShared::BotChat::SayToParty(bot, line);
    }
    PSendSysMessage("PBOK;%s;joinaccount", name.c_str());
    return true;
}

// .partybot sendback <guid>: PBOK;<name>;sendback;<1 a companion again, 0 a former one (no free slot)>
//                            | PBERR;<name>;<notcompanion|notyours|notinaccount|self|failed>
bool ChatHandler::HandlePartyBotSendBackCommand(char* args)
{
    Player* player = nullptr;
    uint32 guid = 0;
    std::string name;
    if (!ExtractCompanionSlotTarget(&args, ".partybot sendback <guid>", player, guid, name))
        return false;
    bool parted = false;
    std::string const error = sCompanionMgr.SendBack(player, guid, parted);
    if (!error.empty())
    {
        PSendSysMessage("PBERR;%s;%s", name.c_str(), error.c_str());
        SetSentErrorMessage(true);
        return false;
    }
    PSendSysMessage("PBOK;%s;sendback;%u", name.c_str(), parted ? 0 : 1);
    return true;
}

// .partybot buyslot: PBOK;;buyslot;<total now>;<copper paid> | PBERR;;<notyet|maxslots|nomoney>
bool ChatHandler::HandlePartyBotBuySlotCommand(char* /*args*/)
{
    Player* player = GetSession() ? GetSession()->GetPlayer() : nullptr;
    if (!player)
        return false;
    uint32 const price = sCompanionMgr.SlotsOf(player).price;
    std::string const error = sCompanionMgr.BuySlot(player);
    if (!error.empty())
    {
        PSendSysMessage("PBERR;;%s", error.c_str());
        SetSentErrorMessage(true);
        return false;
    }
    PSendSysMessage("PBOK;;buyslot;%u;%u", sCompanionMgr.SlotsOf(player).total, price);
    return true;
}

bool ChatHandler::HandlePartyBotReplyCommand(char* args)
{
    uint32 id = 0;
    char* option = nullptr;
    if (!ExtractUInt32(&args, id) || !(option = ExtractLiteralArg(&args)))
    {
        SendSysMessage("PBERR;;usage;.partybot reply <id> <option>");
        SetSentErrorMessage(true);
        return false;
    }

    std::string error;
    if (!sRequestBoard.Reply(GetSession() ? GetSession()->GetPlayer() : nullptr, id, option, error))
    {
        PSendSysMessage("PBERR;;%s;%u", error.c_str(), id);
        SetSentErrorMessage(true);
        return false;
    }
    PSendSysMessage("PBOK;;reply;%u;%s", id, option);
    return true;
}

bool ChatHandler::HandlePartyBotRewardModeCommand(char* args)
{
    std::string name;
    Player* pBot = ExtractPartyBotTarget(&args, name);
    if (!pBot)
        return false;

    if (!sCompanionMgr.IsCompanion(pBot->GetGUIDLow()))
    {
        PSendSysMessage("PBERR;%s;notcompanion", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    char* mode = ExtractLiteralArg(&args);
    Companions::RewardMode value;
    if (mode && strcmp(mode, "auto") == 0)
        value = Companions::RewardMode::Auto;
    else if (mode && strcmp(mode, "ask") == 0)
        value = Companions::RewardMode::AskPlayer;
    else
    {
        PSendSysMessage("PBERR;%s;usage;.partybot rewardmode <name> auto|ask", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    sCompanionMgr.SetRewardMode(pBot->GetGUIDLow(), value);
    PSendSysMessage("PBOK;%s;rewardmode;%s", name.c_str(), mode);
    return true;
}

bool ChatHandler::HandlePartyBotGateCommand(char* args)
{
    Player* player = GetSession() ? GetSession()->GetPlayer() : nullptr;
    char* action = ExtractLiteralArg(&args);
    if (!player || !action)
    {
        SendSysMessage("PBERR;;usage;.partybot gate add [name] | remove | list");
        SetSentErrorMessage(true);
        return false;
    }

    if (strcmp(action, "add") == 0)
    {
        std::string name = args ? args : "";
        while (!name.empty() && name.front() == ' ')
            name.erase(name.begin());
        uint32 const id = BotShared::TownGates::Add(player->GetMapId(), player->GetZoneId(),
            player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), name);
        if (!id)
        {
            SendSysMessage("PBERR;;other");
            SetSentErrorMessage(true);
            return false;
        }
        PSendSysMessage("PBOK;;gate;add;%u;%s", id, name.c_str());
        return true;
    }

    if (strcmp(action, "remove") == 0)
    {
        BotShared::TownGates::Gate removed;
        if (!BotShared::TownGates::RemoveNear(player->GetMapId(), player->GetPositionX(), player->GetPositionY(), 30.0f, removed))
        {
            SendSysMessage("PBERR;;notfound");
            SetSentErrorMessage(true);
            return false;
        }
        PSendSysMessage("PBOK;;gate;remove;%u;%s", removed.id, removed.name.c_str());
        return true;
    }

    if (strcmp(action, "list") == 0)
    {
        std::vector<BotShared::TownGates::Gate> const gates = BotShared::TownGates::OfZone(player->GetMapId(), player->GetZoneId());
        PSendSysMessage("Town gates in this zone: %u", uint32(gates.size()));
        for (BotShared::TownGates::Gate const& g : gates)
            PSendSysMessage("  #%u %s (%.1f, %.1f, %.1f), %.0f yd", g.id, g.name.c_str(), g.x, g.y, g.z,
                player->GetDistance2d(g.x, g.y));
        return true;
    }

    SendSysMessage("PBERR;;usage;.partybot gate add [name] | remove | list");
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandlePartyBotMirrorCommand(char* args)
{
    Player* player = GetSession() ? GetSession()->GetPlayer() : nullptr;
    char* action = ExtractLiteralArg(&args);
    namespace Mirror = BotShared::QuestMirror;

    // follow <bot> <questId> / unfollow <bot>: the leader's mirror of a bot's quest (MirrorSync)
    if (player && action && (strcmp(action, "follow") == 0 || strcmp(action, "unfollow") == 0))
    {
        std::string name;
        char* botName = ExtractArg(&args);
        Player* pBot = ExtractPartyBotTarget(&botName, name);
        if (!pBot)
            return false;
        PartyBotAI* ai = dynamic_cast<PartyBotAI*>(pBot->AI());
        Companions::MirrorSync::Stop(ai->GetPartyLeader(), ai->m_mirrorLink);
        if (strcmp(action, "unfollow") == 0)
        {
            PSendSysMessage("PBOK;%s;mirror;unfollow", name.c_str());
            return true;
        }
        uint32 questId = 0;
        if (!ExtractUInt32(&args, questId) || !Mirror::HasMirror(questId))
        {
            PSendSysMessage("PBERR;%s;nomirror;%u", name.c_str(), questId);
            SetSentErrorMessage(true);
            return false;
        }
        Companions::MirrorSync::Follow(ai->m_mirrorLink, questId);
        PSendSysMessage("PBOK;%s;mirror;follow;%u", name.c_str(), questId);
        return true;
    }

    uint32 id = 0;
    if (!player || !action || !ExtractUInt32(&args, id))
    {
        SendSysMessage("PBERR;;usage;.partybot mirror show|remove <id> | count <id> <objective 1-4> <n> | follow <bot> <questId> | unfollow <bot>");
        SetSentErrorMessage(true);
        return false;
    }

    if (strcmp(action, "show") == 0)
    {
        switch (Mirror::Show(player, id))
        {
            case Mirror::ShowResult::Shown:        PSendSysMessage("PBOK;;mirror;show;%u", id); return true;
            case Mirror::ShowResult::AlreadyShown: PSendSysMessage("PBOK;;mirror;already;%u", id); return true;
            case Mirror::ShowResult::LogFull:      PSendSysMessage("PBERR;;logfull;%u", id); break;
            case Mirror::ShowResult::NoQuest:      PSendSysMessage("PBERR;;notfound;%u", id); break;
        }
        SetSentErrorMessage(true);
        return false;
    }

    if (strcmp(action, "count") == 0)
    {
        uint32 objective = 0, count = 0;
        if (!ExtractUInt32(&args, objective) || !ExtractUInt32(&args, count) || objective < 1 ||
            !Mirror::SetCount(player, id, objective - 1, count))
        {
            PSendSysMessage("PBERR;;mirror;count;%u", id);
            SetSentErrorMessage(true);
            return false;
        }
        PSendSysMessage("PBOK;;mirror;count;%u;%u;%u", id, objective, count);
        return true;
    }

    if (strcmp(action, "remove") == 0)
    {
        if (!Mirror::Remove(player, id))
        {
            PSendSysMessage("PBERR;;notfound;%u", id);
            SetSentErrorMessage(true);
            return false;
        }
        PSendSysMessage("PBOK;;mirror;remove;%u", id);
        return true;
    }

    SendSysMessage("PBERR;;usage;.partybot mirror show <id> | count <id> <objective 1-4> <n> | remove <id>");
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandlePartyBotClassQuestsCommand(char* args)
{
    namespace Chains = BotShared::ClassQuestChains;
    auto line = [this](Chains::Chain const& chain, char const* state)
    {
        Quest const* last = sObjectMgr.GetQuestTemplate(chain.id);
        std::string steps;
        for (uint32 id : chain.quests)
            steps += (steps.empty() ? "" : ">") + std::to_string(id);
        PSendSysMessage("  %u %s aff %u lvl %u races %u [%s] %s%s%s %s", chain.id,
            chain.kind == Chains::Kind::Ability ? "ability" : "gear", chain.affinity, chain.minLevel, chain.raceMask,
            steps.c_str(), last ? last->GetTitle().c_str() : "?", chain.comment.empty() ? "" : " - ", chain.comment.c_str(), state);
    };

    if (args && strncmp(args, "class", 5) == 0)
    {
        char* word = ExtractLiteralArg(&args);
        uint32 classId = 0;
        if (!word || !ExtractUInt32(&args, classId) || !classId)
        {
            SendSysMessage("PBERR;;usage;.partybot classquests [bot] | class <classId>");
            SetSentErrorMessage(true);
            return false;
        }
        uint32 count = 0;
        for (Chains::Chain const& chain : Chains::All())
            if (chain.classMask & (1 << (classId - 1)))
            {
                line(chain, "");
                ++count;
            }
        PSendSysMessage("%u chains of class %u.", count, classId);
        return true;
    }

    // visit <bot>: a capital visit now, wherever the leader is (the next ask comes at once)
    bool const visit = args && strncmp(args, "visit", 5) == 0;
    if (visit)
        ExtractLiteralArg(&args);

    std::string name;
    Player* pBot = ExtractPartyBotTarget(&args, name);
    if (!pBot)
        return false;
    PartyBotAI* ai = dynamic_cast<PartyBotAI*>(pBot->AI());
    namespace Flow = Companions::ClassQuestFlow;
    if (visit)
    {
        ai->m_classQuestFlow.forceVisit = true;
        ai->m_classQuestFlow.askedThisVisit = false;
        PSendSysMessage("PBOK;%s;classquests;visit", name.c_str());
        return true;
    }

    std::vector<Chains::Chain const*> const chains = Chains::ChainsFor(pBot);
    for (Chains::Chain const* chain : chains)
    {
        Flow::Record const r = Flow::RecordOf(ai->m_classQuestFlow, chain->id);
        std::string const state = std::string(Chains::IsDone(pBot, *chain) ? "(done" :
            pBot->GetLevel() >= chain->minLevel ? "(to do" : "(later") + ", " + Flow::StateName(r.state) +
            ", asks " + std::to_string(r.asksLeft) + (r.declined ? ", declined" : "") + (r.helped ? ", helped" : "") + ")";
        line(*chain, state.c_str());
    }
    Flow::State const& flow = ai->m_classQuestFlow;
    PSendSysMessage("%u chains for %s, affinity %u. Flow: loaded %u, leader in capital %u, asked this visit %u, request #%u (chain %u).",
        uint32(chains.size()), name.c_str(), sCompanionMgr.GetAffinity(pBot->GetGUIDLow()),
        uint32(flow.loaded), uint32(flow.leaderInCapital), uint32(flow.askedThisVisit), flow.requestId, flow.askingChain);
    return true;
}
