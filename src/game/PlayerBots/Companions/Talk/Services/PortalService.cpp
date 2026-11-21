#include "Companions/Talk/Services/ServiceList.h"
#include "Companions/Talk/Talk.h"
#include "Companions/Affinity/Gates.h"
#include "Life/Errands/ErrandCommon.h"
#include "Life/Requests/Phrases.h"
#include "Shared/Portals.h"
#include "Log.h"
#include "Player.h"
#include "SpellEntry.h"
#include <algorithm>
#include <cstdlib>

namespace Companions
{
namespace Talk
{
    namespace
    {
        bool Unlocked(Player const* bot)
        {
            return Gates::Allows(bot->GetGUIDLow(), Gates::Gate::MagePortal) && !BotShared::Portals::Known(bot).empty();
        }

        // Its runes allow this one now.
        bool CanCast(Player const* bot, SpellEntry const* spell)
        {
            uint32 const rune = BotShared::Portals::RuneOf(spell);
            return !rune || bot->GetItemCount(rune) >= std::max<uint32>(spell->ReagentCount[0], 1);
        }

        // "能帮我开个传送门吗？": a page of the cities he knows (like a teleporter NPC), then he
        // opens the portal with his own Rune of Portals (his supplies: Shared/Supplies; the
        // player pays nothing, user 2026-11-21). No rune: not offered, the affinity page says why.
        class PortalService : public Service
        {
        public:
            char const* Key() const override { return "portal"; }

            void Offer(Context const& ctx, std::vector<Option>& out) const override
            {
                if (!Unlocked(ctx.bot) || !BotShared::Portals::CastsFromBags(ctx.bot))
                    return;
                Option option;
                option.id = Key();
                option.icon = Icon::Taxi;
                option.text = Life::Phrases::Word("talk.opt.portal", "能帮我开个传送门吗？");
                out.push_back(option);
            }

            Outcome Choose(Context const& ctx, std::string const& arg) const override
            {
                Outcome outcome;
                if (arg.empty())
                {
                    outcome.kind = Outcome::Kind::Page;
                    outcome.text = Line(ctx, "talk.portal.where");
                    for (SpellEntry const* spell : BotShared::Portals::Known(ctx.bot))
                        if (CanCast(ctx.bot, spell))
                        {
                            Option city;
                            city.id = std::string(Key()) + ":" + std::to_string(spell->Id);
                            city.icon = Icon::Taxi;
                            city.text = Life::Phrases::Word("talk.opt.portal." + std::to_string(spell->Id), spell->SpellName[0]);
                            outcome.options.push_back(city);
                        }
                    return outcome;
                }

                uint32 const spellId = uint32(std::strtoul(arg.c_str(), nullptr, 10));
                SpellEntry const* spell = nullptr;
                for (SpellEntry const* known : BotShared::Portals::Known(ctx.bot))
                    if (known->Id == spellId)
                        spell = known;
                if (!spell || !CanCast(ctx.bot, spell))
                {
                    outcome.reply = Line(ctx, "talk.gone");
                    return outcome;
                }
                // A 10 s cast: it stands still for it (its follow would break it off).
                Life::Errands::StopWalk(ctx.bot);
                ctx.bot->SetFacingToObject(ctx.player);
                SpellCastResult const result = ctx.bot->CastSpell(ctx.bot, spell, false);
                if (result != SPELL_CAST_OK)
                {
                    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Talk] %s cannot open portal %u for %s (result %u).",
                             ctx.bot->GetName(), spellId, ctx.player->GetName(), uint32(result));
                    outcome.reply = Line(ctx, "talk.portal.cannot");
                    return outcome;
                }
                outcome.reply = Line(ctx, "talk.portal.ok");
                return outcome;
            }

            void Blockers(Player* bot, std::vector<Blocker>& out) const override
            {
                if (Unlocked(bot) && !BotShared::Portals::CastsFromBags(bot))
                    out.push_back({ Gates::Of(Gates::Gate::MagePortal).key,
                                    Life::Phrases::Word("talk.block.portal", "缺传送门符文") });
            }
        };
    }

    std::unique_ptr<Service> MakePortalService()
    {
        return std::make_unique<PortalService>();
    }
}
}
