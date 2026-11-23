/*
 * Talent commands used by the PartyBot addon's talent panel (addons/PartyBot/Talents.lua).
 *
 *   .partybot talents [name]                     list the bot's talents
 *   .partybot learntalent <name> <talentId>[x<count>] ...
 *                                                spend free points, same rules as a player, in the
 *                                                given order (the panel sends the player's whole
 *                                                choice when it closes; there is no way back)
 *   .partybot talentdebug <name> reset           free reset of all talents
 *   .partybot talentdebug <name> unlearn <id>    remove one rank (refused if the tree would break)
 *   .partybot talentdebug <name> autospec        premade spec template of the bot's role
 *                                                (talentdebug needs PartyBot.TalentDebug = 1; GM
 *                                                only, the addon has no button for it)
 *
 * Replies (system messages, ';' separated):
 *   PBTAL;<name>;BEGIN;<class>;<level>;<free points>;<debug 0/1>
 *   PBTAL;<name>;T;<tab 0-2>;<talentId>;<row>;<col>;<rank>;<max rank>;<depends on>;<depends on rank>;<spell rank 1>,<spell rank 2>,...
 *   PBTAL;<name>;END
 *   PBOK;<name>;talent;learn;<points learned>   PBOK;<name>;talent;unlearn;<talentId>;<new rank>
 *   PBOK;<name>;talent;reset
 *   PBOK;<name>;talent;autospec;<template name | random>
 *   PBERR;<name>;<code>   codes: usage nodebug notalent nopoints maxrank talentreq dependent tier
 * Every change is followed by the new PBTAL listing.
 */

