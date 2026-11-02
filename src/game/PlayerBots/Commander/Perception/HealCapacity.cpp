#include "Commander/Perception/HealCapacity.h"
#include "PartyBotAI.h"
#include "HealSpells.h"
#include "SpellEntry.h"
#include <algorithm>

namespace Commander
{
namespace HealCapacity
{
    std::vector<HealRate> Rates(PartyBotAI const* bot)
    {
        std::vector<HealRate> rates;
        Player* me = bot->me;
        for (SpellEntry const* spell : bot->m_spellListDirectHeal)
        {
            if (!spell || !HealSpells::IsSingleTargetHeal(spell))
                continue;
            float const heal = HealSpells::Estimate(me, me, spell).direct;
            float const cost = HealSpells::ManaCost(me, spell);
            if (heal <= 0.0f || cost <= 0.0f)
                continue;
            float const crit = std::min(100.0f, std::max(0.0f, me->GetSpellCritPercent(SpellSchools(spell->School)))) / 100.0f;
            float const expected = heal * (1.0f + CRIT_BONUS * crit);
            float const busySec = std::max(HealSpells::GCD_SEC, float(spell->GetCastTime(me)) / 1000.0f);
            HealRate r;
            r.hps = expected / busySec;
            r.perMana = expected / cost;
            rates.push_back(r);
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
