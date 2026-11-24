#include "Companions/Talk/Services/ServiceList.h"
#include "Companions/Talk/Talk.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/Tanks.h"
#include "Shared/Phrases.h"
#include "Player.h"

namespace Companions
{
namespace Talk
{
    namespace
    {
        char const* const PASSIVE_ARG = "passive";

        // 战斗策略 (user 2026-11-24): whether the party starts fights on its own
        // (Commander/Coordination/Situation/Pull/PullStance.h, AutoClear.h). Only the one that
        // changes something: "wait for me" while active, "go ahead" while passive. The stance is
        // the player's, for his whole party; any member answers.
        class PullStanceService : public Service
        {
        public:
            char const* Key() const override { return "pullstance"; }

            void Offer(Context const& ctx, std::vector<Option>& out) const override
            {
                if (!ctx.player)
                    return;
                bool const active = sCommanderMgr.GetPullStance(ctx.player->GetObjectGuid()) == Commander::PullStance::Active;
                Option option;
                option.icon = Icon::Chat;
                if (active)
                {
                    option.id = std::string(Key()) + ":" + PASSIVE_ARG;
                    option.text = BotShared::Phrases::Word(ctx.lang, "talk.opt.pullstance.passive");
                }
                else
                {
                    option.id = Key();
                    option.text = BotShared::Phrases::Word(ctx.lang, "talk.opt.pullstance.active");
                }
                out.push_back(option);
            }

            Outcome Choose(Context const& ctx, std::string const& arg) const override
            {
                Commander::PullStance const stance = arg == PASSIVE_ARG ? Commander::PullStance::Passive
                                                                        : Commander::PullStance::Active;
                sCommanderMgr.SetPullStance(ctx.player->GetObjectGuid(), stance);     // (saved)

                Outcome outcome;
                outcome.kind = Outcome::Kind::Close;
                if (stance == Commander::PullStance::Passive)
                    outcome.reply = Line(ctx, "talk.pullstance.passive");
                else
                    outcome.reply = Line(ctx, HasTank(ctx) ? "talk.pullstance.active" : "talk.pullstance.notank");
                return outcome;
            }

        private:
            // A tank bot that can pull (AutoClear does nothing without one).
            static bool HasTank(Context const& ctx)
            {
                std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(ctx.bot);
                return commander && Commander::Tanks::Main(commander->GetWorld(), true);
            }
        };
    }

    std::unique_ptr<Service> MakePullStanceService()
    {
        return std::make_unique<PullStanceService>();
    }
}
}