#include "Chat.h"
#include "Player.h"
#include "TeamBotAI.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "World.h"
#include "DBCStores.h"
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    // the talent if it belongs to the player's class, with its tab
    TalentEntry const* ClassTalent(Player const* pPlayer, uint32 talentId, TalentTabEntry const** pTab = nullptr)
    {
        TalentEntry const* pTalent = sTalentStore.LookupEntry(talentId);
        if (!pTalent)
            return nullptr;
        TalentTabEntry const* pTabInfo = sTalentTabStore.LookupEntry(pTalent->TalentTab);
        if (!pTabInfo || !(pPlayer->GetClassMask() & pTabInfo->ClassMask))
            return nullptr;
        if (pTab)
            *pTab = pTabInfo;
        return pTalent;
    }

    // learned ranks, 0 = not learned
    uint32 CurrentRank(Player const* pPlayer, TalentEntry const* pTalent)
    {
        for (int32 k = MAX_TALENT_RANK - 1; k >= 0; --k)
            if (pTalent->RankID[k] && pPlayer->HasSpell(pTalent->RankID[k]))
                return uint32(k + 1);
        return 0;
    }

    uint32 MaxRank(TalentEntry const* pTalent)
    {
        uint32 ranks = 0;
        while (ranks < MAX_TALENT_RANK && pTalent->RankID[ranks])
            ++ranks;
        return ranks;
    }

    // all talents of one talent tab
    std::vector<TalentEntry const*> TabTalents(uint32 talentTab)
    {
        std::vector<TalentEntry const*> talents;
        for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
            if (TalentEntry const* pTalent = sTalentStore.LookupEntry(i))
                if (pTalent->TalentTab == talentTab)
                    talents.push_back(pTalent);
        return talents;
    }

    // Why one rank of the talent can't be removed (nullptr = it can): a learned talent depends on
    // it, or a deeper row would lose the points needed above it (5 per row).
    char const* RemoveRankBlocker(Player const* pPlayer, TalentEntry const* pTalent, uint32 rank)
    {
        uint32 const newRank = rank - 1;
        std::vector<TalentEntry const*> const talents = TabTalents(pTalent->TalentTab);

        std::map<uint32 /*row*/, uint32 /*points*/> rowPoints;
        std::vector<std::pair<TalentEntry const*, uint32>> learned;
        for (TalentEntry const* pOther : talents)
        {
            uint32 const otherRank = pOther == pTalent ? newRank : CurrentRank(pPlayer, pOther);
            if (!otherRank)
                continue;
            if (pOther->DependsOn == pTalent->TalentID && newRank < pOther->DependsOnRank + 1)
                return "dependent";
            rowPoints[pOther->Row] += otherRank;
            learned.emplace_back(pOther, otherRank);
        }

        for (auto const& entry : learned)
        {
            uint32 above = 0;
            for (auto const& row : rowPoints)
                if (row.first < entry.first->Row)
                    above += row.second;
            if (above < entry.first->Row * MAX_TALENT_RANK)
                return "tier";
        }
        return nullptr;
    }

    void RemoveOneRank(Player* pPlayer, TalentEntry const* pTalent, uint32 rank)
    {
        // like Player::ResetTalents, for this talent only
        for (uint32 spellId : pTalent->RankID)
        {
            if (!spellId)
                continue;
            if (SpellEntry const* pSpell = sSpellMgr.GetSpellEntry(spellId))
                for (uint32 triggered : pSpell->EffectTriggerSpell)
                    if (triggered)
                        pPlayer->RemoveAurasDueToSpell(triggered);
            pPlayer->RemoveSpell(spellId, false, false);
        }
        if (rank >= 2)
            pPlayer->LearnSpell(pTalent->RankID[rank - 2], false, true);
    }

    // highest premade spec template the bot has the points for: its role first, else any role
    PlayerPremadeSpecTemplate const* PickSpecTemplate(Player const* pBot, CombatBotRoles role)
    {
        PlayerPremadeSpecTemplate const* pBest = nullptr;
        for (auto const& itr : sObjectMgr.GetPlayerPremadeSpecTemplates())
        {
            PlayerPremadeSpecTemplate const& spec = itr.second;
            if (spec.requiredClass != pBot->GetClass() || spec.level > pBot->GetLevel())
                continue;
            if (!pBest)
            {
                pBest = &spec;
                continue;
            }
            bool const roleFits = spec.role == role;
            bool const bestRoleFits = pBest->role == role;
            if (roleFits != bestRoleFits ? roleFits : spec.level > pBest->level)
                pBest = &spec;
        }
        return pBest;
    }

    std::vector<std::string> TalentListing(Player const* pBot, std::string const& name)
    {
        std::vector<std::string> lines;
        std::ostringstream begin;
        begin << "PBTAL;" << name << ";BEGIN;" << uint32(pBot->GetClass()) << ";" << uint32(pBot->GetLevel()) << ";"
              << pBot->GetFreeTalentPoints() << ";" << (sWorld.getConfig(CONFIG_BOOL_PARTY_BOT_TALENT_DEBUG) ? 1 : 0);
        lines.push_back(begin.str());

        for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
        {
            TalentTabEntry const* pTab = nullptr;
            TalentEntry const* pTalent = ClassTalent(pBot, i, &pTab);
            if (!pTalent)
                continue;

            std::ostringstream ss;
            ss << "PBTAL;" << name << ";T;" << pTab->tabpage << ";" << pTalent->TalentID << ";" << pTalent->Row << ";"
               << pTalent->Col << ";" << CurrentRank(pBot, pTalent) << ";" << MaxRank(pTalent) << ";"
               << pTalent->DependsOn << ";" << pTalent->DependsOnRank << ";";
            for (uint32 k = 0; k < MaxRank(pTalent); ++k)
                ss << (k ? "," : "") << pTalent->RankID[k];
            lines.push_back(ss.str());
        }

        lines.push_back("PBTAL;" + name + ";END");
        return lines;
    }

    // the bot uses its new spells from the next update on
    void TalentsChanged(Player* pBot)
    {
        if (TeamBotAI* pAI = dynamic_cast<TeamBotAI*>(pBot->AI()))
            pAI->m_resetSpellData = true;
    }
}

// .partybot talents [name]
bool ChatHandler::HandlePartyBotTalentsCommand(char* args)
{
    std::string name;
    Player* pBot = ExtractPartyBotTarget(&args, name);
    if (!pBot)
        return false;

    for (std::string const& line : TalentListing(pBot, name))
        SendSysMessage(line.c_str());
    return true;
}

namespace
{
    // one more rank of the talent, same rules as a player; nullptr = learned, else the error code
    char const* LearnOneRank(Player* pBot, uint32 talentId)
    {
        TalentEntry const* pTalent = ClassTalent(pBot, talentId);
        if (!pTalent)
            return "notalent";
        uint32 const rank = CurrentRank(pBot, pTalent);
        if (!pBot->GetFreeTalentPoints())
            return "nopoints";
        if (rank >= MaxRank(pTalent))
            return "maxrank";
        if (!pBot->LearnTalent(talentId, rank))     // tier or required talent missing
            return "talentreq";
        return nullptr;
    }

