#include "Commander/Perception/ControlValue.h"
#include "Commander/Perception/CastValue.h"
#include "Commander/Perception/FearRisk.h"
#include "Commander/Perception/ManaWorth.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/ValueModel.h"
#include <algorithm>
#include <cstdio>

namespace Commander
{
namespace ControlValue
{
    namespace
    {
        float Risk(FightLoad::Result const& r)
        {
            if (r.tankDies || r.unfinished)
                return 1.0f;
            float const floor = FightLoad::TANK_FLOOR_PCT;
            return std::min(1.0f, std::max(0.0f, (floor - r.lowestTankPct) / floor));
        }

        float Loss(WorldModel const& world, FightLoad::Result const& r, float penalty)
        {
            return Value::Points(world, r.damage) + Risk(r) * penalty;
        }

        bool IsTarget(std::vector<EnemyFacts const*> const& targets, ObjectGuid guid)
        {
            return std::any_of(targets.begin(), targets.end(), [guid](EnemyFacts const* e) { return e->guid == guid; });
        }

        bool IsMelee(PartyMemberFacts const& m)
        {
            return m.alive && (m.isPet || m.role == ROLE_TANK || m.role == ROLE_MELEE_DPS);
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
             ControlAbility const& a, std::vector<EnemyFacts const*> const& targets, float aoeLoss)
    {
        Worth w;
        if (targets.empty())
            return w;
        for (EnemyFacts const* e : targets)
            w.sec = std::max(w.sec, ExpectedSec(world, a, *e));

        PartyMemberFacts const* tank = Tanks::Main(world);
        float const penalty = tank ? Value::DeathPenalty(*tank) : 0.0f;

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
                    f.arriveSec = std::max(f.arriveSec, left + ExpectedSec(world, a, *e));
                }
            f.sameFight = true;
        }
        FightLoad::Result const asIs = FightLoad::Simulate(fight);
        FightLoad::Result const ctrl = FightLoad::Simulate(controlled);
        w.damage = Value::Points(world, asIs.damage - ctrl.damage);
        w.danger = (Risk(asIs) - Risk(ctrl)) * penalty;

        if (tank)
        {
            for (EnemyFacts const* e : targets)
            {
                PartyMemberFacts const* victim = world.FindMember(e->victim);
                if (!victim || victim->guid == tank->guid || Tanks::IsTank(*victim))
                    continue;
                float const sec = std::min(ExpectedSec(world, a, *e), TAKEOVER_SEC);
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
        float const cooldownSec = a.cooldownMs / 1000.0f;
        if (cooldownSec > Value::GCD_SEC)
            w.cooldown = COOLDOWN_POINTS_PER_MIN * std::min(cooldownSec, Value::FightRemainingSec(world)) / 60.0f;
        w.aoe = std::max(0.0f, aoeLoss);

        if (a.fear)
        {
            if (tank)
            {
                float const base = Loss(world, ctrl, penalty);
                for (FearRisk::PackRisk const& risk : FearRisk::Of(world, member.pos, a.radius, a.durationSec, targets))
                {
                    FightLoad::Input withPack = controlled;
                    for (PackCreature const& c : risk.pack->members)
                        withPack.foes.push_back(FightLoad::FoeOf(c, *tank, ADD_ARRIVE_SEC));
                    w.adds += risk.chance * std::max(0.0f, Loss(world, FightLoad::Simulate(withPack), penalty) - base);
                }
            }
            for (PartyMemberFacts const& m : world.party)
                if (IsMelee(m) && IsTarget(targets, m.victim))
                    w.scatter += Value::LostAction(world, m, std::min(w.sec, REGATHER_SEC));
        }
        return w;
    }

    std::string Describe(Worth const& w)
    {
        char buf[300];
        snprintf(buf, sizeof(buf),
            "net %+.1f (%.0f s: damage %.1f danger %.1f victims %.1f | action %.1f reach %.1f mana %.1f cd %.1f aoe %.1f adds %.1f scatter %.1f)",
            w.Net(), w.sec, w.damage, w.danger, w.victims, w.action, w.reach, w.mana, w.cooldown, w.aoe, w.adds, w.scatter);
        return buf;
    }
}
}
