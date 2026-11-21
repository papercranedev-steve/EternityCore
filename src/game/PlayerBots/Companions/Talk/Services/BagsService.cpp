#include "Companions/Talk/Services/ServiceList.h"
#include "Companions/Talk/Talk.h"
#include "Companions/Affinity/Gates.h"
#include "Life/Requests/Phrases.h"
#include "Player.h"

namespace Companions
{
namespace Talk
{
    namespace
    {
        class BagsService : public Service
        {
        public:
            char const* Key() const override { return "bags"; }

            void Offer(Context const& ctx, std::vector<Option>& out) const override
            {
                if (!Gates::Allows(ctx.bot->GetGUIDLow(), Gates::Gate::ManageBags))
                    return;
                Option option;
                option.id = Key();
                option.icon = Icon::Banker;
                option.text = Life::Phrases::Word("talk.opt.bags", "让我看看你的包。");
                out.push_back(option);
            }

            Outcome Choose(Context const& ctx, std::string const& /*arg*/) const override
            {
                Outcome outcome;
                outcome.kind = Outcome::Kind::Open;
                outcome.window = "bags";
                outcome.reply = Line(ctx, "talk.bags.ok");
                return outcome;
            }
        };
    }

    std::unique_ptr<Service> MakeBagsService()
    {
        return std::make_unique<BagsService>();
    }
}
}
