#include "Shared/SkillUpCost.h"
#include "Shared/Crafting.h"
#include "Shared/Professions.h"
#include "Shared/Vendor.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "SpellEntry.h"
#include <algorithm>
#include <limits>
#include <map>
#include <mutex>
#include <vector>

namespace BotShared
{
namespace SkillUpCost
{
    namespace
    {
        uint32 const MAX_VALUE = 300;
        uint32 const FLOOR_DIVISOR = 20;    // at least value^2 / 20 copper a point

        struct Recipe
        {
            uint32 req = 0;         // learned at (the trainer's requirement)
            uint32 yellow = 0;      // SkillLineAbility min_value
            uint32 gray = 0;        // SkillLineAbility max_value
            uint32 net = 0;         // copper a cast: reagents less the product's sell price
        };

        uint32 ReagentCost(SpellEntry const* spell)
        {
            uint32 cost = 0;
            for (uint32 r = 0; r < MAX_SPELL_REAGENTS; ++r)
                if (spell->Reagent[r] > 0 && spell->ReagentCount[r])
                    cost += Vendor::UnitPrice(sObjectMgr.GetItemPrototype(uint32(spell->Reagent[r]))) * spell->ReagentCount[r];
            return cost;
        }

        std::vector<Recipe> RecipesOf(uint32 skill)
        {
            std::vector<Recipe> out;
            for (Professions::TrainerRecipe const& t : Professions::TrainerRecipes(skill))
            {
                SpellEntry const* spell = sSpellMgr.GetSpellEntry(t.spellId);
                if (!spell)
                    continue;
                Recipe r;
                r.req = t.reqSkillValue;
                SkillLineAbilityMapBounds const bounds = sSpellMgr.GetSkillLineAbilityMapBoundsBySpellId(t.spellId);
                for (auto itr = bounds.first; itr != bounds.second; ++itr)
                    if (itr->second->skillId == skill)
                    {
                        r.yellow = itr->second->min_value;
                        r.gray = itr->second->max_value;
                        break;
                    }
                uint32 const cost = ReagentCost(spell);
                if (!r.gray || !cost)
                    continue;   // levels nothing, or reagents without a price: unknown
                Crafting::Product const product = Crafting::ProductOf(spell);
                ItemPrototype const* proto = product.itemId ? sObjectMgr.GetItemPrototype(product.itemId) : nullptr;
                uint32 const back = proto ? proto->SellPrice * product.perCast : 0;
                r.net = cost > back ? cost - back : 0;
                out.push_back(r);
            }
            return out;
        }

        std::vector<uint32> Build(uint32 skill)
        {
            std::vector<uint32> table(MAX_VALUE, 0);
            std::vector<Recipe> const recipes = RecipesOf(skill);
            uint32 previous = 0;
            for (uint32 s = 1; s < MAX_VALUE; ++s)
            {
                double best = std::numeric_limits<double>::max();
                for (Recipe const& r : recipes)
                {
                    if (r.req > s || s >= r.gray)
                        continue;
                    double const points = (s < r.yellow || r.gray <= r.yellow) ? 1.0 : double(r.gray - s) / double(r.gray - r.yellow);
                    if (points <= 0.0)
                        continue;
                    best = std::min(best, double(r.net) / points);
                }
                uint32 cost = best == std::numeric_limits<double>::max() ? previous : uint32(best + 0.5);
                cost = std::max(cost, s * s / FLOOR_DIVISOR);
                table[s] = cost;
                previous = cost;
            }
            return table;
        }
    }

    uint32 OfPoint(uint32 skill, uint32 value)
    {
        if (Professions::KindOf(skill) != Professions::Kind::Crafting || !value || value >= MAX_VALUE)
            return 0;
        static std::mutex lock;
        static std::map<uint32, std::vector<uint32>> tables;
        std::lock_guard<std::mutex> guard(lock);
        auto itr = tables.find(skill);
        if (itr == tables.end())
            itr = tables.emplace(skill, Build(skill)).first;
        return itr->second[value];
    }
}
}
