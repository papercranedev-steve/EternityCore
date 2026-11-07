/*
 * PartyBot Commander layer 1 (eyes): a member about to die, and what each "keep it alive" spell
 * (PartyMemberFacts::survival, ClassKit::Survival) would save, in points (team rescue part 2,
 * todo item 20, .kiro/docs/2026-11-07/2026-11-07-team-rescue.md section 8). Consequences only;
 * RescueAssigner picks one per member.
 *
 * Danger (Of): damage per second dps = max(Value::IncomingOn x damageTakenMult, measured
 * takenDps), minus physical hits when immune to them (all when immune to both); heals per second
 * heal = every healer bot that can act, has mana and is within HEAL_RANGE_YD, its best
 * HealRate. drain = dps - heal. Time to die ttd = (health + absorb) / drain (no limit when the
 * heals keep up). In danger: ttd < DANGER_SEC (the heal module's emergency, HealPlan).
 *
 * Risk(t) = clamp(1 - t / HORIZON_SEC, 0, 1): dying in t seconds is likely when t is short, a
 * smooth stand-in for "the heals or the tank will not catch up in time".
 *
 * An option (Save): the member's damage per second x factor f for its duration d, plus extra
 * health A (absorb, Last Stand) or a heal (Lay on Hands, capped at the health missing):
 *   Absorb A, Reduce f = amount, Immune f = 0, ImmunePhysical f = 1 - physical share,
 *   Heal, ExtraHealth, DropThreat f = 1 - share of the enemies on it for HORIZON_SEC.
 * ttd' from the drain with f during d and without after.
 * benefit = DeathPenalty x (Risk(ttd) - Risk(ttd')) + Points(health it keeps within the horizon:
 *           the damage f takes off, the absorb or extra health used, the heal used)
 * cost    = LostAction(caster, GCD) [+ LostAction(caster, d): Ice Block]
 *           [+ LostAction(target, d) for a melee target that cannot attack: Blessing of Protection]
 *           + ManaWorth::Points(mana) + cooldown points (ControlValue::COOLDOWN_POINTS_PER_MIN x
 *           min(cooldown, fight left + NEXT_FIGHTS_SEC) / 60: a long cooldown is also missing in
 *           the next pulls) [+ DropThreat: what the enemies' next holders take on top of it,
 *           Value::MoveLoss, for the horizon]
 * Not offered: not ready, out of range (ally spells), a target immune to the spell's mechanic
 * (Weakened Soul, Forbearance), a tank target for notOnTank spells, self-only spells on others.
 * Pure: WorldModel facts.
 */

#ifndef MANGOS_COMMANDER_SURVIVAL_VALUE_H
#define MANGOS_COMMANDER_SURVIVAL_VALUE_H

#include "Commander/Perception/RescueValue.h"
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
            float heal = 0.0f;          // heals per second the healer bots can put on it
            float ttd = 0.0f;           // seconds to die, infinity when the heals keep up
            bool InDanger() const { return ttd < DANGER_SEC; }
        };

        Danger Of(WorldModel const& world, PartyMemberFacts const& member);
        float Risk(float ttd);

        RescueValue::Option Save(WorldModel const& world, PartyMemberFacts const& member, Danger const& danger,
                                 PartyMemberFacts const& caster, SurvivalAbility const& a);
    }
}

#endif
