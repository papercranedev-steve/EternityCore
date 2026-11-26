#include "Commander/Perception/Values/ControlValue.h"
#include "Commander/Perception/Values/CastValue.h"
#include "Commander/Perception/Forecast/FearRisk.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Profiles/Tags.h"
#include "Commander/Perception/Values/SwitchCost.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Trace/Trace.h"
#include <algorithm>
#include <cstdio>

namespace Commander
{
namespace ControlValue
{
    namespace
    {
        using FightLoad::Risk;
        using FightLoad::Loss;

        bool IsTarget(std::vector<EnemyFacts const*> const& targets, ObjectGuid guid)
        {
            return std::any_of(targets.begin(), targets.end(), [guid](EnemyFacts const* e) { return e->guid == guid; });
        }

        bool IsMelee(PartyMemberFacts const& m)
        {
            return m.alive && (m.isPet || m.role == ROLE_TANK || m.role == ROLE_MELEE_DPS);
        }

        // Seconds the party fights on without the foes `isTarget` picks (0: nobody else).
        template <typename IsTarget>
        float RestOf(FightLoad::Input const& fight, IsTarget isTarget, char const* why)
        {
            FightLoad::Input rest = fight;
            rest.foes.erase(std::remove_if(rest.foes.begin(), rest.foes.end(),
                [&isTarget](FightLoad::Foe const& f) { return isTarget(f.guid); }), rest.foes.end());
            return rest.foes.empty() ? 0.0f : FightLoad::Simulate(rest, why).fightSec;
        }

        float RestSec(FightLoad::Input const& fight, std::vector<EnemyFacts const*> const& targets, char const* why)
        {
            return RestOf(fight, [&targets](ObjectGuid g) { return IsTarget(targets, g); }, why);
        }

        // `held` back in `fight` now instead of when its control ends: Points(damage) + Risk
        // difference x penalty against `base` (Simulate of `fight`), at least 0. -1 = not held
        // in `fight` (not there, or joining at once).
        float FreedLoss(WorldModel const& world, FightLoad::Input const& fight, FightLoad::Result const& base,
                        ObjectGuid held, float penalty, char const* why)
        {
            FightLoad::Input freed = fight;
            bool found = false;
            for (FightLoad::Foe& f : freed.foes)
                if (f.guid == held && f.arriveSec > 0.0f)
                {
                    f.arriveSec = 0.0f;
                    found = true;
                }
            if (!found)
                return -1.0f;
            FightLoad::Result const back = FightLoad::Simulate(freed, why);
            return std::max(0.0f, Value::Points(world, back.damage - base.damage) + (Risk(back) - Risk(base)) * penalty);
        }
    }

    bool Affects(ControlAbility const& a, uint32 entry, uint32 creatureTypeMask)
    {
        if (a.creatureTypeMask && !(a.creatureTypeMask & creatureTypeMask))
            return false;
        std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(entry);
        if (!profile)
            return true;
        if (profile->schoolImmuneMask & (1u << a.school))
            return false;
        // Bit convention of Creature::IsImmuneToSpell(Effect): 1 << (mechanic - 1).
        return !a.mechanicMask || (a.mechanicMask & ~profile->mechanicImmuneMask) != 0;
    }

    std::vector<EnemyFacts const*> Targets(WorldModel const& world, PartyMemberFacts const& member,
                                           ControlAbility const& a, EnemyFacts const* target)
    {
        std::vector<EnemyFacts const*> out;
        if (a.radius <= 0.0f)
        {
            if (target)
                out.push_back(target);
            return out;
        }
        for (EnemyFacts const& e : world.enemies)
            if (!e.onAllyOnly && e.acting && Affects(a, e.entry, e.creatureTypeMask) &&
                PackResolver::Distance(e.pos, member.pos) <= a.radius + e.boundingRadius)
                out.push_back(&e);
        return out;
    }

    float ExpectedSec(WorldModel const& world, ControlAbility const& a, EnemyFacts const& enemy)
    {
        if (a.breaksOnDamage)
        {
            if (enemy.partyDotMs)
                return 0.0f;
            for (PartyMemberFacts const& m : world.party)
                if (!m.isBot && !m.isPet && m.alive && m.victim == enemy.guid)
                    return 0.0f;
        }
        float const left = enemy.control.spellId ? enemy.control.remainingMs / 1000.0f : 0.0f;
        return std::max(0.0f, a.durationSec - left);
    }