    // "<talentId>" or "<talentId>x<count>"
    bool ParseTalentToken(char const* token, uint32& talentId, uint32& count)
    {
        char* end = nullptr;
        talentId = uint32(strtoul(token, &end, 10));
        if (end == token)
            return false;
        count = 1;
        if (*end == 'x')
        {
            char const* countStart = end + 1;
            count = uint32(strtoul(countStart, &end, 10));
            if (end == countStart)
                return false;
        }
        return !*end && talentId && count;
    }
}

// .partybot learntalent <name> <talentId>[x<count>] ...
// Learns in the given order and stops at the first point that can't be learned.
bool ChatHandler::HandlePartyBotLearnTalentCommand(char* args)
{
    std::string name;
    Player* pBot = ExtractPartyBotTarget(&args, name);
    if (!pBot)
        return false;

    uint32 learned = 0;
    char const* error = nullptr;
    char* token = ExtractArg(&args);
    if (!token)
        error = "usage";
    for (; token && !error; token = ExtractArg(&args))
    {
        uint32 talentId = 0;
        uint32 count = 0;
        if (!ParseTalentToken(token, talentId, count))
        {
            error = "usage";
            break;
        }
        for (uint32 i = 0; i < count && !error; ++i)
        {
            error = LearnOneRank(pBot, talentId);
            if (!error)
                ++learned;
        }
    }

    if (learned)
    {
        TalentsChanged(pBot);
        PSendSysMessage("PBOK;%s;talent;learn;%u", name.c_str(), learned);
    }
    if (error)
    {
        if (!strcmp(error, "usage"))
            PSendSysMessage("PBERR;%s;usage;.partybot learntalent <name> <talentId>[x<count>] ...", name.c_str());
        else
            PSendSysMessage("PBERR;%s;%s", name.c_str(), error);
    }
    if (learned)
        for (std::string const& line : TalentListing(pBot, name))
            SendSysMessage(line.c_str());
    if (error)
    {
        SetSentErrorMessage(true);
        return false;
    }
    return true;
}

// .partybot talentdebug <name> reset | unlearn <talentId> | autospec
bool ChatHandler::HandlePartyBotTalentDebugCommand(char* args)
{
    std::string name;
    Player* pBot = ExtractPartyBotTarget(&args, name);
    if (!pBot)
        return false;

    if (!sWorld.getConfig(CONFIG_BOOL_PARTY_BOT_TALENT_DEBUG))
    {
        PSendSysMessage("PBERR;%s;nodebug", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    char* actionArg = ExtractArg(&args);
    std::string const action = actionArg ? actionArg : "";

    if (action == "reset")
    {
        pBot->ResetTalents(true);
        PSendSysMessage("PBOK;%s;talent;reset", name.c_str());
    }
    else if (action == "unlearn")
    {
        uint32 talentId = 0;
        TalentEntry const* pTalent = ExtractUInt32(&args, talentId) ? ClassTalent(pBot, talentId) : nullptr;
        uint32 const rank = pTalent ? CurrentRank(pBot, pTalent) : 0;
        char const* error = !pTalent ? "notalent" : !rank ? "notalent" : RemoveRankBlocker(pBot, pTalent, rank);
        if (error)
        {
            PSendSysMessage("PBERR;%s;%s", name.c_str(), error);
            SetSentErrorMessage(true);
            return false;
        }
        RemoveOneRank(pBot, pTalent, rank);
        PSendSysMessage("PBOK;%s;talent;unlearn;%u;%u", name.c_str(), talentId, rank - 1);
    }
    else if (action == "autospec")
    {
        TeamBotAI* pAI = static_cast<TeamBotAI*>(pBot->AI());
        std::string used = "random";
        if (PlayerPremadeSpecTemplate const* pSpec = PickSpecTemplate(pBot, pAI->m_role))
        {
            sObjectMgr.ApplyPremadeSpecTemplateToPlayer(pSpec->entry, pBot);     // resets talents first
            used = pSpec->name;
            for (char& c : used)
                if (c == ';')
                    c = ',';
        }
        else
        {
            pBot->ResetTalents(true);
            pAI->LearnRandomTalents();
        }
        PSendSysMessage("PBOK;%s;talent;autospec;%s", name.c_str(), used.c_str());
    }
    else
    {
        PSendSysMessage("PBERR;%s;usage;.partybot talentdebug <name> reset|unlearn <talentId>|autospec", name.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    TalentsChanged(pBot);
    for (std::string const& line : TalentListing(pBot, name))
        SendSysMessage(line.c_str());
    return true;
}
