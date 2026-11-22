#include "Companions/Talk/Services/ServiceList.h"
#include "Companions/Talk/Talk.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Shared/Phrases.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "Group.h"
#include "SharedDefines.h"

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

    namespace
    {
        char const* const TEAM_STAY = "stay";
        char const* const TEAM_FOLLOW = "follow";
        char const* const TEAM_SUMMON = "summon";

        // The player's party bots (any map: SetStay goes through each one's own team).
        template <class Fn>
        void ForEachPartyBot(Player* player, Fn fn)
        {
            Group* group = player ? player->GetGroup() : nullptr;
            if (!group)
                return;
            for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
                if (Player* member = itr->getSource())
                    if (member != player && member->AI())
                        if (PartyBotAI* ai = dynamic_cast<PartyBotAI*>(member->AI()))
                            fn(member, ai);
        }

        // 大家 (the unit menu's 大家): the whole party waits / follows (only the one that
        // changes something), and the party's warlocks summon those left behind
        // (Commander GatherPlanner). He answers for them.
        class TeamService : public Service
        {
        public:
            char const* Key() const override { return "team"; }
            void Offer(Context const& ctx, std::vector<Option>& out) const override
            {
                bool anyStays = false, anyFollows = false, warlock = false;
                ForEachPartyBot(ctx.player, [&](Player* member, PartyBotAI* ai)
                {
                    Commander::StaySpot spot;
                    (sCommanderMgr.GetStay(ai, spot) ? anyStays : anyFollows) = true;
                    warlock = warlock || member->GetClass() == CLASS_WARLOCK;
                });
                auto add = [&](char const* arg, char const* word)
                {
                    Option option;
                    option.id = std::string(Key()) + ":" + arg;
                    option.icon = Icon::Chat;
                    option.text = BotShared::Phrases::Word(ctx.lang, word);
                    out.push_back(option);
                };
                if (anyFollows)
                    add(TEAM_STAY, "talk.opt.team.stay");
                if (anyStays)
                    add(TEAM_FOLLOW, "talk.opt.team.follow");
                if (warlock)
                    add(TEAM_SUMMON, "talk.opt.team.summon");
            }
            Outcome Choose(Context const& ctx, std::string const& arg) const override
            {
                Outcome outcome;
                outcome.kind = Outcome::Kind::Close;
                if (arg == TEAM_SUMMON)
                {
                    // (the warlocks say whom they summon)
                    if (!sCommanderMgr.GatherNow(ctx.player))
                        outcome.reply = Line(ctx, "talk.gone");
                    return outcome;
                }
                bool const stay = arg == TEAM_STAY;
                if (!stay && arg != TEAM_FOLLOW)
                {
                    outcome.reply = Line(ctx, "talk.gone");
                    return outcome;
                }
                outcome.reply = !OrderTeam(ctx.player, stay).empty() ? Line(ctx, stay ? "talk.stay.team" : "talk.follow.team")
                                                            : Line(ctx, "talk.stay.cannot");
                return outcome;
            }
        };
    }

    std::unique_ptr<Service> MakeStayService()
    {
        return std::make_unique<StayService>();
    }

    std::unique_ptr<Service> MakeTeamService()
    {
        return std::make_unique<TeamService>();
    }

    std::vector<Player*> OrderTeam(Player* player, bool stay)
    {
        std::vector<Player*> changed;
        ForEachPartyBot(player, [&](Player* member, PartyBotAI* /*ai*/)
        {
            if (sCommanderMgr.SetStay(member, stay))
                changed.push_back(member);
        });
        return changed;
    }
}
}
