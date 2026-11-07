#include "Commander/Coordination/TargetRules.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/RouteRisk.h"
#include "Commander/Perception/SwitchCost.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/ThreatRoom.h"
#include "Commander/Perception/ThreatView.h"
#include "Commander/Perception/ValueModel.h"
#include <algorithm>

// Damage dealers' targets: TargetRules.h "Damage dealers".
namespace Commander
{
namespace DamageTargets
{
    bool ThreatAllows(TargetContext const& ctx, PartyMemberFacts const& bot, EnemyFacts const& e, bool staying)
    {
        WorldModel const& world = ctx.world;
        // Nobody fights it yet: the tank's first (its opener builds the lead), else the player's.
        if (e.victim.IsEmpty())
            return !ctx.tankBot;
        if (e.victim == bot.guid)
            return true;
        bool holder = false;
        if (PartyMemberFacts const* v = world.FindMember(e.victim))
            holder = Tanks::IsHolder(*v);
        else if (AllyFacts const* a = world.FindAlly(e.victim))
            holder = a->tankLike;
        // On a healer or another damage dealer: taking it is not worse.
        if (!holder)
            return true;
        float const room = ThreatView::Headroom(e, bot.guid);
        float const needed = staying ? ThreatRoom::Stop(world, bot.guid, e, ctx.tickSec)
                                     : ThreatRoom::Enter(world, bot.guid, e, ctx.tickSec);
        return room >= needed;
    }

    TargetPick Choose(TargetContext const& ctx, PartyMemberFacts const& bot, EnemyFacts const* current)
    {
        WorldModel const& world = ctx.world;
        bool const melee = bot.role == ROLE_MELEE_DPS;
        float const range = current ? (melee ? MELEE_SWITCH_YD : RANGED_SWITCH_YD)
                                    : (melee ? MELEE_START_YD : RANGED_START_YD);
        float const reach = melee ? MELEE_REACH : RANGED_REACH;

        // Near aggro on the current target: ThreatBehavior stops hitting it, so staying is worth
        // nothing and any safe enemy is better than standing there.
        bool const stalled = current && !ThreatAllows(ctx, bot, *current, true);
        FocusScore const* c = current && !stalled ? &ctx.Score(*current) : nullptr;
        float const share = ctx.Share();
        // Switching from nothing loses nothing: at least one global cooldown of its own output.
        float const idleGain = Value::ContributionPerSec(world, bot) * Value::GCD_SEC;

        TargetPick best;
        TargetPick focus;
        float bestRank = 0.0f;
        for (EnemyFacts const* e : ctx.candidates)
        {
            if (e == current || !ctx.Hittable(*e, bot))
                continue;
            float const distance = PackResolver::Distance(bot.pos, e->pos) - e->boundingRadius;
            if (distance > range)
                continue;
            if (melee && distance > reach && RouteRisk::CrossesIdle(world, bot.pos, e->pos))
                continue;
            if (!ThreatAllows(ctx, bot, *e, false))
                continue;

            FocusScore const& f = ctx.Score(*e);
            float const switchCost = SwitchCost::Of(world, bot, *e, reach);
            TargetPick p;
            p.enemy = e;
            float rank;
            if (c)
            {
                // Smith's rule exchange (FocusAssigner.h), this bot's share of it.
                p.benefit = (f.harmRate * c->killSec - c->harmRate * f.killSec) * share;
                p.cost = switchCost;
                if (p.benefit <= p.cost)
                    continue;
                rank = p.benefit - p.cost;
            }
            else
            {
                p.benefit = std::max(f.harmRate * f.killSec * share, idleGain);
                p.cost = 0.0f;
                rank = p.benefit - switchCost;
            }
            if (e->guid == ctx.plan.focus.enemy)
            {
                p.reason = "party focus";
                focus = p;
            }
            else if (!best.enemy || rank > bestRank)
            {
                p.reason = c ? "kill it first" : (stalled ? "near aggro, switch" : "first target");
                best = p;
                bestRank = rank;
            }
        }
        if (focus.enemy)
            return focus;
        if (best.enemy)
            return best;
        if (current)
        {
            TargetPick keep;
            keep.enemy = current;
            keep.reason = stalled ? "keep (near aggro, nothing safer)" : "keep";
            return keep;
        }
        return TargetPick();
    }
}
}
