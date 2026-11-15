#include "Commander/Coordination/Doctrine/TacticCheck.h"
#include "Commander/Perception/Forecast/FightLoad.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Profiles/Tags.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/ControlValue.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Trace/Trace.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include <algorithm>
#include <cstdio>

namespace Commander
{
namespace TacticCheck
{
    namespace
    {
        std::string EnemyName(uint32 entry)
        {
            std::shared_ptr<CreatureProfile const> p = sProfileStore.GetCreature(entry);
            return p ? p->name : std::string("?");
        }

        std::string SpellName(uint32 spellId)
        {
            SpellEntry const* s = sSpellMgr.GetSpellEntry(spellId);
            return s ? s->SpellName[0] : std::string("?");
        }

        // The name the pull or the retreat captured (the pure brain reads no live objects).
        std::string BotName(TeamPlan const& plan, ObjectGuid bot)
        {
            for (PullMemberName const& n : plan.pull.names)
                if (n.guid == bot)
                    return n.name;
            for (PullMemberName const& n : plan.retreat.names)
                if (n.guid == bot)
                    return n.name;
            return Trace::Guid(bot);
        }

        bool HasControl(PartyMemberFacts const& m, uint32 spellId)
        {
            return std::any_of(m.controls.begin(), m.controls.end(),
                [spellId](ControlAbility const& a) { return a.spellId == spellId; });
        }

        bool InPatrols(WorldModel const& world, ObjectGuid g)
        {
            return std::any_of(world.patrols.begin(), world.patrols.end(), [g](PatrolFacts const& p) { return p.guid == g; });
        }

        // Why a kept hold ends now (nullptr: it stays).
        char const* DropWhy(WorldModel const& world, DoctrinePlan const& d, DoctrineHold const& h)
        {
            PartyMemberFacts const* bot = world.FindMember(h.bot);
            if (!bot || !bot->CanAct())
                return "the controller cannot act";
            if (!HasControl(*bot, h.spellId))
                return "the controller lost the spell";
            EnemyFacts const* e = world.FindEnemy(h.enemy);
            if (!e)
                return h.planned && InPatrols(world, h.enemy) ? nullptr : "it is gone";
            if (!e->health)
                return "it died";
            if (h.kind != ControlKind::Hold && d.Has(h.enemy, STAMP_NO_MEZ))
                return "a control that breaks on damage is not allowed on it now";
            // The pull's patrol hold (PatrolHold.h): not held before it reached the pile (user
            // 2026-11-13: then it is fought with the rest).
            if (h.planned && h.kind != ControlKind::Hold && !e->control.spellId)
                if (PartyMemberFacts const* tank = Tanks::Main(world))
                    if (PackResolver::Distance(tank->pos, e->pos) - e->boundingRadius < ControlValue::ClearOfPileYd(world))
                        return "it reached the pile before it was held, fought with the rest";
            return nullptr;
        }

        // `f` held by a control of `kind` lasting `durationSec` (header: held).
        void Hold(FightLoad::Input& fight, ObjectGuid enemy, ControlKind kind, float durationSec)
        {
            float rest = 0.0f;
            if (kind != ControlKind::Hold)
            {
                FightLoad::Input without = fight;
                without.foes.erase(std::remove_if(without.foes.begin(), without.foes.end(),
                    [enemy](FightLoad::Foe const& f) { return f.guid == enemy; }), without.foes.end());
                rest = without.foes.empty() ? 0.0f : FightLoad::Run(without, nullptr).fightSec;
            }
            for (FightLoad::Foe& f : fight.foes)
                if (f.guid == enemy)
                {
                    f.arriveSec = std::max(f.arriveSec, kind == ControlKind::Hold ? durationSec : rest);
                    f.sameFight = true;
                }
        }

        struct Pick
        {
            EnemyFacts const* enemy = nullptr;
            ControlValue::Option option;
            float score = 0.0f;
            FightLoad::Input fight;
            FightLoad::Result result;
        };
    }

