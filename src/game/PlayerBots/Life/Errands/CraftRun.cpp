#include "Life/Errands/CraftRun.h"
#include "Life/Errands/ErrandCommon.h"
#include "Shared/Crafting.h"
#include "Shared/Supplies.h"
#include "Log.h"
#include "Player.h"
#include "Spell.h"
#include "SpellEntry.h"
#include "Timer.h"

namespace Life
{
namespace CraftRun
{
    namespace
    {
        bool CastsOwnRecipe(Player* me, uint32 spellId)
        {
            Spell const* spell = me->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            return spell && spell->m_spellInfo && spell->m_spellInfo->Id == spellId &&
                   spell->getState() != SPELL_STATE_FINISHED;
        }

        // The power left after the cast stays at MIN_POWER_PCT, unless none of the goods is left.
        bool PowerAllows(Player* me, BotShared::Supplies::Need const& need)
        {
            SpellEntry const* recipe = need.recipe;
            uint32 const cost = Spell::CalculatePowerCost(recipe, me, nullptr, nullptr, false);
            if (!cost)
                return true;
            Powers const power = Powers(recipe->powerType);
            uint32 const have = me->GetPower(power);
            uint32 const max = me->GetMaxPower(power);
            if (have < cost || !max)
                return false;
            return !need.have || float(have - cost) * 100.0f / float(max) >= MIN_POWER_PCT;
        }
    }

    bool Update(Player* me, CraftRunState& state, Player const* anchor)
    {
        if (state.castingSpell)
        {
            if (CastsOwnRecipe(me, state.castingSpell))
                return true;
            state.castingSpell = 0;
        }

        if (!Errands::MayRun(me, anchor) || me->IsMoving() || (anchor && anchor != me && anchor->IsMoving()) ||
            me->IsNonMeleeSpellCasted(false))
            return false;

        uint32 const now = WorldTimer::getMSTime();
        if (now < state.nextTryMs)
            return false;
        state.nextTryMs = now + TRY_MS;

        for (BotShared::Supplies::Need const& need : BotShared::Supplies::ToMake(me))
        {
            uint32 const id = need.recipe->Id;
            auto failed = state.failedUntilMs.find(id);
            if (failed != state.failedUntilMs.end())
            {
                if (now < failed->second)
                    continue;
                state.failedUntilMs.erase(failed);
            }
            if (!PowerAllows(me, need))
                continue;

            SpellCastResult const result = BotShared::Crafting::Craft(me, need.recipe);
            if (result == SPELL_CAST_OK)
            {
                state.castingSpell = id;
                return true;
            }
            state.failedUntilMs[id] = now + FAIL_BACKOFF_MS;
            sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[CraftRun] %s cannot make %s with spell %u (cast result %u).",
                     me->GetName(), BotShared::Goods::Describe(need.goods).c_str(), id, uint32(result));
        }
        return false;
    }

    void Stop(Player* me, CraftRunState& state)
    {
        if (state.castingSpell && CastsOwnRecipe(me, state.castingSpell))
            me->InterruptSpell(CURRENT_GENERIC_SPELL);
        state.castingSpell = 0;
    }
}
}
