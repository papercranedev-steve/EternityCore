/*
 * PartyBot Commander layer 1 (eyes): what putting a control spell on enemies is worth, in
 * points, in combat (crowd control doc .kiro/docs/2026-11-07/2026-11-07-crowd-control.md
 * section 2). A fact for the crowd control assigner (brain, todo 10b) and the escape module's
 * Psychic Scream (cerebellum): "if this bot casts that on them, this happens". No choice here.
 *
 * Seconds out of the fight (ExpectedSec): the spell's duration, minus what an active control
 * on it still lasts; 0 for a control that breaks on damage while a party DoT ticks on it or
 * a player is hitting it.
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
 *   cooldown COOLDOWN_POINTS_PER_MIN x min(cooldown, fight left) / 60 for cooldowns longer than
 *            a GCD: not there for a later emergency in this fight
 *   aoe      given by the caller (AreaLoss for a control that breaks on damage): area damage
 *            the party gives up to keep the targets unbroken
 *
 * AreaLoss(x, sec): every damage dealer bot with an area spell (PartyMemberFacts::
 * areaDpsPerTarget): n = enemies within its radius of x, x included, that an area spell may hit
 * now; while x must stay unbroken that cluster gets no area spell, so it loses
 * max(0, n x per target - filler) damage per second for min(sec, fight left), as seconds of
 * its own output (Value::LostAction, the same valuation as AreaDamageBehavior). The live
 * threat limit of AreaDamageBehavior is not counted (early in a fight the loss is too high).
 *   fears only:
 *   adds     for each pack FearRisk::Of returns: chance x (Loss(controlled + the pack joining
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

#include "Commander/Perception/FightLoad.h"
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
    float const COOLDOWN_POINTS_PER_MIN = 3.0f;
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

        float Benefit() const { return damage + danger + victims; }
        float Cost() const { return action + reach + mana + cooldown + aoe + adds + scatter; }
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
             ControlAbility const& a, std::vector<EnemyFacts const*> const& targets, float aoeLoss);

    // Area damage given up while `enemy` must not be hit for `sec` (header).
    float AreaLoss(WorldModel const& world, EnemyFacts const& enemy, float sec);

    // "net +4.2 (8 s: damage 3.1 danger 2.0 victims 0.5 | action 0.6 mana 0.4 adds 0.4)"
    std::string Describe(Worth const& w);
}
}

#endif