    Worth Of(WorldModel const& world, FightLoad::Input const& fight, PartyMemberFacts const& member,
             ControlAbility const& a, std::vector<EnemyFacts const*> const& targets)
    {
        Worth w;
        if (targets.empty())
            return w;
        Trace::Scope scope("control");
        // Held only while the party has someone else to fight (header: HeldSec).
        float const restSec = a.breaksOnDamage && !a.fear ? RestSec(fight, targets, "without the targets") : -1.0f;
        auto const extension = [&](EnemyFacts const& e)
        {
            float const left = e.control.spellId ? e.control.remainingMs / 1000.0f : 0.0f;
            float const sec = ExpectedSec(world, a, e);
            return restSec < 0.0f ? sec : std::max(0.0f, std::min(left + sec, restSec) - left);
        };
        for (EnemyFacts const* e : targets)
            w.sec = std::max(w.sec, extension(*e));

        PartyMemberFacts const* tank = Tanks::HitTaker(world);
        float const penalty = tank ? Value::DeathPenalty(*tank) : 0.0f;
        struct Record
        {
            Trace::Scope& scope;
            Worth const& w;
            float penalty;
            ~Record()
            {
                if (scope.On())
                    scope.Data(Trace::Format("{\"net\":%.2f,\"sec\":%.1f,\"penalty\":%.2f,\"terms\":\"%s\"}", w.Net(), w.sec,
                        penalty, Describe(w).c_str()));
            }
        } const record{ scope, w, penalty };
        if (scope.On())
        {
            std::string list;
            for (EnemyFacts const* e : targets)
                list += (list.empty() ? "" : ",") + Trace::Guid(e->guid);
            scope.Label("bot %s spell %u on %s%s", Trace::Guid(member.guid).c_str(), a.spellId, list.c_str(),
                a.holding.IsEmpty() ? "" : (" (holds " + Trace::Guid(a.holding) + ")").c_str());
        }

        // Benefit: the same fight with the targets joining later.
        FightLoad::Input controlled = fight;
        for (FightLoad::Foe& f : controlled.foes)
        {
            if (!IsTarget(targets, f.guid))
                continue;
            // Out until: what an active control still lasts plus the extension.
            for (EnemyFacts const* e : targets)
                if (e->guid == f.guid)
                {
                    float const left = e->control.spellId ? e->control.remainingMs / 1000.0f : 0.0f;
                    f.arriveSec = std::max(f.arriveSec, left + extension(*e));
                }
            f.sameFight = true;
        }
        FightLoad::Result const asIs = FightLoad::Simulate(fight, "as is");
        FightLoad::Result const ctrl = FightLoad::Simulate(controlled, "controlled");
        w.damage = Value::Points(world, asIs.damage - ctrl.damage);
        w.danger = (Risk(asIs) - Risk(ctrl)) * penalty;

        // The enemy this cast frees (header: release).
        if (a.oneTarget && !a.holding.IsEmpty() && !IsTarget(targets, a.holding))
        {
            float const loss = FreedLoss(world, controlled, ctrl, a.holding, penalty, "controlled, held one freed");
            if (loss >= 0.0f)
            {
                w.release = loss;
                w.released = a.holding;
            }
        }

        if (tank)
        {
            for (EnemyFacts const* e : targets)
            {
                PartyMemberFacts const* victim = world.FindMember(e->victim);
                if (!victim || victim->guid == tank->guid || Tanks::IsTank(*victim))
                    continue;
                float const sec = std::min(extension(*e), TAKEOVER_SEC);
                w.victims += Value::MoveLoss(world, *e, *victim, *tank, sec) + Value::HarassLoss(world, *e, *victim, sec);
            }
        }

        // Cost.
        w.action = Value::LostAction(world, member, std::max(a.castMs / 1000.0f, Value::GCD_SEC));
        float wait = member.castRemainingMs / 1000.0f;
        if (a.radius <= 0.0f)
        {
            float const distance = PackResolver::Distance(member.pos, targets.front()->pos) - targets.front()->boundingRadius;
            wait += std::max(0.0f, distance - a.maxRange) / CastValues::RUN_YARDS_PER_SEC;
        }
        w.reach = Value::LostAction(world, member, wait);
        w.mana = ManaWorth::Points(world, member, float(a.manaCost));
        // Members on a target that is not feared turn to another enemy (it stands there,
        // untouchable: polymorph, sap, shackle, banish); combo points on it are lost.
        if (!a.fear)
            for (PartyMemberFacts const& m : world.party)
                if (m.alive && IsTarget(targets, m.victim))
                    w.scatter += Value::LostAction(world, m, SwitchCost::SWITCH_SEC +
                        (m.comboTarget == m.victim ? m.comboPoints * SwitchCost::COMBO_POINT_SEC : 0.0f));
        w.cooldown = Value::CooldownPoints(a.cooldownMs / 1000.0f, Value::FightRemainingSec(world));

        if (a.fear)
        {
            if (tank)
            {
                float const base = Loss(world, ctrl, penalty);
                for (FearRisk::PackRisk const& risk : FearRisk::Of(world, member.pos, a.radius, w.sec, a.fearSpeedMult, targets))
                {
                    FightLoad::Input withPack = controlled;
                    for (PackCreature const& c : risk.pack->members)
                        withPack.foes.push_back(FightLoad::FoeOf(c, *tank, ADD_ARRIVE_SEC));
                    w.adds += risk.chance * std::max(0.0f, Loss(world, FightLoad::Simulate(withPack, "controlled, feared into a pack"), penalty) - base);
                }
            }
            for (PartyMemberFacts const& m : world.party)
                if (IsMelee(m) && IsTarget(targets, m.victim))
                    w.scatter += Value::LostAction(world, m, std::min(w.sec, REGATHER_SEC));
        }
        return w;
    }

