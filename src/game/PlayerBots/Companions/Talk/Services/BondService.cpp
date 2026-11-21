#include "Companions/Talk/Services/ServiceList.h"
#include "Companions/Talk/Talk.h"
#include "Companions/Recruit/Trial.h"
#include "Life/Requests/Phrases.h"
#include "Player.h"

namespace Companions
{
namespace Talk
{
    namespace
    {
        // A trial partner ready to stay (Recruit/Trial): "以后跟我一起走吧？" opens his offer page,
        // whose answers are this service's (bond:yes, bond:later). Yes promotes him and he
        // introduces himself (recruiting doc §2.3).
        class BondService : public Service
        {
        public:
            char const* Key() const override { return "bond"; }

            void Offer(Context const& ctx, std::vector<Option>& out) const override
            {
                if (!ctx.bot || !Trial::IsReady(ctx.bot->GetGUIDLow()))
                    return;
                Option option;
                option.id = Key();
                option.icon = Icon::Chat;
                option.text = Life::Phrases::Word("talk.opt.bond", "以后跟我一起走吧？");
                out.push_back(option);
            }

            Outcome Choose(Context const& ctx, std::string const& arg) const override
            {
                Outcome outcome;
                if (arg.empty())
                {
                    Page const page = Trial::OfferPage(ctx);
                    outcome.kind = Outcome::Kind::Page;
                    outcome.text = page.text;
                    outcome.options = page.options;
                    return outcome;
                }
                if (arg != "yes")
                {
                    outcome.kind = Outcome::Kind::Close;
                    outcome.reply = Line(ctx, "trial.bond.later");
                    return outcome;
                }
                if (!Trial::Bond(ctx.bot, ctx.player))
                {
                    outcome.kind = Outcome::Kind::Close;
                    outcome.reply = Line(ctx, "talk.gone");
                    return outcome;
                }
                // He introduces himself: the page has only the player's goodbye.
                static std::vector<std::string> const none;
                Life::Phrases::Vars vars;
                vars["leader"] = ctx.player ? ctx.player->GetName() : "";
                vars["name"] = ctx.bot->GetName();
                vars["class"] = Life::Phrases::Word("class." + std::to_string(uint32(ctx.bot->GetClass())), "冒险者");
                outcome.kind = Outcome::Kind::Page;
                outcome.reply = Line(ctx, "trial.bond.yes");
                outcome.text = Life::Phrases::Say("trial.intro", ctx.tags ? *ctx.tags : none, vars);
                return outcome;
            }
        };
    }

    std::unique_ptr<Service> MakeBondService()
    {
        return std::make_unique<BondService>();
    }
}
}
