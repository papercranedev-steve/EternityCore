#include "Companions/Talk/Services/ServiceList.h"
#include "Companions/Talk/Talk.h"
#include "Companions/Affinity/Gates.h"
#include "Life/Requests/Phrases.h"
#include "Shared/Crafting.h"
#include "Shared/Professions.h"
#include "Player.h"
#include "SpellEntry.h"
#include <set>

namespace Companions
{
namespace Talk
{
    namespace
    {
        // His crafting professions he knows recipes of (one option each). Gathering skills
        // (smelting) and the class's own conjuring are not his trade.
        std::set<uint32> CraftSkills(Player const* bot)
        {
            namespace Prof = BotShared::Professions;
            std::set<uint32> skills;
            for (SpellEntry const* recipe : BotShared::Crafting::KnownRecipes(bot))
            {
                uint32 const skill = BotShared::Crafting::SkillOf(recipe);
                if (Prof::KindOf(skill) == Prof::Kind::Crafting ||
                    skill == Prof::FIRST_AID || skill == Prof::COOKING)
                    skills.insert(skill);
            }
            return skills;
        }

        // "能帮我配点药水吗？", by profession: the craft window of that profession.
        class CraftService : public Service
        {
        public:
            char const* Key() const override { return "craft"; }

            void Offer(Context const& ctx, std::vector<Option>& out) const override
            {
                if (!Gates::Allows(ctx.bot->GetGUIDLow(), Gates::Gate::Crafting))
                    return;
                std::string const anything = Life::Phrases::Word("talk.opt.craft", "能帮我做点东西吗？");
                for (uint32 skill : CraftSkills(ctx.bot))
                {
                    Option option;
                    option.id = std::string(Key()) + ":" + std::to_string(skill);
                    option.icon = Icon::Trainer;
                    option.text = Life::Phrases::Word("talk.opt.craft." + std::to_string(skill), anything);
                    out.push_back(option);
                }
            }

            Outcome Choose(Context const& ctx, std::string const& arg) const override
            {
                Outcome outcome;
                outcome.kind = Outcome::Kind::Open;
                outcome.window = "craft";
                outcome.arg = arg;
                outcome.reply = Line(ctx, "talk.craft.ok");
                return outcome;
            }
        };
    }

    std::unique_ptr<Service> MakeCraftService()
    {
        return std::make_unique<CraftService>();
    }

    bool CraftsFor(Player const* bot, uint32 skill)
    {
        return CraftSkills(bot).count(skill) != 0;
    }
}
}
