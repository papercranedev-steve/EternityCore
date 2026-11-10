#include "Commander/Coordination/Duties/TargetRules.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/RouteRisk.h"
#include "Commander/Perception/Values/SwitchCost.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Forecast/ThreatRoom.h"
#include "Commander/Perception/Forecast/ThreatView.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Trace/Trace.h"
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
        float const reach = melee ? MELEE_REACH : RANGED_REACH;
        // A melee bot that cannot walk to its target (the route passes an idle pack, or no
        // path): the melee stance module stops the chase, so it is a bot without a target.
        if (melee && current && PackResolver::Distance(bot.pos, current->pos) - current->boundingRadius > reach &&
            RouteRisk::Blocked(world, bot, *current))
            current = nullptr;
        float const range = current ? (melee ? MELEE_SWITCH_YD : RANGED_SWITCH_YD)
                                    : JOIN_FIGHT_YD;

        // Near aggro on the current target: ThreatBehavior stops hitting it, so staying is worth
        // nothing and any safe enemy is better than standing there.
        bool const stalled = current && !ThreatAllows(ctx, bot, *current, true);
        FocusScore const* c = current && !stalled ? &ctx.Score(*current) : nullptr;
        float const share = ctx.Share();
        // Switching from nothing loses nothing: at least one global cooldown of its own output.
        float const idleGain = Value::ContributionPerSec(world, bot) * Value::GCD_SEC;

        // Calculation trace: why each enemy was not taken (a bot without a target proposes
        // nothing at all, so this is the only record of it).
        std::string skipped;
        auto const skip = [&skipped](EnemyFacts const& e, std::string const& why)
        {
            if (Trace::On())
                skipped += Trace::Format("%s{\"g\":\"%s\",\"why\":\"%s\"}", skipped.empty() ? "" : ",",
                    Trace::Guid(e.guid).c_str(), why.c_str());
        };

        TargetPick best;
        TargetPick focus;
        float bestRank = 0.0f;
        for (EnemyFacts const* e : ctx.candidates)
        {
            if (e == current)
                continue;
            if (!ctx.Hittable(*e, bot))
            {
                skip(*e, "not hittable");
                continue;
            }
            float const distance = PackResolver::Distance(bot.pos, e->pos) - e->boundingRadius;
            // Doctrine KillFirst (TargetRules.h): the pile is the area spells', a melee bot may
            // go to the focus outside it (scratchpad 剧本1 方法2; the route check below is 方法3).
            bool const outlierFocus = melee && e->guid == ctx.plan.focus.enemy &&
                ctx.plan.doctrine.outliers == OutlierTactic::KillFirst && ctx.plan.doctrine.Has(e->guid, STAMP_OUTLIER);
            if (distance > (outlierFocus ? std::max(range, OUTLIER_MELEE_YD) : range))
            {
                skip(*e, Trace::On() ? Trace::Format("%.0f yd away", distance) : std::string());
                continue;
            }
            if (melee && distance > reach && RouteRisk::Blocked(world, bot, *e))
            {
                if (Trace::On())
                {
                    RouteFact const* r = world.FindRoute(bot.guid, e->guid);
                    skip(*e, Trace::Format("route: %s%s%s", r ? r->why : "straight line crosses an idle creature",
                        r && !r->blocker.IsEmpty() ? " " : "", r && !r->blocker.IsEmpty() ? Trace::Guid(r->blocker).c_str() : ""));
                }
                continue;
            }
            // Doctrine KillNow (RoleStamps.h): the self-destruct focus is a task: no threat rule
            // (it is dead or blown up before threat matters), no exchange.
            // Same for the charmer of a party member (AllyCharm.h).
            bool const killNow = e->guid == ctx.plan.focus.enemy &&
                                 ctx.plan.doctrine.Has(e->guid, STAMP_KILL_NOW | STAMP_FREE_ALLY);
            if (!killNow && !ThreatAllows(ctx, bot, *e, false))
            {
                skip(*e, "threat room");
                continue;
            }

            FocusScore const& f = ctx.Score(*e);
            float const switchCost = SwitchCost::Of(world, bot, *e, reach);
            TargetPick p;
            p.enemy = e;
            float rank;
            if (killNow)
            {
                p.forced = true;
                p.reason = ctx.plan.doctrine.Has(e->guid, STAMP_FREE_ALLY) ? "doctrine: kill it to free the ally"
                                                                           : "doctrine: kill it before it blows up";
                focus = p;
                continue;
            }
            if (c)
            {
                // Smith's rule exchange (FocusAssigner.h), this bot's share of it.
                p.benefit = (f.harmRate * c->killSec - c->harmRate * f.killSec) * share;
                p.cost = switchCost;
                if (p.benefit <= p.cost)
                {
                    skip(*e, "switch not worth it");
                    continue;
                }
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
        TargetPick pick;
        if (focus.enemy)
            pick = focus;
        else if (best.enemy)
            pick = best;
        else if (current)
        {
            pick.enemy = current;
            pick.reason = stalled ? "keep (near aggro, nothing safer)" : "keep";
        }
        if (Trace::On())
            Trace::Emit("target", Trace::Format("%s: %s %s", Trace::Guid(bot.guid).c_str(),
                    pick.enemy ? Trace::Guid(pick.enemy->guid).c_str() : "none", pick.reason),
                "{\"skipped\":[" + skipped + "]}");
        return pick;
    }
}
}
