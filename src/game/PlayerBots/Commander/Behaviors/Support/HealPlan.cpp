#include "Commander/Behaviors/Support/HealPlan.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace HealPlan
{
    namespace
    {
        float Busy(Option const& o)
        {
            return std::max(GCD_SEC, o.castSec);
        }

        float Missing(Target const& t)
        {
            return std::max(0.0f, t.maxHealth - t.health);
        }

        float EffectiveAtLanding(Target const& t, Option const& o)
        {
            float const atLanding = std::min(t.maxHealth, Missing(t) + t.intake * o.castSec);
            return std::max(0.0f, std::min(o.heal, atLanding));
        }

        bool Tie(float a, float b)
        {
            return std::fabs(a - b) <= TIE_FRACTION * std::max(std::max(std::fabs(a), std::fabs(b)), 1.0f);
        }

        // The most mana-efficient option (heal per mana), -1 if none heals.
        int MostEfficient(std::vector<Option> const& options)
        {
            int best = -1;
            for (size_t i = 0; i < options.size(); ++i)
            {
                Option const& o = options[i];
                if (o.heal <= 0.0f || o.emergencyOnly)
                    continue;
                float const perMana = o.mana > 0.0f ? o.heal / o.mana : NO_LIMIT;
                if (best < 0)
                {
                    best = int(i);
                    continue;
                }
                Option const& b = options[best];
                float const bestPerMana = b.mana > 0.0f ? b.heal / b.mana : NO_LIMIT;
                if (perMana > bestPerMana)
                    best = int(i);
            }
            return best;
        }

        // Net mana per second over one lift + rest cycle (header).
        float CycleManaRate(Target const& t, Healer const& h, Option const& o, float cycleDeficit)
        {
            float const busy = Busy(o);
            float const net = o.heal / busy - t.intake;
            float const castTime = cycleDeficit / net;
            float const restTime = cycleDeficit / t.intake;
            float const spent = o.mana / busy * castTime;
            float const regained = h.regenCasting * (castTime + std::min(FIVE_SECOND_RULE_SEC, restTime)) +
                                   h.regen * std::max(0.0f, restTime - FIVE_SECOND_RULE_SEC);
            return (spent - regained) / (castTime + restTime);
        }
    }

    float ManaWeight(float tension, float manaPct)
    {
        float const lowMana = 1.0f - std::max(0.0f, std::min(100.0f, manaPct)) / 100.0f;
        return std::max(0.0f, std::min(1.0f, std::max(tension, lowMana)));
    }

    float ReserveHealth(Target const& t)
    {
        float const reserve = std::max(t.maxHealth * MIN_RESERVE_PCT / 100.0f, t.burst * BURST_SAFETY);
        return std::min(reserve, t.maxHealth * MAX_RESERVE_PCT / 100.0f);
    }

    Assessment Assess(Target const& t, Healer const& h, std::vector<Option> const& options)
    {
        Assessment a;
        float const missing = Missing(t);
        if (missing <= 0.0f || t.maxHealth <= 0.0f || options.empty())
            return a;

        a.reserve = ReserveHealth(t);
        if (t.intake > 0.0f)
        {
            a.timeToDie = t.health / t.intake;
            a.timeToReserve = std::max(0.0f, t.health - a.reserve) / t.intake;
        }
        if (t.intake > 0.0f && (t.health <= a.reserve || a.timeToDie < EMERGENCY_TTD_SEC))
        {
            a.need = Need::Emergency;
            return a;
        }

        // Only emergency heals (or none that heals): nothing to lift with.
        int const efficient = MostEfficient(options);
        if (efficient < 0)
            return a;
        Option const& e = options[efficient];
        float const minDeficit = std::min(e.heal, t.maxHealth);
        float const lead = t.intake * (e.castSec + h.tickSec);
        float const maxDeficit = std::max(minDeficit, t.maxHealth - a.reserve - lead);
        a.deficit = minDeficit + (maxDeficit - minDeficit) * h.manaWeight;
        if (t.waitDeficit > 0.0f)
            a.deficit = std::max(a.deficit, std::min(t.waitDeficit, maxDeficit));
        if (t.boostDeficit > 0.0f && t.boostDeficit <= maxDeficit)
            a.deficit = std::max(a.deficit, t.boostDeficit);

        if (t.chained)
            a.need = Need::Chain;
        else
            a.need = missing >= a.deficit ? Need::Lift : Need::Rest;
        return a;
    }

    int Pick(Target const& t, Healer const& h, Assessment const& a, std::vector<Option>& options)
    {
        for (Option& o : options)
            o.effective = EffectiveAtLanding(t, o);

        int best = -1;
        if (a.need == Need::Emergency)
        {
            // The biggest heal that lands in time; ties: less mana. Heals that do not pull
            // aggro first (the reserve can be well above the threat module's floor).
            for (int pass = 0; pass < 2 && best < 0; ++pass)
            {
                for (size_t i = 0; i < options.size(); ++i)
                {
                    Option const& o = options[i];
                    if (o.effective <= 0.0f || o.castSec + LANDING_MARGIN_SEC >= a.timeToDie || (pass == 0 && o.pulls))
                        continue;
                    if (best < 0 || (!Tie(o.effective, options[best].effective) && o.effective > options[best].effective) ||
                        (Tie(o.effective, options[best].effective) && o.mana < options[best].mana))
                        best = int(i);
                }
            }
            if (best >= 0)
                return best;
            // None lands in time: the fastest.
            for (size_t i = 0; i < options.size(); ++i)
            {
                Option const& o = options[i];
                if (o.effective <= 0.0f)
                    continue;
                if (best < 0 || o.castSec < options[best].castSec ||
                    (o.castSec == options[best].castSec && o.effective > options[best].effective))
                    best = int(i);
            }
            return best;
        }

        if (a.need != Need::Lift && a.need != Need::Chain)
            return -1;

        float const missing = Missing(t);
        auto overhealOk = [](Option const& o) { return o.heal - o.effective <= OVERHEAL_MAX * o.heal; };

        float bestRate = 0.0f;
        for (size_t i = 0; i < options.size(); ++i)
        {
            Option const& o = options[i];
            if (o.pulls || o.emergencyOnly || o.effective <= 0.0f || !overhealOk(o))
                continue;
            float const hps = o.heal / Busy(o);
            float rate;
            if (t.intake > 0.0f)
            {
                float const net = hps - t.intake;
                if (net <= 0.0f || missing / net > LIFT_MAX_SEC)
                    continue;
                rate = CycleManaRate(t, h, o, std::max(a.deficit, missing));
            }
            else
                rate = o.mana / o.effective;   // mana per health healed

            if (best >= 0)
            {
                float const bestHps = options[best].heal / Busy(options[best]);
                bool const better = Tie(rate, bestRate) ? hps > bestHps : rate < bestRate;
                if (!better)
                    continue;
            }
            best = int(i);
            bestRate = rate;
        }
        if (best >= 0)
            return best;

        // Nothing lifts in time: the fastest heal that does not overheal or pull.
        for (size_t i = 0; i < options.size(); ++i)
        {
            Option const& o = options[i];
            if (o.pulls || o.emergencyOnly || o.effective <= 0.0f || !overhealOk(o))
                continue;
            if (best < 0 || o.heal / Busy(o) > options[best].heal / Busy(options[best]))
                best = int(i);
        }
        return best;
    }

    float BoostedHeal(Option const& o, Boost const& b)
    {
        return o.heal * (1.0f + std::min(1.0f, std::max(0.0f, b.critChance)) * (CRIT_MULT - 1.0f));
    }

    int PickBoosted(Target const& t, Assessment const& a, std::vector<Option> const& options, int picked,
                    Boost const& b)
    {
        bool const emergency = a.need == Need::Emergency;
        if (!emergency && a.need != Need::Lift && a.need != Need::Chain)
            return -1;

        // The biggest heal that does not pull (with a cast time for an instant boost).
        int big = -1;
        for (size_t i = 0; i < options.size(); ++i)
        {
            Option const& o = options[i];
            if (o.pulls || o.heal <= 0.0f || (b.instant && o.castSec <= 0.0f) || (o.emergencyOnly && !emergency))
                continue;
            if (big < 0 || o.heal > options[big].heal)
                big = int(i);
        }
        if (big < 0)
            return -1;
        Option const& o = options[big];

        if (b.instant)
        {
            if (!emergency)
                return -1;
            bool const pickedInTime = picked >= 0 &&
                options[picked].castSec + LANDING_MARGIN_SEC < a.timeToDie;
            return pickedInTime ? -1 : big;
        }

        if (emergency && o.castSec + LANDING_MARGIN_SEC >= a.timeToDie)
            return -1;
        float const missingAtLanding = std::min(t.maxHealth, Missing(t) + t.intake * o.castSec);
        return missingAtLanding >= BoostedHeal(o, b) * (1.0f - OVERHEAL_MAX) ? big : -1;
    }
}
}
