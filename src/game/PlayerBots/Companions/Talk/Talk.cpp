#include "Companions/Talk/Talk.h"
#include "Companions/CompanionMgr.h"
#include "Life/Requests/Phrases.h"
#include "Shared/Capitals.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Utilities/Random.h"

namespace Companions
{
namespace Talk
{
    namespace
    {
        // As the addon's affinity standings (初识 0, 熟络 20, 信任 50, 挚友 80).
        char const* TierOf(uint32 affinity)
        {
            if (affinity >= 80)
                return "close";
            if (affinity >= 50)
                return "trusted";
            if (affinity >= 20)
                return "friend";
            return "new";
        }

        uint32 constexpr LOW_MANA_PCT = 30;
        uint32 constexpr TOWN_GREETING_PCT = 50;

        std::string Greeting(Context const& ctx)
        {
            Player* bot = ctx.bot;
            if (bot->GetPowerType() == POWER_MANA && bot->GetMaxPower(POWER_MANA) &&
                bot->GetPower(POWER_MANA) * 100 / bot->GetMaxPower(POWER_MANA) < LOW_MANA_PCT)
            {
                std::string const line = Line(ctx, "talk.hello.drink");
                if (!line.empty())
                    return line;
            }
            if (BotShared::Capitals::OfZone(bot->GetZoneId()) && urand(0, 99) < TOWN_GREETING_PCT)
            {
                std::string const line = Line(ctx, "talk.hello.town");
                if (!line.empty())
                    return line;
            }
            std::string const line = Line(ctx, std::string("talk.hello.") + TierOf(ctx.affinity));
            return line.empty() ? Line(ctx, "talk.hello.new") : line;
        }

        std::pair<std::string, std::string> Split(std::string const& optionId)
        {
            size_t const colon = optionId.find(':');
            if (colon == std::string::npos)
                return { optionId, std::string() };
            return { optionId.substr(0, colon), optionId.substr(colon + 1) };
        }
    }

    char const* IconKey(Icon icon)
    {
        switch (icon)
        {
            case Icon::Vendor:  return "vendor";
            case Icon::Taxi:    return "taxi";
            case Icon::Trainer: return "trainer";
            case Icon::Binder:  return "binder";
            case Icon::Banker:  return "banker";
            case Icon::Chat:
            default:            return "chat";
        }
    }

    std::string Line(Context const& ctx, std::string const& key)
    {
        static std::vector<std::string> const none;
        Life::Phrases::Vars vars;
        vars["leader"] = ctx.player ? ctx.player->GetName() : "";
        return Life::Phrases::Say(key, ctx.tags ? *ctx.tags : none, vars);
    }

    Refusal Check(Player* player, Player* bot, std::string& line)
    {
        if (!player || !bot || !player->GetGroup() || bot->GetGroup() != player->GetGroup())
            return Refusal::NotInGroup;
        if (!bot->IsAlive())
            return Refusal::Dead;
        if (!bot->IsWithinDistInMap(player, TALK_YD))
            return Refusal::Far;
        if (bot->IsInCombat() || player->IsInCombat())
        {
            line = Line(ContextOf(player, bot), "talk.busy");
            return Refusal::Busy;
        }
        return Refusal::None;
    }

    Context ContextOf(Player* player, Player* bot)
    {
        Context ctx;
        ctx.player = player;
        ctx.bot = bot;
        ctx.ai = bot && bot->AI() ? dynamic_cast<PartyBotAI*>(bot->AI()) : nullptr;
        ctx.tags = ctx.ai ? &ctx.ai->PersonalityTags() : nullptr;
        ctx.companion = bot && sCompanionMgr.IsCompanion(bot->GetGUIDLow());
        ctx.affinity = ctx.companion ? sCompanionMgr.GetAffinity(bot->GetGUIDLow()) : 0;
        return ctx;
    }

    std::vector<Option> Offers(Context const& ctx)
    {
        std::vector<Option> options;
        for (std::unique_ptr<Service> const& service : Services())
            service->Offer(ctx, options);
        return options;
    }

    Page Open(Context const& ctx)
    {
        // He turns to the player and talks, as an NPC does when its gossip opens.
        if (!ctx.bot->IsMoving())
            ctx.bot->SetFacingToObject(ctx.player);
        ctx.bot->HandleEmoteCommand(EMOTE_ONESHOT_TALK);

        Page page;
        page.text = Greeting(ctx);
        page.options = Offers(ctx);
        return page;
    }

    bool Choose(Context const& ctx, std::string const& optionId, Outcome& out)
    {
        bool offered = false;
        for (Option const& option : Offers(ctx))
            offered = offered || option.id == optionId;
        std::pair<std::string, std::string> const parts = Split(optionId);
        if (offered)
            for (std::unique_ptr<Service> const& service : Services())
                if (parts.first == service->Key())
                {
                    out = service->Choose(ctx, parts.second);
                    return true;
                }
        out = Outcome();
        out.reply = Line(ctx, "talk.gone");
        return false;
    }
}
}
