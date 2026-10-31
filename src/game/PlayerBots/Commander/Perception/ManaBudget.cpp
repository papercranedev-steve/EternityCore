#include "Commander/Perception/ManaBudget.h"
#include <algorithm>

namespace Commander
{
namespace ManaBudget
{
    namespace
    {
        // Linear from (x0, y0) to (x1, y1), flat outside.
        float Lerp(float x, float x0, float y0, float x1, float y1)
        {
            if (x <= x0)
                return y0;
            if (x >= x1)
                return y1;
            return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
        }

        // Tension above which there is "no mana at all" (avoids dividing by 0).
        float const OUT_OF_MANA_TENSION = 100.0f;
    }

    float NeededHealing(float incomingDps, float fightSec, float headroom, uint32 healers)
    {
        float const total = std::max(0.0f, incomingDps * std::max(0.0f, fightSec) - std::max(0.0f, headroom));
        return total / float(std::max(1u, healers));
    }

    float NeededMana(float neededHealing, float healPerMana)
    {
        float const perMana = std::max(0.1f, healPerMana) * (1.0f - OVERHEAL);
        return std::max(0.0f, neededHealing) / perMana;
    }

    float Tension(float neededMana, float mana)
    {
        if (neededMana <= 0.0f)
            return 0.0f;
        if (mana <= 0.0f)
            return OUT_OF_MANA_TENSION;
        return std::min(OUT_OF_MANA_TENSION, neededMana / mana);
    }

    float HealCeilingPct(float tension, bool tankLike)
    {
        if (tension <= 1.0f)
            return 100.0f;
        if (tankLike)
            return Lerp(tension, 1.5f, 100.0f, 2.0f, CEILING_TANK_AT_2_0);
        if (tension <= 1.5f)
            return Lerp(tension, 1.0f, 100.0f, 1.5f, CEILING_OTHER_AT_1_5);
        return Lerp(tension, 1.5f, CEILING_OTHER_AT_1_5, 2.0f, CEILING_OTHER_AT_2_0);
    }

    float Scarcity(float tension)
    {
        return std::min(1.0f, std::max(0.0f, tension - 1.0f));
    }

    float SavedManaHealth(float mana, float healPerMana, float tension)
    {
        return std::max(0.0f, mana) * healPerMana * SAVED_MANA_DISCOUNT * Scarcity(tension);
    }

    bool WantsLowManaWarning(float manaPct, float tension)
    {
        // tension = mana needed / mana left, i.e. fight time / time the mana lasts.
        return manaPct >= 0.0f && manaPct < WARN_MANA_PCT && tension > WARN_TENSION;
    }
}
}