    namespace
    {
        // Value::ContributionPerSec in a fight that has not started: the simulated incoming damage.
        float ContributionIn(WorldModel const& world, FightLoad::Result const& fight, PartyMemberFacts const& member)
        {
            return Value::ContributionPerSecAt(world, member, fight.damage / std::max(1.0f, fight.fightSec));
        }
    }

    Worth OfPull(WorldModel const& world, FightLoad::Input const& fight, FightLoad::Result const& asIs,
                 PartyMemberFacts const& member, ControlAbility const& a, ObjectGuid target, uint32 nearTarget)
    {
        Worth w;
        w.sec = a.durationSec;
        // Held only while the party fights the others (header: HeldSec).
        if (a.breaksOnDamage && !a.fear)
            w.sec = std::min(w.sec, RestOf(fight, [target](ObjectGuid g) { return g == target; }, "pull: without the target"));
        PartyMemberFacts const* tank = Tanks::HitTaker(world);
        float const penalty = tank ? Value::DeathPenalty(*tank) : 0.0f;

        FightLoad::Input controlled = fight;
        for (FightLoad::Foe& f : controlled.foes)
            if (f.guid == target)
            {
                f.arriveSec = std::max(f.arriveSec, w.sec);
                f.sameFight = true;
            }
        FightLoad::Result const ctrl = FightLoad::Simulate(controlled, "pull: controlled at the start");
        w.damage = Value::Points(world, asIs.damage - ctrl.damage);
        w.danger = (Risk(asIs) - Risk(ctrl)) * penalty;

        float const perSec = ContributionIn(world, asIs, member);
        w.action = perSec * std::max(a.castMs / 1000.0f, Value::GCD_SEC);
        ManaWorth::Fight planned;
        planned.sec = asIs.fightSec;
        planned.incomingDps = asIs.damage / std::max(1.0f, asIs.fightSec);
        w.mana = ManaWorth::Points(world, member, float(a.manaCost), planned);
        w.cooldown = Value::CooldownPoints(a.cooldownMs / 1000.0f, asIs.fightSec);

        // Fought next to it: the party's area spells keep off the pack while it sleeps.
        if (a.breaksOnDamage && nearTarget > 0)
        {
            float const sec = std::min(w.sec, asIs.fightSec);
            for (PartyMemberFacts const& m : world.party)
            {
                if (!m.CanAct() || m.areaDpsPerTarget <= 0.0f || m.fillerDps <= 0.0f)
                    continue;
                float const extra = float(nearTarget + 1) * m.areaDpsPerTarget - m.fillerDps;
                if (extra > 0.0f)
                    w.aoe += ContributionIn(world, asIs, m) * extra * sec / m.fillerDps;
            }
        }
        return w;
    }

    std::vector<Option> OptionsFor(WorldModel const& world, uint32 entry, uint32 creatureTypeMask)
    {
        std::vector<Option> out;
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.isBot || m.isPet || !m.alive)
                continue;
            for (ControlAbility const& a : m.controls)
            {
                if (a.radius > 0.0f || a.peacefulOnly || !Affects(a, entry, creatureTypeMask))
                    continue;
                Option o;
                o.bot = m.guid;
                o.spellId = a.spellId;
                o.kind = a.Kind();
                o.durationSec = a.durationSec;
                o.maxRange = a.maxRange;
                o.castMs = a.castMs;
                o.ready = a.ready && m.CanAct();
                o.areaDealer = !m.areaSpells.empty();
                if (a.oneTarget)
                    o.holding = a.holding;
                out.push_back(o);
            }
        }
        return out;
    }

    float ClearOfPileYd(WorldModel const& world)
    {
        return Tags::LedgerOf(world).areaRadius + PILE_SPREAD_YD + CLEAR_MARGIN_YD;
    }

    float HeldWorth(WorldModel const& world, FightLoad::Input const& fight, ObjectGuid enemy)
    {
        PartyMemberFacts const* tank = Tanks::HitTaker(world);
        float const penalty = tank ? Value::DeathPenalty(*tank) : 0.0f;
        Trace::Scope scope("held");
        if (scope.On())
            scope.Label("%s", Trace::Guid(enemy).c_str());
        FightLoad::Result const base = FightLoad::Simulate(fight, "held");
        float const loss = std::max(0.0f, FreedLoss(world, fight, base, enemy, penalty, "freed now"));
        if (scope.On())
            scope.Data(Trace::Format("{\"points\":%.2f}", loss));
        return loss;
    }

    std::string Describe(Worth const& w)
    {
        char buf[320];
        snprintf(buf, sizeof(buf),
            "net %+.1f (%.0f s: damage %.1f danger %.1f victims %.1f | action %.1f reach %.1f mana %.1f cd %.1f aoe %.1f adds %.1f scatter %.1f release %.1f)",
            w.Net(), w.sec, w.damage, w.danger, w.victims, w.action, w.reach, w.mana, w.cooldown, w.aoe, w.adds, w.scatter, w.release);
        return buf;
    }
}
}
