/*
 * PartyBot Commander layer 1 (eyes): what putting a control spell on enemies is worth, in
 * points, in combat (crowd control doc .kiro/docs/2026-11-07/2026-11-07-crowd-control.md
 * section 2). A fact for the crowd control assigner (brain, todo 10b) and the escape module's
 * Psychic Scream (cerebellum): "if this bot casts that on them, this happens". No choice here.
 *
 * Seconds out of the fight (ExpectedSec): the spell's duration, minus what an active control
 * on it still lasts; 0 for a control that breaks on damage while a party DoT ticks on it or
 * a player is hitting it.
 * HeldSec (since 2026-11-08, strat-20261108-232105: the mage kept polymorphing an insect of
 * 784 health, also as the last enemy, and the party stood waiting): a control that breaks on
 * damage (not a fear) holds the enemy only while the party has others to fight. Then the
 * tank wakes it (Coordination/Doctrine/ControlledEnd.h), so the extension is capped at the fight
 * without the targets (FightLoad of the others; 0 when nobody else is left). Of: worth.sec,
 * the arrival in the controlled run and the victims' seconds use it; OfPull: worth.sec.
 * Known limit: other controlled enemies in the rest count as waking when their control ends,
 * while ControlledEnd may wake one sooner.
 *
 * Benefit, the fight simulated twice (FightLoad::FromWorld: every enemy on the tank, the
 * healer bots healing), once as it is and once with the targets joining `sec` later:
 *   damage   Points(damage on the tank saved)
 *   danger   (Risk(as is) - Risk(controlled)) x the tank's death penalty;
 *            Risk = 1 if the tank dies or the fight does not end, else how far it drops below
 *            FightLoad::TANK_FLOOR_PCT (0 above, 1 at 0%)
 *   victims  targets hitting someone other than the tank: Value::MoveLoss + Value::HarassLoss
 *            for min(sec, TAKEOVER_SEC) (the tank would pick them up by then anyway)
 * Cost:
 *   action   Value::LostAction(max(cast time, GCD))
 *   reach    LostAction(its own cast or channel left + walk into range / run speed)
 *   mana     ManaWorth::Points(mana cost)
 *   cooldown Value::CooldownPoints(cooldown, fight left): COOLDOWN_POINTS_PER_MIN per minute, for cooldowns longer than
 *            a GCD: not there for a later emergency in this fight
 *   aoe      before a pull only (OfPull). In combat whether a pile is left to area spells is
 *            the doctrine's (Coordination/Doctrine/TacticalDoctrine.h, NoMez); AreaLoss was
 *            removed 2026-11-09 (doc .kiro/docs/2026-11-09-doctrine 1: it compared the control
 *            with the area spell of this moment only, so the control won while the pull
 *            still gathered and the pile then got no Blizzard)
 *   release  one target per caster (ControlAbility::oneTarget) and the bot holds another
 *            enemy with it (ControlAbility::holding): that one is freed when this lands. The
 *            controlled fight simulated once more with it back now instead of after what its
 *            control still lasts: Points(damage) + Risk difference x penalty. Just controlled:
 *            most of its duration is given up, near the end almost nothing (no stored time:
 *            what is left is the measure). Mana and time already spent on it do not count.
 *            (strat-20261108-140533: the priest moved its shackle three times in 9 s.)
 *
 *   fears only:
 *   adds     for each pack FearRisk::Of returns (the run over `sec`, its speed bonus, the
 *            runner's call for help on the way): chance x (Loss(controlled + the pack joining
 *            ADD_ARRIVE_SEC in) - Loss(controlled)), Loss = Points(damage) + Risk x penalty
 *   scatter  melee members (tank, melee, pets) on a target: LostAction(min(sec, REGATHER_SEC))
 *   other controls:
 *   scatter  members on a target turn to another enemy: LostAction(SwitchCost::SWITCH_SEC +
 *            their combo points on it x COMBO_POINT_SEC)
 *
 * Pure: WorldModel facts and static profiles (immunities from CreatureProfile).
 */

#ifndef MANGOS_COMMANDER_CONTROL_VALUE_H
#define MANGOS_COMMANDER_CONTROL_VALUE_H

#include "Commander/Perception/Forecast/FightLoad.h"
#include "Commander/Perception/WorldModel.h"
#include <string>
#include <vector>

namespace Commander
{
namespace ControlValue
{
    // Untested guesses (crowd control doc section 7).
    float const TAKEOVER_SEC = 4.0f;
    float const REGATHER_SEC = 3.0f;
    float const ADD_ARRIVE_SEC = 4.0f;

