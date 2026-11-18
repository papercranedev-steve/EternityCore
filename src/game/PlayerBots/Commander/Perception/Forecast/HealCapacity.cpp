#include "Commander/Perception/Forecast/HealCapacity.h"
#include "TeamBotAI.h"
#include "HealSpells.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include <algorithm>

namespace Commander
{
namespace HealCapacity
{
    namespace
    {
        bool HasCooldown(SpellEntry const* spell)
        {
            return spell->RecoveryTime > 0 || spell->CategoryRecoveryTime > 0;
        }

        void AddRate(Player* me, SpellEntry const* spell, std::vector<HealRate>& rates)
        {
            if (!spell || !HealSpells::IsSingleTargetHeal(spell))
                return;
            float const heal = HealSpells::Estimate(me, me, spell).direct;
            float const cost = HealSpells::ManaCost(me, spell);
            if (heal <= 0.0f || cost <= 0.0f)
                return;
            float const crit = std::min(100.0f, std::max(0.0f, me->GetSpellCritPercent(SpellSchools(spell->School)))) / 100.0f;
            float const expected = heal * (1.0f + CRIT_BONUS * crit);
            float const busySec = std::max(HealSpells::GCD_SEC, float(spell->GetCastTime(me)) / 1000.0f);
            HealRate r;
            r.hps = expected / busySec;
            r.perMana = expected / cost;
            r.castSec = float(spell->GetCastTime(me)) / 1000.0f;
            rates.push_back(r);
        }
    }

    std::vector<HealRate> Rates(TeamBotAI const* bot)
    {
        std::vector<HealRate> rates;
        for (SpellEntry const* spell : bot->m_spellListDirectHeal)
            AddRate(bot->me, spell, rates);
        // Flash of Light (HealSpells::HealingPart); Holy Shock has a cooldown, no steady rate.
        for (SpellEntry const* spell : bot->m_spellListIndirectHeal)
            if (!HasCooldown(spell))
                AddRate(bot->me, spell, rates);
        return rates;
    }

    std::vector<HealRate> PlayerRates(Player* player)
    {
        std::vector<HealRate> rates;
        for (auto const& known : player->GetSpellMap())
        {
            if (known.second.disabled || known.second.state == PLAYERSPELL_REMOVED)
                continue;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(known.first);
            if (!spell || spell->HasAttribute(SPELL_ATTR_PASSIVE) || spell->HasAttribute(SPELL_ATTR_DO_NOT_DISPLAY) ||
                spell->IsFitToFamily<SPELLFAMILY_DRUID, CF_DRUID_SWIFTMEND>())
                continue;
            // A plain heal, or one triggered without a cooldown (Flash of Light).
            bool const triggered = HealSpells::HealingPart(spell) != spell;
            if (triggered ? HasCooldown(spell) || !HealSpells::HealingPart(spell)->HasEffect(SPELL_EFFECT_HEAL)
                          : !spell->HasEffect(SPELL_EFFECT_HEAL))
                continue;
            AddRate(player, spell, rates);
        }
        return rates;
    }

    float MaxHps(std::vector<HealRate> const& rates)
    {
        float best = 0.0f;
        for (HealRate const& r : rates)
            best = std::max(best, r.hps);
        return best;
    }
}
}
