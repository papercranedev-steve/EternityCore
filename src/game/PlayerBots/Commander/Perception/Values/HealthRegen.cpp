#include "Commander/Perception/Values/HealthRegen.h"
#include "Player.h"
#include "SpellAuras.h"
#include "World.h"
#include "PlayerAI.h"
#include <algorithm>

namespace Commander
{
namespace HealthRegen
{
    Rate OutOfCombat(Unit const* unit)
    {
        Rate r;
        Player const* player = unit ? unit->ToPlayer() : nullptr;
        if (!player || !player->IsAlive() || player->IsInCombat())
            return r;

        float const tickSec = float(REGEN_TIME_PLAYER_FULL) / 1000.0f;
        float const rate = sWorld.getConfig(CONFIG_FLOAT_RATE_HEALTH);
        float natural = player->GetRegenHPPerSpirit() * rate;
        for (auto const* aura : player->GetAurasByType(SPELL_AURA_MOD_HEALTH_REGEN_PERCENT))
            natural *= (100.0f + aura->GetModifier()->m_amount) / 100.0f;
        if (PlayerAI const* ai = player->AI())
            natural *= ai->RegenRate();
        if (!player->IsStandingUp())
            natural *= 1.5f;
        natural += rate * tickSec * (player->GetTotalAuraModifier(SPELL_AURA_MOD_HEALTH_REGEN_IN_COMBAT) / 5.0f);
        r.naturalPerSec = std::max(0.0f, natural / tickSec);

        for (auto const* aura : player->GetAurasByType(SPELL_AURA_MOD_REGEN))
        {
            int32 const period = aura->GetModifier()->periodictime;
            if (period <= 0)
                continue;
            r.foodPerSec += float(aura->GetModifier()->m_amount) * 1000.0f / float(period);
            int32 const left = aura->GetAuraDuration();
            r.foodSec = std::max(r.foodSec, left < 0 ? NO_LIMIT : float(left) / 1000.0f);
        }
        return r;
    }

    float SecondsToFull(Unit const* unit)
    {
        if (!unit || unit->GetHealth() >= unit->GetMaxHealth())
            return 0.0f;
        float const missing = float(unit->GetMaxHealth() - unit->GetHealth());
        Rate const r = OutOfCombat(unit);
        float const both = r.naturalPerSec + r.foodPerSec;
        if (r.foodPerSec > 0.0f && r.foodSec > 0.0f)
        {
            if (both * r.foodSec >= missing)
                return missing / both;
            float const rest = missing - both * r.foodSec;
            return r.naturalPerSec > 0.0f ? r.foodSec + rest / r.naturalPerSec : NO_LIMIT;
        }
        return r.naturalPerSec > 0.0f ? missing / r.naturalPerSec : NO_LIMIT;
    }
}
}
