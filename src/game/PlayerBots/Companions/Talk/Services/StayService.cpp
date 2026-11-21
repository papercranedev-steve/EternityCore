#include "Companions/Talk/Services/ServiceList.h"
#include "Companions/Talk/Talk.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Shared/Phrases.h"
#include "PartyBotAI.h"
#include "Player.h"

namespace Companions
{
namespace Talk
{
    namespace
    {
        char const* const FOLLOW_ARG = "follow";

        // Only the one he can use now: "wait here" while he follows, "follow me" while he waits.
        class StayService : public Service
        {
        public:
            char const* Key() const override { return "stay"; }

            void Offer(Context const& ctx, std::vector<Option>& out) const override
            {
                if (!ctx.ai)
                    return;
                Commander::StaySpot spot;
                bool const staying = sCommanderMgr.GetStay(ctx.ai, spot);
                Option option;
                option.icon = Icon::Chat;
                if (staying)
                {
                    option.id = std::string(Key()) + ":" + FOLLOW_ARG;
                    option.text = BotShared::Phrases::Word(ctx.lang, "talk.opt.follow");
                }
                else
                {
                    option.id = Key();
                    option.text = BotShared::Phrases::Word(ctx.lang, "talk.opt.stay");
                }
                out.push_back(option);
            }

            Outcome Choose(Context const& ctx, std::string const& arg) const override
            {
                bool const stay = arg != FOLLOW_ARG;
                Outcome outcome;
                outcome.kind = Outcome::Kind::Close;
                if (!sCommanderMgr.SetStay(ctx.bot, stay))
                {
                    outcome.reply = Line(ctx, "talk.stay.cannot");
                    return outcome;
                }
                outcome.reply = Line(ctx, stay ? "talk.stay.ok" : "talk.follow.ok");
                return outcome;
            }
        };
    }

    std::unique_ptr<Service> MakeStayService()
    {
        return std::make_unique<StayService>();
    }
}
}
