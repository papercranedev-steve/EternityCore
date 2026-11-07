#include "Commander/Perception/FleeStopValue.h"
#include "Commander/Perception/CastValue.h"
#include "Commander/Perception/ControlValue.h"
#include "Commander/Perception/FleePredictor.h"
#include "Commander/Perception/ManaWorth.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ValueModel.h"
#include <algorithm>
#include <cstdio>

namespace Commander
{
namespace FleeStopValue
{
    bool KillsInHold(WorldModel const& world, EnemyFacts const& e, FleeStopAbility const& a)
    {
        return a.holdSec > 0.0f && FleePredictor::HealthAtRun(e) <= Value::DealtDps(world) * a.holdSec;
    }

    Worth Of(WorldModel const& world, PartyMemberFacts const& member, FleeStopAbility const& a, EnemyFacts const& e)
    {
        Worth w;
        FleeFacts const& f = e.flee;
        if (!f.predicted || f.focusedPoints <= 0.0f)
            return w;
        if (a.holdSec > 0.0f)
            w.benefit = KillsInHold(world, e, a) ? f.focusedPoints : 0.0f;
        else if (a.slowPct > 0.0f)
        {
            float const reach = std::min(f.reachYd, f.runSpeed * (1.0f - a.slowPct) * f.runKillSec);
            w.benefit = std::max(0.0f, f.focusedPoints - FleePredictor::PointsWithin(world, e, reach));
        }
        if (w.benefit <= 0.0f)
            return w;

        w.action = Value::LostAction(world, member, std::max(a.castMs / 1000.0f, Value::GCD_SEC));
        float const range = a.radius > 0.0f ? a.radius : a.maxRange;
        float const walk = std::max(0.0f, PackResolver::Distance(member.pos, e.pos) - e.boundingRadius - range);
        if (walk > 0.0f)
            w.reach = Value::LostAction(world, member, walk / CastValues::RUN_YARDS_PER_SEC);
        if (a.manaCost)
            w.mana = ManaWorth::Points(world, member, float(a.manaCost));
        float const cooldownSec = a.cooldownMs / 1000.0f;
        if (cooldownSec > Value::GCD_SEC)
            w.cooldown = ControlValue::COOLDOWN_POINTS_PER_MIN * std::min(cooldownSec, Value::FightRemainingSec(world)) / 60.0f;
        return w;
    }

    std::string Describe(Worth const& w)
    {
        char buf[160];
        snprintf(buf, sizeof(buf), "net %+.1f (benefit %.1f | action %.1f reach %.1f mana %.1f cooldown %.1f)",
            w.Net(), w.benefit, w.action, w.reach, w.mana, w.cooldown);
        return buf;
    }
}
}
