#include "Companions/Account/JoinOffer.h"
#include "Companions/CompanionMgr.h"
#include "Shared/BotChat.h"
#include "Shared/Phrases.h"
#include "PartyBotAI.h"
#include "MotionMaster.h"
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

    namespace
    {
        void Rise(Player* me, State& state)
        {
            state.kneelUntilMs = 0;
            if (me && me->GetStandState() == UNIT_STAND_STATE_KNEEL)
                me->SetStandState(UNIT_STAND_STATE_STAND);
        }

        // True while he still kneels.
        bool UpdateKneel(Player* me, Player* leader, State& state)
        {
            if (!state.kneelUntilMs)
                return false;
            // (joined: MayJoin is false now, he rises after his line)
            if (Now() >= state.kneelUntilMs || !MayJoin(leader, me) || !me->IsAlive() || me->IsInCombat() ||
                leader->IsInCombat() || !me->IsWithinDistInMap(leader, Talk::TALK_YD))
            {
                Rise(me, state);
                return false;
            }
            if (me->GetStandState() != UNIT_STAND_STATE_KNEEL)
                me->SetStandState(UNIT_STAND_STATE_KNEEL);
            return true;
        }

        void Kneel(Talk::Context const& ctx)
        {
            if (!ctx.ai)
                return;
            ctx.ai->JoinOfferState().kneelUntilMs = Now() + KNEEL_MAX_MS;
            ctx.bot->StopMoving();
            ctx.bot->GetMotionMaster()->MoveIdle();
            ctx.bot->SetFacingToObject(ctx.player);
            ctx.bot->SetStandState(UNIT_STAND_STATE_KNEEL);
        }

        void EndKneel(Talk::Context const& ctx)
        {
            if (ctx.ai)
                Rise(ctx.bot, ctx.ai->JoinOfferState());
        }
    }

    bool Update(Player* me, Player* leader, State& state)
    {
        if (!me || !leader)
            return false;
        if (UpdateKneel(me, leader, state))
            return true;
        if (!MayJoin(leader, me))
        {
            state.dueMs = 0;
            return false;
        }
        CompanionInfo const* info = sCompanionMgr.Find(me->GetGUIDLow());
        if (!info || info->joinAsked)
            return false;
        uint32 const now = Now();
        if (!state.dueMs)
        {
            state.dueMs = now + OFFER_DELAY_MS;
            return false;
        }
        if (now < state.dueMs)
            return false;
        // Not while fighting or apart: a little later.
        if (me->IsInCombat() || leader->IsInCombat() || !leader->IsAlive() || !me->IsAlive() ||
            !me->IsWithinDistInMap(leader, Talk::TALK_YD))
        {
            state.dueMs = now + 5 * IN_MILLISECONDS;
            return false;
        }
        sCompanionMgr.MarkJoinAsked(me->GetGUIDLow());
        Talk::Context const ctx = Talk::ContextOf(leader, me);
        me->SetFacingToObject(leader);
        me->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
        SayToParty(me, ctx, "account.offer");
        Talk::SendPage(leader, me, OfferPage(ctx));
        return false;
    }

    Talk::Page OfferPage(Talk::Context const& ctx)
    {
        Talk::Page page;
        page.text = Talk::Line(ctx, "account.offer.page");
        Talk::Option yes;
        yes.id = "account:yes";
        yes.icon = Talk::Icon::Yes;
        yes.text = Talk::Line(ctx, "account.offer.yes");
        Talk::Option later;
        later.id = "account:later";
        later.icon = Talk::Icon::No;
        later.text = Talk::Line(ctx, "account.offer.later");
        page.options = { yes, later };
        return page;
    }

    namespace
    {
        // The rite (user 2026-11-22: not a clerk's form): from now on he is another self of
        // the player, and by the old custom the one you give your days to names you anew.
        Talk::Page NamingPage(Talk::Context const& ctx)
        {
            Talk::Page page;
            page.text = Talk::Line(ctx, "account.name.page");
            Talk::Option name;
            name.id = "account:name";
            name.icon = Talk::Icon::Yes;
            name.text = Talk::Line(ctx, "account.name.new");
            Talk::Option keep;
            keep.id = "account:keep";
            keep.icon = Talk::Icon::Chat;
            keep.text = Talk::Line(ctx, "account.name.keep");
            Talk::Option later;
            later.id = "account:later";
            later.icon = Talk::Icon::No;
            later.text = Talk::Line(ctx, "account.offer.later");
            page.options = { name, keep, later };
            return page;
        }

        Talk::Outcome Closing(std::string reply)
        {
            Talk::Outcome outcome;
            outcome.kind = Talk::Outcome::Kind::Close;
            outcome.reply = std::move(reply);
            return outcome;
        }
    }

    Talk::Outcome Choose(Talk::Context const& ctx, std::string const& arg)
    {
        uint32 const guid = ctx.bot->GetGUIDLow();
        Talk::Outcome outcome;
        if (arg.empty() || arg == "yes")
        {
            sCompanionMgr.MarkJoinAsked(guid);
            // (full: said now, not after he was named)
            if (arg == "yes" && !sCompanionMgr.AccountHasRoom(ctx.player))
                return Closing(Talk::Line(ctx, "account.join.full"));
            Talk::Page const page = arg.empty() ? OfferPage(ctx) : NamingPage(ctx);
            outcome.kind = Talk::Outcome::Kind::Page;
            outcome.text = page.text;
            outcome.options = page.options;
            return outcome;
        }
        if (arg == "name")
        {
            // the addon's name box; it sends `.partybot joinaccount <guid> <name>`, its 取消
            // account:later. He waits on one knee.
            Kneel(ctx);
            outcome.kind = Talk::Outcome::Kind::Open;
            outcome.reply = Talk::Line(ctx, "account.name.listen");
            outcome.window = "name";
            outcome.arg = std::to_string(guid);
            return outcome;
        }
        EndKneel(ctx);
        if (arg != "keep")
            return Closing(Talk::Line(ctx, "account.later"));

        std::string const name = ctx.bot->GetName();
        std::string const error = sCompanionMgr.JoinAccount(ctx.player, guid);
        if (error == "accountfull")
            return Closing(Talk::Line(ctx, "account.join.full"));
        if (!error.empty())
            return Closing(Talk::Line(ctx, "talk.gone"));
        BotShared::BotChat::SystemTo(ctx.player, "PBOK;" + name + ";joinaccount");
        return Closing(JoinedLine(ctx, name));
    }

    std::string JoinedLine(Talk::Context const& ctx, std::string const& oldName)
    {
        if (oldName.empty())
            return Talk::Line(ctx, "account.join.done");
        if (oldName == ctx.bot->GetName())
            return Talk::Line(ctx, "account.join.kept");
        return BotShared::Phrases::Format(Talk::Line(ctx, "account.join.named"), { { "old", oldName } });
    }
}
}
