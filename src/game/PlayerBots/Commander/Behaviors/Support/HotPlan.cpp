#include "Commander/Behaviors/Support/HotPlan.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace HotPlan
{
    int PickHot(std::vector<HotOption> const& options, bool preferBig)
    {
        float biggest = 0.0f;
        for (HotOption const& o : options)
            biggest = std::max(biggest, o.total);

        int best = -1;
        float bestRate = 0.0f;
        for (size_t i = 0; i < options.size(); ++i)
        {
            HotOption const& o = options[i];
            if (o.total <= 0.0f || o.total < MIN_RANK_SHARE * biggest)
                continue;
            float const effective = std::min(o.total, std::max(0.0f, o.need));
            if (o.total - effective > HealPlan::OVERHEAL_MAX * o.total)
                continue;
            float const rate = o.mana / effective;     // mana per health healed
            if (best >= 0)
            {
                HotOption const& b = options[best];
                bool better;
                if (preferBig)
                    better = o.total > b.total;
                else if (std::fabs(rate - bestRate) <= HealPlan::TIE_FRACTION * std::max(rate, bestRate))
                    better = o.total > b.total;
                else
                    better = rate < bestRate;
                if (!better)
                    continue;
            }
            best = int(i);
            bestRate = rate;
        }
        return best;
    }

    EatUse Decide(Eat const& e, float missing, HealPlan::Need need)
    {
        if (!e.known || !e.ready || e.heal <= 0.0f)
            return EatUse::None;
        if (need == HealPlan::Need::Emergency)
            return EatUse::Emergency;
        if (missing < e.heal * (1.0f - HealPlan::OVERHEAL_MAX))
            return EatUse::None;
        if (e.ticksLeft <= 1)
            return EatUse::Free;
        if (e.ticksLeft <= 2 && (need == HealPlan::Need::Lift || need == HealPlan::Need::Chain))
            return EatUse::Lift;
        return EatUse::None;
    }

    float WaitDeficit(Eat const& e)
    {
        if (!e.known || e.heal <= 0.0f || e.ticksLeft > 2 || e.cooldownSec > e.untilLastTickSec)
            return 0.0f;
        return e.heal * (1.0f - HealPlan::OVERHEAL_MAX);
    }
}
}
