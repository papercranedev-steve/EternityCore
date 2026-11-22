#include "Companions/Talk/Services/ServiceList.h"
#include "Companions/Talk/Talk.h"
#include "Shared/Phrases.h"

namespace Companions
{
namespace Talk
{
    namespace
    {
        // His sheet, talents and quest log (the unit menu's 观察 / 天赋 / 任务): the window
        // closes and the addon opens that window, as the bags (BagsService) do. Anyone of the
        // party; no words from him, it is only looking.
        struct Look
        {
            char const* arg;        // also the addon's window key
            char const* word;
        };
        Look const LOOKS[] = {
            { "sheet",   "talk.opt.sheet" },
            { "talents", "talk.opt.talents" },
            { "quests",  "talk.opt.quests" },
        };

        class LookService : public Service
        {
        public:
            char const* Key() const override { return "look"; }
            void Offer(Context const& ctx, std::vector<Option>& out) const override
            {
                for (Look const& look : LOOKS)
                {
                    Option option;
                    option.id = std::string(Key()) + ":" + look.arg;
                    option.icon = Icon::Chat;
                    option.text = BotShared::Phrases::Word(ctx.lang, look.word);
                    out.push_back(option);
                }
            }
            Outcome Choose(Context const& ctx, std::string const& arg) const override
            {
                Outcome outcome;
                for (Look const& look : LOOKS)
                    if (arg == look.arg)
                    {
                        outcome.kind = Outcome::Kind::Open;
                        outcome.window = look.arg;
                        return outcome;
                    }
                outcome.reply = Line(ctx, "talk.gone");
                return outcome;
            }
        };
    }

    std::unique_ptr<Service> MakeLookService()
    {
        return std::make_unique<LookService>();
    }
}
}
