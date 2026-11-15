/*
 * PartyBot Commander layer 1 (eyes): a member about to die, and what each "keep it alive" spell
 * (PartyMemberFacts::survival, ClassKit::Survival) would save, in points (team rescue part 2,
 * todo item 20, .kiro/docs/2026-11-07/2026-11-07-team-rescue.md section 8). Consequences only;
 * RescueAssigner picks one per member.
 *
 * Danger (Of): damage per second dps = max(Value::IncomingOn x damageTakenMult, measured
 * takenDps), minus physical hits when immune to them (all when immune to both); heals (HealOn):
 * every healer bot that can act, has mana and is within HEAL_RANGE_YD, its best HealRate x
 * this member's share of the damage the members in its range take (Demand: one healer does
 * not heal everyone at full rate), the first landing after its cast time (+ what it casts
 * now). Heals being cast on it now (PendingHealOn, any member's PartyMemberFacts::pendingHeal):
 * one that lands before the member would die adds its amount (capped at the health missing
 * when it lands) to its health; HealOn's rate starts after it. Since 2026-11-15
 * (strat-20261115-173647: the tank at 1500 Shield Walled 0.6 s before a 3800 Greater Heal).
 * Time to die ttd: (health + absorb + pending heals) under the full dps until the first heal,
 * then dps - heal (no limit when the heals keep up). In danger: ttd < DANGER_SEC (the heal
 * module's emergency, HealPlan). Since 2026-11-08 (strat-20261108-161025: a mage at 8% under
 * seven enemies was not "in danger", each healer's whole rate counted on him from now on).
 *
 * Risk(t) = clamp(1 - t / HORIZON_SEC, 0, 1): dying in t seconds is likely when t is short, a
 * smooth stand-in for "the heals or the tank will not catch up in time".
 *
 * An option (Save): the member's damage per second x factor f for its duration d, plus extra
 * health A (absorb, Last Stand) or a heal (Lay on Hands, capped at the health missing):
 *   Absorb A, Reduce f = amount, Immune f = 0, ImmunePhysical f = 1 - physical share,
 *   ReducePhysical f = 1 - physical share x (1 - amount) (Evasion, Deterrence, Barkskin),
 *   Heal, ExtraHealth, DropThreat f = 1 - share of the enemies on it for HORIZON_SEC.
 * ttd' from the drain with f during d and without after.
 * benefit = DeathPenalty x (Risk(ttd) - Risk(ttd')) + Points(health it keeps within the horizon:
 *           the damage f takes off, the absorb or extra health used, the heal used)
 * A consumable (healthstone, healing potion: SurvivalAbility::itemId) has no option when the
 * fight is over (Value::FightRemainingSec) before the member would die; RescueAssigner ranks it
 * after every spell.
 * cost    = LostAction(caster, GCD) (not for an option off the global cooldown: potions,
 *           healthstones) [+ LostAction(caster, d): Ice Block]
 *           [+ LostAction(target, d) for a melee target that cannot attack: Blessing of Protection]
 *           + ManaWorth::Points(mana) + cooldown points (Value::CooldownPoints over
 *           fight left + NEXT_FIGHTS_SEC: a long cooldown is also missing in the next pulls)
 *           [+ Value::ResetCost: a spell on cooldown cast after its reset, Cold Snap / Preparation] [+ DropThreat: what the enemies' next holders take on top of it,
 *           Value::MoveLoss, for the horizon]
 * Not offered: not ready, out of range (ally spells), a target immune to the spell's mechanic
 * (Weakened Soul, Forbearance), a tank target for notOnTank spells, self-only spells on others.
 * Pure: WorldModel facts.
 */

#ifndef MANGOS_COMMANDER_SURVIVAL_VALUE_H
#define MANGOS_COMMANDER_SURVIVAL_VALUE_H

#include "Commander/Perception/Values/RescueValue.h"
#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    namespace SurvivalValue
    {
        // Untested guesses.
        static constexpr float DANGER_SEC = 4.0f;           // = HealPlan::EMERGENCY_TTD_SEC
        static constexpr float HORIZON_SEC = 8.0f;
        static constexpr float HEAL_RANGE_YD = 40.0f;
        static constexpr float NEXT_FIGHTS_SEC = 120.0f;

        struct Danger
        {
            float dps = 0.0f;           // damage per second on it
            float physicalDps = 0.0f;   // of which melee hits
            float enemyDps = 0.0f;      // of which from enemies whose victim it is (predicted)
            float heal = 0.0f;          // heals per second the healer bots can put on it (HealOn)
            float healDelaySec = 0.0f;  // until the first of them lands
            float pendingHeal = 0.0f;   // heals being cast on it that land before it dies (PendingHealOn)
            float ttd = 0.0f;           // seconds to die, infinity when the heals keep up
            bool InDanger() const { return ttd < DANGER_SEC; }
        };

        Danger Of(WorldModel const& world, PartyMemberFacts const& member);

        // Heals per second the healer bots can put on `member` taking `demand` damage per
        // second, and when the first lands (header).
        struct HealIn
        {
            float hps = 0.0f;
            float delaySec = 0.0f;
        };
        HealIn HealOn(WorldModel const& world, PartyMemberFacts const& member, float demand);
        // Health the heals being cast on `member` (PartyMemberFacts::pendingHeal, players too)
        // add: each that lands before `health` runs out under `dps`, capped at the health
        // missing then (header).
        float PendingHealOn(WorldModel const& world, PartyMemberFacts const& member, float health, float dps);
        // Damage per second a member takes now (predicted from the enemies on it, or measured).
        float Demand(WorldModel const& world, PartyMemberFacts const& member);
        // Seconds `health` lasts under `dps` with `heal` coming.
        float TimeToDieUnder(float health, float dps, HealIn const& heal);
        float Risk(float ttd);

        RescueValue::Option Save(WorldModel const& world, PartyMemberFacts const& member, Danger const& danger,
                                 PartyMemberFacts const& caster, SurvivalAbility const& a);
    }
}

#endif
