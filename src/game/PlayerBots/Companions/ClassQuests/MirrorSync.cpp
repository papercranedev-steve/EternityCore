#include "Companions/ClassQuests/MirrorSync.h"
#include "Shared/QuestMirror.h"
#include "Shared/QuestPlaces.h"
#include "Shared/ClassQuestChains.h"
#include "Chat.h"
#include "WorldSession.h"
#include <algorithm>
#include "Player.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Timer.h"

namespace Companions
{
    namespace MirrorSync
    {
        namespace
        {
            uint32 const CHECK_MS = 1000;
            // The flag's target is the nearest spawn from where the leader stood; after walking
            // this far it is picked again (a kill objective has spawns all over a zone).
            float const REPICK_DISTANCE = 150.0f;

            namespace Mirror = BotShared::QuestMirror;
            namespace Places = BotShared::QuestPlaces;

            // Where the leader should go now for the bot's quest (see the header).
            bool WhereToGo(Player const* bot, Player const* leader, Quest const* quest, bool inLog, Places::Place& out)
            {
                uint32 const mapId = leader->GetMapId();
                float const x = leader->GetPositionX();
                float const y = leader->GetPositionY();
                if (!inLog)
                    return Places::NearestGiver(quest, mapId, x, y, out);
                if (bot->GetQuestStatus(quest->GetQuestId()) == QUEST_STATUS_COMPLETE)
                    return Places::NearestEnder(quest, mapId, x, y, out);
                for (Mirror::Counter const& counter : Mirror::CountersOf(quest))
                    if (Mirror::Progress(bot, quest, counter) < counter.needed && Places::NearestFor(quest, counter, mapId, x, y, out))
                        return true;
                return false;
            }

            // Puts the flag on the leader's map when its target changed. Nothing to go to (on
            // this map): the old flag stays, there is no packet to take it away.
            void UpdateFlag(Player* bot, Player* leader, Link& link, Quest const* quest, bool inLog)
            {
                Places::Place place;
                if (!WhereToGo(bot, leader, quest, inLog, place))
                {
                    link.flagEntry = 0;
                    return;
                }
                float const fromX = leader->GetPositionX();
                float const fromY = leader->GetPositionY();
                bool const sameTarget = link.flagEntry == place.entry && link.flagGameObject == place.gameObject &&
                    link.flagMapId == leader->GetMapId();
                if (sameTarget)
                {
                    float const dx = fromX - link.flagFromX;
                    float const dy = fromY - link.flagFromY;
                    if (dx * dx + dy * dy < REPICK_DISTANCE * REPICK_DISTANCE)
                        return;
                    link.flagFromX = fromX;
                    link.flagFromY = fromY;
                    if (place.x == link.flagX && place.y == link.flagY)
                        return;
                }
                link.flagEntry = place.entry;
                link.flagGameObject = place.gameObject;
                link.flagMapId = leader->GetMapId();
                link.flagX = place.x;
                link.flagY = place.y;
                link.flagFromX = fromX;
                link.flagFromY = fromY;
                Mirror::ShowPoint(leader, place.x, place.y, Places::NameOf(place, leader->GetSession()->GetSessionDbLocaleIndex()));
            }

            // The next quest of the chain the bot can do, nullptr when the chain ends here.
            Quest const* NextStep(Player const* bot, Quest const* quest, uint32 chainId)
            {
                if (BotShared::ClassQuestChains::Chain const* chain = chainId ? BotShared::ClassQuestChains::Find(chainId) : nullptr)
                {
                    std::vector<Quest const*> const steps = BotShared::ClassQuestChains::StepsFor(bot, *chain);
                    auto const at = std::find(steps.begin(), steps.end(), quest);
                    if (at == steps.end() || at + 1 == steps.end() || !Mirror::HasMirror((*(at + 1))->GetQuestId()))
                        return nullptr;
                    return *(at + 1);
                }
                Quest const* next = sObjectMgr.GetQuestTemplate(quest->GetNextQuestInChain());
                if (!next || !Mirror::HasMirror(next->GetQuestId()))
                    return nullptr;
                if (!bot->SatisfyQuestClass(next, false) || !bot->SatisfyQuestRace(next, false))
                    return nullptr;
                return next;
            }
        }

