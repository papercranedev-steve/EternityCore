#include "Life/Errands/Trip/LessonPriority.h"
#include "Shared/Phrases.h"
#include "Commander/Perception/Profiles/SpellTags.h"
#include "Shared/ClassTraining.h"
#include "Shared/Crafting.h"
#include "Shared/FoodAndDrink.h"
#include "Shared/Provisions.h"
#include "DBCStores.h"
#include "ItemPrototype.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include <algorithm>
#include <map>

namespace Life
{
namespace Lessons
{
    namespace
    {
        uint32 SkillLineOf(uint32 spellId)
        {
            SkillLineAbilityMapBounds const bounds = sSpellMgr.GetSkillLineAbilityMapBoundsBySpellId(spellId);
            return bounds.first != bounds.second ? bounds.first->second->skillId : 0;
        }

        // The skill line most of the player's talents belong to (its talent tree), 0 = none.
        uint32 TreeOf(Player const* me)
        {
            std::map<uint32, uint32> count;
            for (auto const& entry : me->GetSpellMap())
            {
                if (entry.second.state == PLAYERSPELL_REMOVED || !GetTalentSpellPos(entry.first))
                    continue;
                if (uint32 const skill = SkillLineOf(entry.first))
                    ++count[skill];
            }
            uint32 best = 0;
            uint32 bestCount = 0;
            for (auto const& itr : count)
                if (itr.second > bestCount)
                {
                    best = itr.first;
                    bestCount = itr.second;
                }
            return best;
        }

        // Conjured goods others live on: food, water, a healthstone, a soulstone.
        bool MakesTeamSupply(SpellEntry const* spell)
        {
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(BotShared::Crafting::ProductOf(spell).itemId);
            if (!proto || !(proto->Flags & ITEM_FLAG_CONJURED))
                return false;
            return BotShared::FoodAndDrink::ItemSpell(proto, BotShared::FoodAndDrink::Kind::Food) ||
                   BotShared::FoodAndDrink::ItemSpell(proto, BotShared::FoodAndDrink::Kind::Drink) ||
                   BotShared::Provisions::IsHealthstone(proto->ItemId) ||
                   spell->SpellName[0].find("Soulstone") != std::string::npos;
        }

        bool Resurrects(SpellEntry const* spell)
        {
            return spell->HasEffect(SPELL_EFFECT_RESURRECT) || spell->HasEffect(SPELL_EFFECT_RESURRECT_NEW);
        }

        bool Heals(SpellEntry const* spell)
        {
            return spell->HasEffect(SPELL_EFFECT_HEAL) || spell->HasAura(SPELL_AURA_PERIODIC_HEAL);
        }

        bool Taunts(SpellEntry const* spell)
        {
            return spell->HasEffect(SPELL_EFFECT_ATTACK_ME) || spell->HasAura(SPELL_AURA_MOD_TAUNT);
        }

        bool Controls(SpellEntry const* spell)
        {
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                if (Commander::SpellTags::IsDisableEffect(spell, i))
                    return true;
            return false;
        }

        // A buff it can put on others (Arcane Intellect, Shadow Protection).
        bool BuffsOthers(SpellEntry const* spell)
        {
            if (!spell->IsPositiveSpell() || !spell->HasEffect(SPELL_EFFECT_APPLY_AURA))
                return false;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                if (spell->Effect[i] == SPELL_EFFECT_APPLY_AURA && spell->EffectImplicitTargetA[i] != TARGET_UNIT_CASTER)
                    return true;
            return false;
        }

        Tier Judge(SpellEntry const* spell, Context const& context, uint32 tree)
        {
            uint32 const first = sSpellMgr.GetFirstSpellInChain(spell->Id);
            bool const isNew = first == spell->Id;
            bool const used = context.usedChains.count(first) > 0;
            bool const inTree = tree && SkillLineOf(spell->Id) == tree;

            if (MakesTeamSupply(spell))
                return Tier::TeamSupply;
            if (Resurrects(spell))
                return isNew ? Tier::Resurrect : context.heals ? Tier::RoleCore : Tier::Useful;
            if (context.heals && Heals(spell) && (used || isNew))
                return Tier::RoleCore;
            if (context.tanks && (Taunts(spell) || (inTree && used)))
                return Tier::RoleCore;
            if (Commander::SpellTags::DealsDamage(spell) && inTree && (used || isNew))
                return Tier::Key;
            if (Commander::SpellTags::StopsCasts(spell) || Controls(spell))
                return Tier::Key;
            if (used)
                return Tier::Useful;
            if (isNew && (Commander::SpellTags::DealsDamage(spell) || Heals(spell) || BuffsOthers(spell)))
                return Tier::Useful;
            return Tier::Junk;
        }
    }

    Tier TierOf(Player const* me, uint32 spellId, Context const& context)
    {
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
        return spell ? Judge(spell, context, TreeOf(me)) : Tier::Junk;
    }

    std::vector<Listed> Available(Player const* me, Context const& context)
    {
        uint32 const tree = TreeOf(me);
        std::vector<Listed> listed;
        for (BotShared::ClassTraining::Lesson const& lesson : BotShared::ClassTraining::Available(me))
        {
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(lesson.spellId);
            if (!spell)
                continue;
            listed.push_back({ lesson.spellId, lesson.cost, Judge(spell, context, tree) });
        }
        std::stable_sort(listed.begin(), listed.end(), [](Listed const& a, Listed const& b)
        {
            return a.tier != b.tier ? a.tier < b.tier : a.cost > b.cost;
        });
        return listed;
    }

    uint32 CostUpTo(std::vector<Listed> const& lessons, Tier upTo)
    {
        uint32 cost = 0;
        for (Listed const& l : lessons)
            if (l.tier <= upTo)
                cost += l.cost;
        return cost;
    }

    std::string NameOf(uint32 spellId, LocaleConstant locale, BotShared::I18n::Lang lang)
    {
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
        if (!spell)
            return std::to_string(spellId);
        uint32 const loc = uint32(locale) < spell->SpellName.size() ? uint32(locale) : 0;
        std::string name = spell->SpellName[loc].empty() ? spell->SpellName[0] : spell->SpellName[loc];
        std::string const& rank = spell->Rank[loc].empty() ? spell->Rank[0] : spell->Rank[loc];
        if (!rank.empty())
            name = BotShared::Phrases::Text(lang, "lesson.rank", { { "name", name }, { "rank", rank } });
        return name;
    }
}
}