    void Apply(WorldModel const& world, TeamPlan& plan, DoctrinePlan const& prev, DoctrinePlan& d, bool recheck)
    {
        // Kept holds.
        d.holdsWhy = prev.holdsWhy;
        for (DoctrineHold const& h : prev.holds)
        {
            if (char const* why = DropWhy(world, d, h))
            {
                if (EnemyFacts const* e = world.FindEnemy(h.enemy))
                    plan.notes.push_back("Hold on " + EnemyName(e->entry) + " ended: " + why + ".");
                // Its patrol group is no longer held outside: it joins the pile like any other.
                if (h.planned)
                    for (DoctrineGroup& g : d.groups)
                        if (g.Has(h.enemy))
                            g.holdOut = false;
                recheck = true;
                continue;
            }
            if (!d.HoldOn(h.enemy))
                d.holds.push_back(h);
        }

        PartyMemberFacts const* tank = Tanks::Main(world);
        if (recheck && tank && d.tactic != PileTactic::None && d.phase != FightPhase::Cleanup)
        {
            Trace::Scope scope("tacticcheck");
            FightLoad::Input fight;
            if (FightLoad::FromWorld(world, fight))
            {
                float const penalty = Value::DeathPenalty(*tank);
                for (DoctrineHold const& h : d.holds)
                    if (PartyMemberFacts const* b = world.FindMember(h.bot))
                        for (ControlAbility const& a : b->controls)
                            if (a.spellId == h.spellId)
                                Hold(fight, h.enemy, h.kind, a.durationSec);
                FightLoad::Result now = FightLoad::Simulate(fight, "with the holds");
                FightLoad::Result const asIs = now;
                std::vector<std::string> added;
                while (!FightLoad::Bearable(now))
                {
                    Pick best;
                    float const scoreNow = FightLoad::Score(now);
                    for (int pass = 0; pass < 2 && !best.enemy; ++pass)
                    {
                        bool const areaDealers = pass == 1;
                        for (DoctrineEnemy const& x : d.enemies)
                        {
                            EnemyFacts const* e = world.FindEnemy(x.guid);
                            if (!e || !e->acting || !e->health || e->control.spellId || d.HoldOn(x.guid) ||
                                (x.stamps & (STAMP_SWARM | STAMP_SELF_DESTRUCT | STAMP_PLAYER_TARGET | STAMP_OFF_TANK)) ||
                                Tier(x.tier) == Tier::Fodder)
                                continue;
                            bool const noMez = (x.stamps & STAMP_NO_MEZ) != 0;
                            for (ControlValue::Option const& o : ControlValue::OptionsFor(world, e->entry, e->creatureTypeMask))
                            {
                                PartyMemberFacts const* b = world.FindMember(o.bot);
                                // A one target spell already on another enemy: casting it here frees
                                // that one, which the estimate does not count (log 11-15 183927: the
                                // shackle went from the banshee to a gargoyle, the banshee came out).
                                bool const busy = !o.holding.IsEmpty() && o.holding != e->guid;
                                if (!o.ready || busy || o.areaDealer != areaDealers || o.kind == ControlKind::Fear ||
                                    (noMez && o.kind != ControlKind::Hold) || !b || Tanks::IsTank(*b) || d.HoldBy(o.bot))
                                    continue;
                                Pick p;
                                p.enemy = e;
                                p.option = o;
                                p.fight = fight;
                                Hold(p.fight, e->guid, o.kind, o.durationSec);
                                p.result = FightLoad::Run(p.fight, nullptr);
                                p.score = FightLoad::Score(p.result);
                                if (p.score > scoreNow && (!best.enemy || p.score > best.score))
                                    best = std::move(p);
                            }
                        }
                    }
                    if (!best.enemy)
                        break;
                    DoctrineHold h;
                    h.enemy = best.enemy->guid;
                    h.bot = best.option.bot;
                    h.spellId = best.option.spellId;
                    h.kind = best.option.kind;
                    h.points = std::max(0.0f, FightLoad::Loss(world, now, penalty) - FightLoad::Loss(world, best.result, penalty));
                    char buf[200];
                    snprintf(buf, sizeof(buf), "%s holds %s with %s", BotName(plan, h.bot).c_str(),
                        EnemyName(best.enemy->entry).c_str(), SpellName(h.spellId).c_str());
                    h.why = buf;
                    added.push_back(buf);
                    d.holds.push_back(h);
                    fight = std::move(best.fight);
                    now = best.result;
                }
                bool const bearable = FightLoad::Bearable(now);
                char buf[96];
                snprintf(buf, sizeof(buf), "%s%s", d.holds.empty() ? "" : (std::to_string(d.holds.size()) + " held, ").c_str(),
                    bearable ? "bearable" : "not bearable, no control left");
                std::string const why = buf;
                if (!added.empty())
                {
                    std::string list;
                    for (std::string const& a : added)
                        list += (list.empty() ? "" : ", ") + a;
                    plan.notes.push_back("Not bearable as is (" + FightLoad::Describe(asIs) + "): " + list +
                        (bearable ? "." : "; still not bearable."));
                }
                else if (!bearable && why != d.holdsWhy)
                    plan.notes.push_back("Not bearable (" + FightLoad::Describe(now) + ") and no control left that helps: fighting on.");
                d.holdsWhy = why;
                if (scope.On())
                {
                    scope.Label("%s", why.c_str());
                    std::string list;
                    for (DoctrineHold const& x : d.holds)
                        list += Trace::Format("%s{\"enemy\":\"%s\",\"bot\":\"%s\",\"spell\":%u,\"points\":%.2f}",
                            list.empty() ? "" : ",", Trace::Guid(x.enemy).c_str(), Trace::Guid(x.bot).c_str(), x.spellId, x.points);
                    scope.Data("{\"holds\":[" + list + "]}");
                }
            }
        }

        for (DoctrineEnemy& x : d.enemies)
            if (d.HoldOn(x.guid))
                x.stamps |= STAMP_MUST_CONTROL;
    }
}
}