    struct Worth
    {
        float sec = 0.0f;
        // benefit
        float damage = 0.0f;
        float danger = 0.0f;
        float victims = 0.0f;
        // cost
        float action = 0.0f;
        float reach = 0.0f;
        float mana = 0.0f;
        float cooldown = 0.0f;
        float aoe = 0.0f;
        float adds = 0.0f;
        float scatter = 0.0f;
        float release = 0.0f;
        ObjectGuid released;    // the enemy `release` is for (empty: none)

        float Benefit() const { return damage + danger + victims; }
        float Cost() const { return action + reach + mana + cooldown + aoe + adds + scatter + release; }
        float Net() const { return Benefit() - Cost(); }
    };

    // `a` can work on a creature of this entry and type: creature type, mechanic and school
    // immunity. Range, sight and combat state are not checked.
    bool Affects(ControlAbility const& a, uint32 entry, uint32 creatureTypeMask);

    // The enemies of ours the spell would control: the target for a single target spell;
    // every acting enemy within the radius of `member` it affects for an area one.
    std::vector<EnemyFacts const*> Targets(WorldModel const& world, PartyMemberFacts const& member,
                                           ControlAbility const& a, EnemyFacts const* target);

    float ExpectedSec(WorldModel const& world, ControlAbility const& a, EnemyFacts const& enemy);

    // `fight`: FightLoad::FromWorld(world) (built once per refresh by the caller).
    Worth Of(WorldModel const& world, FightLoad::Input const& fight, PartyMemberFacts const& member,
             ControlAbility const& a, std::vector<EnemyFacts const*> const& targets);

    // Before a pull (Coordination/Situation/Pull/PullControl.h): `target` controlled as the fight starts.
    // `fight` = FightLoad::Build of the expected creatures, `asIs` its Simulate. Same benefit
    // as Of (damage, danger; nobody is hit yet: no victims); costs measured in that fight
    // (the live rates are 0 out of combat): action and aoe at the members' share of its
    // incoming damage, mana with ManaWorth's planned fight, cooldown against its length.
    // nearTarget = pulled creatures fought next to the target (0 when the fight is elsewhere:
    // brought back, opened from range): area spells there would wake it.
    Worth OfPull(WorldModel const& world, FightLoad::Input const& fight, FightLoad::Result const& asIs,
                 PartyMemberFacts const& member, ControlAbility const& a, ObjectGuid target, uint32 nearTarget);

    // "net +4.2 (8 s: damage 3.1 danger 2.0 victims 0.5 | action 0.6 mana 0.4 adds 0.4)"
    std::string Describe(Worth const& w);

    // ---- Battlefield groups (doc .kiro/docs/2026-11-09-doctrine/2026-11-13-battlefield-groups.md
    // 5, 7, 8): facts for the doctrine's group tactics and the fight spot.

    // One single target control a bot has that works on a creature (Affects): who could hold
    // it, with what, for how long. Range, sight and readiness are not filtered (`ready` is the
    // ability's; a plan made before the pull may count on it). Spells for creatures out of
    // combat only (Sap) are left out: the doctrine's controls are cast in the fight.
    struct Option
    {
        ObjectGuid bot;
        uint32 spellId = 0;
        ControlKind kind = ControlKind::Mez;
        float durationSec = 0.0f;
        float maxRange = 0.0f;
        uint32 castMs = 0;
        bool ready = false;
        bool areaDealer = false;    // the bot has area spells (the doctrine prefers other controllers)
        // ControlAbility::holding: a one target spell on (or landing on) this enemy now; cast on
        // another one it frees this one.
        ObjectGuid holding;
    };
    std::vector<Option> OptionsFor(WorldModel const& world, uint32 entry, uint32 creatureTypeMask);

    // How far from the pile (its centre, the main tank) a control that breaks on damage stays
    // unbroken: the largest area spell radius of the party (TeamLedger::areaRadius) +
    // PILE_SPREAD_YD (the pile spreads that far around the tank, TacticalDoctrine::CLUSTER_YD)
    // + CLEAR_MARGIN_YD. Without area spells: PILE_SPREAD_YD + CLEAR_MARGIN_YD (melee splash).
    float const PILE_SPREAD_YD = 10.0f;
    float const CLEAR_MARGIN_YD = 5.0f;
    float ClearOfPileYd(WorldModel const& world);

    // What losing the control on `enemy` now costs (the release rule above, for an enemy
    // already held): `fight` (FightLoad::FromWorld, the held enemy joining when its control
    // ends) against the same fight with it back now; Points(damage) + Risk difference x the
    // main tank's death penalty. 0 when it is not held or not in `fight`.
    float HeldWorth(WorldModel const& world, FightLoad::Input const& fight, ObjectGuid enemy);
}
}

#endif
