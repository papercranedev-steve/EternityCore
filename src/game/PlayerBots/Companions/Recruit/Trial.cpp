#include "Companions/Recruit/Trial.h"
#include "Companions/CompanionMgr.h"
#include "Shared/Phrases.h"
#include "Shared/BotChat.h"
#include "Player.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "QuestDef.h"
#include "Timer.h"
#include <algorithm>
#include <string>

namespace Companions
{
namespace Trial
{
    namespace
    {
        // His player: the recruiter, a real player of his group.
        Player* PlayerOf(Player* bot)
        {
            CompanionInfo const* info = sCompanionMgr.Find(bot->GetGUIDLow());
            if (!info || !info->trial)
                return nullptr;
            Player* player = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, info->recruiterGuid));
            if (!player || player->IsBot() || !player->IsInWorld() || !bot->GetGroup() || bot->GetGroup() != player->GetGroup())
                return nullptr;
            return player;
        }

        uint32 Now()
        {
            return std::max<uint32>(WorldTimer::getMSTime(), 1);
        }

        void Tell(Player* player, Player* bot, std::string const& what)
        {
            BotShared::BotChat::SystemTo(player, "PBTRIAL;" + std::string(bot->GetName()) + ";" + what);
        }

        // Says it if the line exists.
        void SayToParty(Player* bot, Talk::Context const& ctx, std::string const& key)
        {
            std::string const text = Talk::Line(ctx, key);
            if (!text.empty())
                BotShared::BotChat::SayToParty(bot, text);
        }
    }

    void OnQuestRewarded(Player* player, Quest const* quest)
    {
        Group* group = player ? player->GetGroup() : nullptr;
        if (!group || !quest)
            return;
        uint32 const questId = quest->GetQuestId();
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* bot = ref->getSource();
            if (!bot || bot == player || !bot->IsInWorld() || !sCompanionMgr.IsTrial(bot->GetGUIDLow()))
                continue;
            if (PlayerOf(bot) != player || !bot->IsInMap(player) || !bot->IsWithinDistInMap(player, TOGETHER_YD))
                continue;
            // (he turned it in already, or still has it in his log)
            if (!bot->GetQuestRewardStatus(questId) && bot->GetQuestStatus(questId) == QUEST_STATUS_NONE)
                continue;
            uint32 const done = sCompanionMgr.CountTrialQuest(bot->GetGUIDLow());
            if (!done)
                continue;
            Tell(player, bot, "quest;" + std::to_string(std::min(done, QUESTS_NEEDED)) + ";" + std::to_string(QUESTS_NEEDED));
            if (done == QUESTS_NEEDED)
                sCompanionMgr.ScheduleTrialOffer(bot->GetGUIDLow(), Now() + QUEST_OFFER_DELAY_MS);
        }
    }

    void OnDungeonEnd(Map* map)
    {
        if (!map)
            return;
        for (auto itr = map->GetPlayers().getFirst(); itr != nullptr; itr = itr->next())
        {
            Player* bot = itr->getSource();
            if (!bot || !bot->IsInWorld() || !sCompanionMgr.IsTrial(bot->GetGUIDLow()))
                continue;
            Player* player = PlayerOf(bot);
            if (!player || player->GetMap() != map)
                continue;
            sCompanionMgr.RaiseTrialQuests(bot->GetGUIDLow(), QUESTS_NEEDED);
            sCompanionMgr.ScheduleTrialOffer(bot->GetGUIDLow(), Now() + DUNGEON_OFFER_DELAY_MS);
        }
    }

    bool IsReady(uint32 charGuid)
    {
        return sCompanionMgr.IsTrial(charGuid) && sCompanionMgr.TrialQuests(charGuid) >= QUESTS_NEEDED;
    }

    Talk::Page OfferPage(Talk::Context const& ctx)
    {
        Talk::Page page;
        page.text = Talk::Line(ctx, "trial.offer.page");
        Talk::Option yes;
        yes.id = "bond:yes";
        yes.text = Talk::Line(ctx, "trial.offer.yes");
        Talk::Option later;
        later.id = "bond:later";
        later.text = Talk::Line(ctx, "trial.offer.later");
        page.options = { yes, later };
        return page;
    }

    void Update(Player* me, Player* leader, State& state, std::vector<std::string> const& /*tags*/)
    {
        if (!me || !sCompanionMgr.IsTrial(me->GetGUIDLow()))
            return;
        Player* player = PlayerOf(me);
        if (!player || player != leader)
            return;
        Talk::Context const ctx = Talk::ContextOf(player, me);

        if (!state.told)
        {
            state.told = true;
            SayToParty(me, ctx, "trial.start");
            Tell(player, me, "start;" + std::to_string(sCompanionMgr.TrialQuests(me->GetGUIDLow())) + ";" + std::to_string(QUESTS_NEEDED));
        }

        uint32 const now = Now();
        if (!sCompanionMgr.TakeTrialOffer(me->GetGUIDLow(), now))
            return;
        // Not while fighting or apart: a little later.
        if (me->IsInCombat() || player->IsInCombat() || !player->IsAlive() || !me->IsAlive() ||
            !me->IsWithinDistInMap(player, Talk::TALK_YD))
        {
            sCompanionMgr.ScheduleTrialOffer(me->GetGUIDLow(), now + 5 * IN_MILLISECONDS);
            return;
        }
        me->SetFacingToObject(player);
        me->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
        SayToParty(me, ctx, "trial.offer");
        Talk::SendPage(player, me, OfferPage(ctx));
    }

    bool Bond(Player* bot, Player* player)
    {
        uint32 const affinity = sCompanionMgr.Promote(bot);
        if (!affinity)
            return false;
        if (player)
        {
            sCompanionMgr.TellBond(player, bot);
            BotShared::BotChat::SystemTo(player, "PBAFF;" + std::string(bot->GetName()) + ";" + std::to_string(affinity) +
                ";" + std::to_string(affinity) + ";bond");
        }
        return true;
    }
}
}
