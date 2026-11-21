#include "Companions/Account/JoinOffer.h"
#include "Companions/CompanionMgr.h"
#include "Shared/BotChat.h"
#include "Player.h"
#include "Timer.h"
#include <algorithm>

namespace Companions
{
namespace JoinOffer
{
    namespace
    {
        uint32 Now()
        {
            return std::max<uint32>(WorldTimer::getMSTime(), 1);
        }

        void SayToParty(Player* bot, Talk::Context const& ctx, std::string const& key)
        {
            std::string const text = Talk::Line(ctx, key);
            if (!text.empty())
                BotShared::BotChat::SayToParty(bot, text);
        }
    }

    bool MayJoin(Player const* player, Player const* bot)
    {
        if (!player || !bot)
            return false;
        uint32 const guid = bot->GetGUIDLow();
        CompanionInfo const* info = sCompanionMgr.Find(guid);
        return info && !info->temporary && !info->trial && !info->parted && !info->inAccount &&
            info->recruiterGuid == player->GetGUIDLow() && !player->IsBot() &&
            sCompanionMgr.GetAffinity(guid) >= MAX_AFFINITY;
    }

    void Update(Player* me, Player* leader, State& state)
    {
        if (!me || !MayJoin(leader, me))
        {
            state.dueMs = 0;
            return;
        }
        CompanionInfo const* info = sCompanionMgr.Find(me->GetGUIDLow());
        if (!info || info->joinAsked)
            return;
        uint32 const now = Now();
        if (!state.dueMs)
        {
            state.dueMs = now + OFFER_DELAY_MS;
            return;
        }
        if (now < state.dueMs)
            return;
        // Not while fighting or apart: a little later.
        if (me->IsInCombat() || leader->IsInCombat() || !leader->IsAlive() || !me->IsAlive() ||
            !me->IsWithinDistInMap(leader, Talk::TALK_YD))
        {
            state.dueMs = now + 5 * IN_MILLISECONDS;
            return;
        }
        sCompanionMgr.MarkJoinAsked(me->GetGUIDLow());
        Talk::Context const ctx = Talk::ContextOf(leader, me);
        me->SetFacingToObject(leader);
        me->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
        SayToParty(me, ctx, "account.offer");
        Talk::SendPage(leader, me, OfferPage(ctx));
    }

    Talk::Page OfferPage(Talk::Context const& ctx)
    {
        Talk::Page page;
        page.text = Talk::Line(ctx, "account.offer.page");
        Talk::Option yes;
        yes.id = "account:yes";
        yes.text = Talk::Line(ctx, "account.offer.yes");
        Talk::Option later;
        later.id = "account:later";
        later.text = Talk::Line(ctx, "account.offer.later");
        page.options = { yes, later };
        return page;
    }

    std::string Answer(Talk::Context const& ctx, bool yes, bool& joined)
    {
        joined = false;
        uint32 const guid = ctx.bot->GetGUIDLow();
        sCompanionMgr.MarkJoinAsked(guid);
        if (!yes)
            return Talk::Line(ctx, "account.later");
        std::string const error = sCompanionMgr.JoinAccount(ctx.player, guid);
        if (error == "accountfull")
            return Talk::Line(ctx, "account.join.full");
        if (!error.empty())
            return Talk::Line(ctx, "talk.gone");
        joined = true;
        BotShared::BotChat::SystemTo(ctx.player, "PBOK;" + std::string(ctx.bot->GetName()) + ";joinaccount");
        return Talk::Line(ctx, "account.join.done");
    }
}
}
