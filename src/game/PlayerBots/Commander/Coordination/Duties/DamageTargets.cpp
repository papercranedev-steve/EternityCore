#include "Commander/Coordination/Duties/TargetRules.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/RouteRisk.h"
#include "Commander/Perception/Values/SwitchCost.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Forecast/FinishForecast.h"
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
    namespace
    {
        // A caster bot (filler with a cast time): its next filler on `e` lands after the others
        // killed it (FinishForecast.h). Melee and instant damage dealers: never.
        bool FinishedFor(WorldModel const& world, PartyMemberFacts const& bot, EnemyFacts const& e)
        {
            if ((bot.role != ROLE_RANGE_DPS && bot.role != ROLE_HEALER) || !bot.fillerCastMs)
                return false;
            return FinishForecast::Finished(world, e, bot.guid, FinishForecast::FillerLandMs(bot, e));
        }
    }

    bool ThreatAllows(TargetContext const& ctx, PartyMemberFacts const& bot, EnemyFacts const& e, bool staying)
    {
        WorldModel const& world = ctx.world;
        // Doctrine NoHold (fodder, a trap swarm): the tank need not hold it, no threat rule.
        if (ctx.plan.doctrine.Has(e.guid, STAMP_NO_HOLD))
            return true;
        // Nobody fights it yet: the tank's first (its opener builds the lead), else the player's.
        // A real player tanking opens it himself (player combat doc T2, user 2026-11-28): before,
        // without a tank bot a damage dealer bot hit it first and held it. His orders and the
        // pull's target are forced before this rule (TargetAssigner.h).
        if (e.victim.IsEmpty())
            return !ctx.tankBot && !ctx.humanTank;
        if (e.victim == bot.guid)
            return true;
        // An off-tank holding it (doctrine OffTank.h) is its holder too.
        bool holder = false;
        if (ctx.plan.doctrine.OffTankHolds(e.victim, e.guid))
            holder = true;
        else if (PartyMemberFacts const* v = world.FindMember(e.victim))
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
        // (a healer in melee too: Doctrine/HealerDamage.h)
        bool const melee = bot.role == ROLE_MELEE_DPS || ctx.plan.healerDamage.Melee(bot.guid);
        float const reach = melee ? MELEE_REACH : RANGED_REACH;
        // A melee bot that cannot walk to its target (the route passes an idle pack, or no
        // path): the melee stance module stops the chase, so it is a bot without a target.
        if (melee && current && PackResolver::Distance(bot.pos, current->pos) - current->boundingRadius > reach &&
            RouteRisk::Blocked(world, bot, *current))
            current = nullptr;
        // On a melee straggler (Stragglers.h): any other enemy is taken as from nothing; it is
        // kept only when there is none.
        EnemyFacts const* straggler = nullptr;
        if (current && ctx.plan.doctrine.Has(current->guid, STAMP_STRAGGLER))
        {
            straggler = current;
            current = nullptr;
        }
        // On an off-tank's enemy (doctrine OffTank.h, killed last): the same.
        EnemyFacts const* offTanked = nullptr;
        if (current && ctx.plan.doctrine.Has(current->guid, STAMP_OFF_TANK))
        {
            offTanked = current;
            current = nullptr;
        }
        // Fodder (doctrine NO_HOLD: dies within seconds to the party) is the area spells' and the
        // quick hitters': a caster whose filler takes longer than a GCD only takes it when
        // nothing else is left (rivendare-20261128-230646: the mage started 2.5 s Frostbolts on
        // skeletons the rogue killed first, stopped them and switched, nine times).
        bool const slowCaster = (bot.role == ROLE_RANGE_DPS || bot.role == ROLE_HEALER) &&
                                bot.fillerCastMs > uint32(Value::GCD_SEC * 1000.0f);
        EnemyFacts const* fodder = nullptr;
        if (slowCaster && current && ctx.plan.doctrine.Has(current->guid, STAMP_NO_HOLD))
        {
            fodder = current;
            current = nullptr;
        }
        // The off-tanks' enemies it could hit: the one closest to dying when nothing else is left.
        EnemyFacts const* lastResort = nullptr;
        float const range = current ? (melee ? MELEE_SWITCH_YD : RANGED_SWITCH_YD)
                                    : JOIN_FIGHT_YD;

        // Near aggro on the current target: ThreatBehavior stops hitting it, so staying is worth
        // nothing and any safe enemy is better than standing there.
        bool const stalled = current && !ThreatAllows(ctx, bot, *current, true);
        // Its next cast would land on an enemy the others kill first (FinishForecast.h): staying
        // is worth nothing either, any enemy it can still hurt is better.
        bool const finished = current && !stalled && FinishedFor(world, bot, *current);
        FocusScore const* c = current && !stalled && !finished ? &ctx.Score(*current) : nullptr;
        float const share = ctx.Share();
        // Switching from nothing loses nothing: at least one global cooldown of its own output.
        float const idleGain = Value::ContributionPerSec(world, bot) * Value::GCD_SEC;

        // Calculation trace: why each enemy was not taken (a bot without a target proposes
        // nothing at all, so this is the only record of it).
        std::string skipped;
        char const* pass = "";      // "hard line: " in the second pass below
        auto const skip = [&skipped, &pass](EnemyFacts const& e, std::string const& why)
        {
            if (Trace::On())
                skipped += Trace::Format("%s{\"g\":\"%s\",\"why\":\"%s%s\"}", skipped.empty() ? "" : ",",
                    Trace::Guid(e.guid).c_str(), pass, why.c_str());
        };

        TargetPick best;
        TargetPick focus;
        float bestRank = 0.0f;
        // A melee bot whose every walk the margin blocks (SafeRoute::IDLE_MARGIN): a second pass
        // on the hard line (RouteFact::safeTight). Standing in the corridor helps nobody
        // (stockade-20261114-203133: the opposite cell's circle closed the door, the rogue
        // stood 9 yd from the tank's cell for 20 s).
        for (bool const tight : { false, true })
        {
            if (tight && (!melee || focus.enemy || best.enemy || current))
                break;
            if (tight)
                pass = "hard line: ";
            for (EnemyFacts const* e : ctx.candidates)
            {
                if (e == current)
                    continue;
                if (!ctx.Hittable(*e, bot))
                {
                    skip(*e, "not hittable");
                    continue;
                }
                // Doctrine (Stragglers.h): a melee straggler on its way is the tank's until it joins the pile.
                if (ctx.plan.doctrine.Has(e->guid, STAMP_STRAGGLER))
                {
                    skip(*e, "straggler: the tank's");
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
                if (melee && distance > reach && RouteRisk::Blocked(world, bot, *e, tight))
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
                // Doctrine (OffTank.h): an off-tank holds it, it is killed last.
                if (ctx.plan.doctrine.Has(e->guid, STAMP_OFF_TANK))
                {
                    if (!lastResort || e->health < lastResort->health)
                        lastResort = e;
                    skip(*e, "an off-tank holds it: last");
                    continue;
                }
                if (!killNow && FinishedFor(world, bot, *e))
                {
                    skip(*e, "the others kill it before its cast lands");
                    continue;
                }
                if (!killNow && slowCaster && ctx.plan.doctrine.Has(e->guid, STAMP_NO_HOLD))
                {
                    if (!fodder || e->health < fodder->health)
                        fodder = e;
                    skip(*e, "fodder: the area spells' and the quick hitters'");
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
                    p.reason = tight ? "party focus (walk on the hard line)" : "party focus";
                    focus = p;
                }
                else if (!best.enemy || rank > bestRank)
                {
                    p.reason = tight ? "only walk open, on the hard line" :
                               c ? "kill it first" : stalled ? "near aggro, switch" :
                               finished ? "the others finish its target" : "first target";
                    best = p;
                    bestRank = rank;
                }
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
            pick.reason = stalled ? "keep (near aggro, nothing safer)" :
                          finished ? "keep (the others finish it, nothing else)" : "keep";
        }
        else if (straggler)
        {
            pick.enemy = straggler;
            pick.reason = "keep (a straggler, nothing else)";
        }
        else if (fodder)
        {
            FocusScore const& f = ctx.Score(*fodder);
            pick.enemy = fodder;
            pick.benefit = std::max(f.harmRate * f.killSec * share, idleGain);
            pick.reason = "fodder, nothing else";
        }
        else if (offTanked)
        {
            pick.enemy = offTanked;
            pick.reason = "keep (an off-tank's, nothing else)";
        }
        else if (lastResort)
        {
            FocusScore const& f = ctx.Score(*lastResort);
            pick.enemy = lastResort;
            pick.benefit = std::max(f.harmRate * f.killSec * share, idleGain);
            pick.reason = "an off-tank's, nothing else left";
        }
        if (Trace::On())
            Trace::Emit("target", Trace::Format("%s: %s %s", Trace::Guid(bot.guid).c_str(),
                    pick.enemy ? Trace::Guid(pick.enemy->guid).c_str() : "none", pick.reason),
                "{\"skipped\":[" + skipped + "]}");
        return pick;
    }
}
}
