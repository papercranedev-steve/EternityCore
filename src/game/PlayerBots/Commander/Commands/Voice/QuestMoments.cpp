/*
 * The party voice's quest moments (PartyVoice.h, user 2026-11-22): the party's progress on the
 * human's quests, so he need not keep opening his quest log. One line for the whole party per
 * change, with the human's own progress in it:
 *  - everyone holding the quest is done: "all done, back to <giver>" (Essential: said even to a
 *    quiet party);
 *  - the human is done and bots are not: the one bot says what it still needs, or the party
 *    names who is not done (Social);
 *  - the bots are done and the human is not: what he still needs (Social);
 *  - close to done (two or fewer left over the party, Flavor: mostly chatty parties).
 * Changes are gathered SETTLE_MS (kill credit reaches everyone at once, loot comes one by one)
 * and said out of a fight. Only the quests the human has; objectives a bot cannot get credit
 * for (talking to someone, an event, a spell on a creature) are not counted against the bots:
 * such a bot is neither done nor named.
 */

#include "Commander/Commands/Voice/PartyVoice.h"
#include "Commander/Perception/WorldModel.h"
#include "Shared/GameNames.h"
#include "Shared/I18n.h"
#include "Shared/Phrases.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        namespace Phrases = BotShared::Phrases;
        namespace I18n = BotShared::I18n;
        namespace GameNames = BotShared::GameNames;

        constexpr uint32 LOOK_MS = 1000;
        constexpr uint32 SETTLE_MS = 3000;
        constexpr uint32 ALMOST_LEFT = 2;
        constexpr uint32 ALL_DONE_ONCE_MS = 10 * MINUTE * IN_MILLISECONDS;
        constexpr uint32 LEFT_ONCE_MS = 3 * MINUTE * IN_MILLISECONDS;
        constexpr uint32 ALMOST_ONCE_MS = 5 * MINUTE * IN_MILLISECONDS;

        std::string Q(uint32 questId) { return std::to_string(questId); }

        // Counted progress of an objective: kills, objects, items.
        uint32 Have(Quest const* quest, QuestStatusData const& d)
        {
            uint32 have = 0;
            for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            {
                if (quest->ReqCreatureOrGOId[i])
                    have += std::min(d.m_creatureOrGOcount[i], quest->ReqCreatureOrGOCount[i]);
                if (quest->ReqItemId[i])
                    have += std::min(d.m_itemcount[i], quest->ReqItemCount[i]);
            }
            return have;
        }

        // What a member still needs, by counted objective. `forBot`: only what a bot gets credit
        // for (kills shared by the group, items), not spells on creatures.
        struct Part
        {
            std::string name;
            uint32 have = 0;
            uint32 need = 0;
        };
        std::vector<Part> Missing(Quest const* quest, QuestStatusData const& d, Player const* reader, bool forBot)
        {
            std::vector<Part> parts;
            QuestLocale const* locale = sObjectMgr.GetQuestLocale(quest->GetQuestId());
            int const idx = reader && reader->GetSession() ? reader->GetSession()->GetSessionDbLocaleIndex() : -1;
            for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            {
                int32 const target = quest->ReqCreatureOrGOId[i];
                if (target && d.m_creatureOrGOcount[i] < quest->ReqCreatureOrGOCount[i] && !(forBot && quest->ReqSpell[i]))
                {
                    Part p;
                    // The quest's own words for it first ("Wolves slain"), else the target's name.
                    if (locale && idx >= 0 && idx < int(locale->ObjectiveText[i].size()) && !locale->ObjectiveText[i][idx].empty())
                        p.name = locale->ObjectiveText[i][idx];
                    else if (!quest->ObjectiveText[i].empty())
                        p.name = quest->ObjectiveText[i];
                    else if (target > 0)
                        p.name = GameNames::CreatureName(uint32(target), reader);
                    else if (GameObjectInfo const* info = sObjectMgr.GetGameObjectTemplate(uint32(-target)))
                        p.name = info->name;
                    p.have = d.m_creatureOrGOcount[i];
                    p.need = quest->ReqCreatureOrGOCount[i];
                    parts.push_back(p);
                }
                uint32 const item = quest->ReqItemId[i];
                if (item && d.m_itemcount[i] < quest->ReqItemCount[i])
                {
                    Part p;
                    p.name = GameNames::ItemName(item, reader);
                    p.have = d.m_itemcount[i];
                    p.need = quest->ReqItemCount[i];
                    parts.push_back(p);
                }
            }
            return parts;
        }

        uint32 LeftCount(std::vector<Part> const& parts)
        {
            uint32 left = 0;
            for (Part const& p : parts)
                left += p.need - p.have;
            return left;
        }

        std::string Join(I18n::Lang lang, std::vector<std::string> const& words)
        {
            std::string const sep = Phrases::Word(lang, "voice.quest.sep");
            std::string out;
            for (std::string const& w : words)
                out += (out.empty() ? "" : sep) + w;
            return out;
        }

        std::string PartsText(I18n::Lang lang, std::vector<Part> const& parts)
        {
            std::vector<std::string> words;
            for (Part const& p : parts)
                words.push_back(Phrases::Text(lang, "voice.quest.part",
                    { { "name", p.name }, { "have", std::to_string(p.have) }, { "need", std::to_string(p.need) } }));
            return Join(lang, words);
        }

        // A creature that ends the quest (the first one), 0 = none (an object ends it).
        uint32 EnderOf(uint32 questId)
        {
            for (auto const& itr : sObjectMgr.GetCreatureQuestInvolvedRelationsMap())
                if (itr.second == questId)
                    return itr.first;
            return 0;
        }

        ObjectGuid AnyOf(std::vector<ObjectGuid> const& guids)
        {
            return guids.empty() ? ObjectGuid() : guids[urand(0, uint32(guids.size()) - 1)];
        }
    }

    void PartyVoice::QuestMoments(Map* map, WorldModel const& world, Player const* human, bool fighting, uint32 nowMs)
    {
        if (!human || nowMs < m_nextQuestLookMs)
            return;
        m_nextQuestLookMs = nowMs + LOOK_MS;

        // Look at every member's log; a change of a quest the human has is news.
        std::map<uint64, std::map<uint32, QuestSnap>> snaps;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet)
                continue;
            Player* p = map->GetPlayer(m.guid);
            if (!p)
                continue;
            std::map<uint32, QuestSnap>& now = snaps[m.guid.GetRawValue()];
            // (the status map holds the log and the rewarded quests: the log's are the current ones)
            for (auto const& entry : p->GetQuestStatusMap())
            {
                uint32 const questId = entry.first;
                Quest const* quest = p->IsCurrentQuest(questId) ? sObjectMgr.GetQuestTemplate(questId) : nullptr;
                if (!quest)
                    continue;
                QuestSnap s;
                s.complete = entry.second.m_status == QUEST_STATUS_COMPLETE;
                s.have = Have(quest, entry.second);
                now[questId] = s;
            }

            auto const before = m_questSnaps.find(m.guid.GetRawValue());
            if (before == m_questSnaps.end())
                continue;       // first look at this member: nothing to compare
            for (auto const& q : now)
            {
                auto const was = before->second.find(q.first);
                if (was == before->second.end())
                    continue;   // just taken
                bool const progressed = (q.second.complete && !was->second.complete) || q.second.have > was->second.have;
                if (progressed && human->IsCurrentQuest(q.first))
                    m_questChanged.emplace(q.first, nowMs);
            }
        }
        m_questSnaps.swap(snaps);

        if (fighting)
            return;
        for (auto itr = m_questChanged.begin(); itr != m_questChanged.end();)
        {
            if (WorldTimer::getMSTimeDiff(itr->second, nowMs) < SETTLE_MS)
            {
                ++itr;
                continue;
            }
            uint32 const questId = itr->first;
            itr = m_questChanged.erase(itr);
            QuestNews(map, world, human, questId, nowMs);
        }
    }

    void PartyVoice::QuestNews(Map* map, WorldModel const& world, Player const* human, uint32 questId, uint32 nowMs)
    {
        Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
        QuestStatusData const* humanData = human->GetQuestStatusData(questId);
        if (!quest || !humanData || !human->IsCurrentQuest(questId))
            return;
        I18n::Lang const lang = I18n::Of(human);
        bool const humanDone = humanData->m_status == QUEST_STATUS_COMPLETE;
        std::vector<Part> const humanParts = humanDone ? std::vector<Part>() : Missing(quest, *humanData, human, false);

        std::vector<ObjectGuid> doneBots, leftBots, allBots;
        std::vector<std::string> leftNames;
        std::vector<Part> soleParts;    // the one bot left: what it needs
        uint32 left = LeftCount(humanParts);
        bool unknown = !humanDone && humanParts.empty();    // the human's own objective is not counted
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.isBot || m.isPet)
                continue;
            Player const* p = map->GetPlayer(m.guid);
            QuestStatusData const* d = p && p->IsCurrentQuest(questId) ? p->GetQuestStatusData(questId) : nullptr;
            if (!d)
                continue;
            allBots.push_back(m.guid);
            if (d->m_status == QUEST_STATUS_COMPLETE)
            {
                doneBots.push_back(m.guid);
                continue;
            }
            std::vector<Part> const parts = Missing(quest, *d, human, true);
            if (parts.empty())
            {
                unknown = true;     // waits for something it gets no credit for
                continue;
            }
            leftBots.push_back(m.guid);
            leftNames.push_back(p->GetName());
            soleParts = parts;
            left += LeftCount(parts);
        }

        std::string const title = GameNames::QuestTitle(questId, human);
        std::string const once = Q(questId);

        // Everyone holding it is done.
        if (humanDone && leftBots.empty() && !unknown)
        {
            uint32 const ender = EnderOf(questId);
            std::string const giver = ender ? GameNames::CreatureName(ender, human) : std::string();
            std::vector<std::pair<std::string, std::string>> vars = { { "quest", title } };
            if (!giver.empty())
                vars.emplace_back("giver", giver);
            Say(giver.empty() ? "voice.quest.alldone.nogiver" : "voice.quest.alldone", AnyOf(allBots), VoiceTier::Essential,
                nowMs, vars, "quest.all:" + once, ALL_DONE_ONCE_MS);
            return;
        }

        // The human is done, bots are not.
        if (humanDone && !leftBots.empty())
        {
            if (leftBots.size() == 1)
                Say("voice.quest.self_left", leftBots.front(), VoiceTier::Social, nowMs,
                    { { "quest", title }, { "left", PartsText(lang, soleParts) } }, "quest.left:" + once, LEFT_ONCE_MS);
            else
                Say("voice.quest.bots_left", AnyOf(doneBots.empty() ? leftBots : doneBots), VoiceTier::Social, nowMs,
                    { { "quest", title }, { "names", Join(lang, leftNames) } }, "quest.left:" + once, LEFT_ONCE_MS);
            return;
        }

        // The bots are done, the human is not.
        if (!humanDone && !humanParts.empty() && !doneBots.empty() && leftBots.empty())
        {
            Say("voice.quest.player_left", AnyOf(doneBots), VoiceTier::Social, nowMs,
                { { "quest", title }, { "name", human->GetName() }, { "left", PartsText(lang, humanParts) } },
                "quest.left:" + once, LEFT_ONCE_MS);
            return;
        }

        // Close to done over the whole party.
        if (!unknown && left > 0 && left <= ALMOST_LEFT && !allBots.empty())
            Say("voice.quest.almost", ObjectGuid(), VoiceTier::Flavor, nowMs, { { "quest", title } },
                "quest.almost:" + once, ALMOST_ONCE_MS);
    }
}