        void Follow(Link& link, uint32 questId, uint32 chainId)
        {
            link = Link();
            link.chainId = chainId;
            link.questId = questId;         // lastCheckMs 0: at the next update
        }

        void Stop(Player* leader, Link& link)
        {
            if (leader && link.shownId)
                Mirror::Remove(leader, link.shownId);
            link = Link();
        }

        Event Update(Player* bot, Player* leader, Link& link)
        {
            if (!link.questId)
                return Event::None;
            uint32 const now = WorldTimer::getMSTime();
            if (link.lastCheckMs && WorldTimer::getMSTimeDiff(link.lastCheckMs, now) < CHECK_MS)
                return Event::None;
            link.lastCheckMs = now;
            // The leader's quest log is the leader's map's: only from the same map.
            if (!bot || !leader || !leader->IsInWorld() || leader->GetMap() != bot->GetMap())
                return Event::None;

            uint32 const questId = link.questId;
            Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
            if (!quest || !Mirror::HasMirror(questId))
            {
                Stop(leader, link);
                return Event::None;
            }

            if (link.shownId && !Mirror::IsShown(leader, link.shownId))
            {
                link = Link();
                return Event::LeaderAbandoned;
            }

            if (bot->GetQuestRewardStatus(questId))
            {
                if (link.shownId)
                    Mirror::Remove(leader, link.shownId);
                link.shownId = 0;
                if (Quest const* next = NextStep(bot, quest, link.chainId))
                {
                    link.questId = next->GetQuestId();
                    link.lastCheckMs = 0;
                    return Event::StepDone;
                }
                link = Link();
                return Event::ChainDone;
            }

            bool const inLog = bot->IsCurrentQuest(questId);
            uint32 const mirrorId = Mirror::MirrorId(questId);
            if (!inLog && link.shownId == mirrorId)
            {
                Stop(leader, link);
                return Event::BotAbandoned;
            }

            uint32 const wanted = inLog ? mirrorId : Mirror::PlaceholderId(questId);
            if (link.shownId != wanted)
            {
                if (link.shownId)
                    Mirror::Remove(leader, link.shownId);
                link.shownId = 0;
                switch (Mirror::Show(leader, wanted))
                {
                    case Mirror::ShowResult::Shown:
                        // Put in the log by the server, so the client plays no accept sound: the addon does.
                        ChatHandler(leader).PSendSysMessage("PBQUEST;%s;mirrorshown;%u", bot->GetName(), wanted);
                        // (and on: shown now)
                    case Mirror::ShowResult::AlreadyShown:
                        link.shownId = wanted;
                        link.logFullSaid = false;
                        break;
                    case Mirror::ShowResult::LogFull:
                        // Said once until it fits; the addon tells the player to make room.
                        if (!link.logFullSaid)
                            ChatHandler(leader).PSendSysMessage("PBQUEST;%s;mirrorfull;%u;%s", bot->GetName(), questId, quest->GetTitle().c_str());
                        link.logFullSaid = true;
                        return Event::None;
                    case Mirror::ShowResult::NoQuest:
                        link = Link();
                        return Event::None;
                }
            }

            if (inLog)
            {
                std::vector<Mirror::Counter> const counters = Mirror::CountersOf(quest);
                for (uint32 i = 0; i < counters.size(); ++i)
                    Mirror::SetCount(leader, mirrorId, i, Mirror::Progress(bot, quest, counters[i]));
            }
            UpdateFlag(bot, leader, link, quest, inLog);
            return Event::None;
        }
    }
}
