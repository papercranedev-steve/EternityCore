#include "Companions/Talk/Services/ServiceList.h"
#include "Companions/Talk/Talk.h"
#include "Companions/Account/JoinOffer.h"
#include "Shared/Phrases.h"
#include "Player.h"

namespace Companions
{
namespace Talk
{
    namespace
    {
        // A companion at affinity 100, not in the player's account yet (Account/JoinOffer):
        // "到我的账号里来吧？" opens his offer page, whose answers are this service's
        // (account:yes, account:later). Any time, also after he asked once (account doc §2).
        class AccountService : public Service
        {
        public:
            char const* Key() const override { return "account"; }

            void Offer(Context const& ctx, std::vector<Option>& out) const override
            {
                if (!JoinOffer::MayJoin(ctx.player, ctx.bot))
                    return;
                Option option;
                option.id = Key();
                option.icon = Icon::Chat;
                option.text = BotShared::Phrases::Word(ctx.lang, "talk.opt.account");
                out.push_back(option);
            }

            Outcome Choose(Context const& ctx, std::string const& arg) const override
            {
                Outcome outcome;
                if (arg.empty())
                {
                    Page const page = JoinOffer::OfferPage(ctx);
                    outcome.kind = Outcome::Kind::Page;
                    outcome.text = page.text;
                    outcome.options = page.options;
                    return outcome;
                }
                bool joined = false;
                outcome.kind = Outcome::Kind::Close;
                outcome.reply = JoinOffer::Answer(ctx, arg == "yes", joined);
                return outcome;
            }
        };
    }

    std::unique_ptr<Service> MakeAccountService()
    {
        return std::make_unique<AccountService>();
    }
}
}
