/*
 * PartyBot Commander layer 1 (eyes): an enemy hitting a party member who is not a tank, and
 * what each way of getting it off them is worth, in points (team rescue, todo item 20,
 * .kiro/docs/2026-11-07/2026-11-07-team-rescue.md). Consequences only; RescueAssigner (brain)
 * picks one way per enemy for the whole party.
 *
 * Loose (LooseVictim): acting, not crowd controlled, ours (not on an ally only), its victim a
 * party member who is not a tank (Tanks::IsTank), not mind controlled, not a pet that taunts
 * by itself (PartyMemberFacts::tauntsItself: it growls the enemy back), not an ally.
 *
 * Every option: benefit = (loss doing nothing - loss with it) over the horizon, the losses of
 * Perception/Values/LooseEnemy.h (Value::MoveLoss + Value::HarassLoss, at most HORIZON_SEC and the
 * enemy's time to die). Urgent when the victim dies within the horizon of everything hitting
 * it (LooseValue::deadly).
 *   Taunt     a tank bot's taunt (PartyMemberFacts::taunts, ready): hits until it lands =
 *             walk into its range (distance - range) / RUN_YARDS_PER_SEC + TAUNT_REACTION_SEC;
 *             a temporary taunt (holdSec, Mocking Blow) also the hits after it fades until the
 *             tank's threat closes the gap. x the creature's taunt weight; not on taunt immune
 *             creatures, not without line of sight (EnemyFacts::InSightOf).
 *             cost = LostAction(GCD when it has one + walk) + reserve (share of its cooldown in
 *             RESERVE_HORIZON_SEC x Points(the next enemy's damage per second x TAUNT_RESERVE_SEC,
 *             NextTauntDps): a taunt used now is not there for the next one) + LostAction(STANCE_SWAP_COST_SEC) when it
 *             needs a stance swap.
 *   Threat    the tank walks over and wins it back with threat (LooseEnemy::WinBack);
 *             x the taunt weight; cost = SwitchCost::Of (0 when it already is the tank's target).
 *   ThreatDrop the victim's own threat drop (each of PartyMemberFacts::threatDrops, ready; only
 *             once the enemy hits it, user 2026-11-14): Fade, or Feint / Cower on that enemy
 *             (onTarget: within its range and sight). After the drop, the
 *             highest other row beating its lowered threat (110% in melee reach, 130% otherwise)
 *             takes the enemy; none: no option. benefit = what the victim does not take on top
 *             of that one (Value::MoveLoss; all hits if it is not a member) until Fade ends
 *             (Feint, Cower: HORIZON_SEC, the victim's ThreatRoom stop rule keeps it off) or
 *             the enemy dies. Urgent when the victim dies within FADE_URGENT_SEC of the hits on
 *             it. cost = LostAction(GCD) + ManaWorth::Points(mana) + cooldown points
 *             (Value::CooldownPoints, as for controls).
 *             A wipe (ThreatDropAbility::wipe, rogue Vanish since 2026-11-08: the Sap doc; hunter
 *             Feign Death since 2026-11-14, a resist is not counted;
 *             .kiro/docs/2026-11-08-sap.md 5): one cast frees the victim of every enemy on
 *             it, so the option counts all of them (whichever enemy's need it is offered
 *             in): each turns to its highest other row with any threat (MoveLoss onto it, 0
 *             when that is worse), or with nobody else on its list leaves the fight (all its
 *             hits, Value::HitLoss), until it dies or HORIZON_SEC. cost = LostAction(GCD) +
 *             cooldown points over the fight left + WIPE_NEXT_FIGHTS_SEC (as SurvivalValue).
 * Pure: WorldModel facts and static creature profiles.
 */

#ifndef MANGOS_COMMANDER_RESCUE_VALUE_H
#define MANGOS_COMMANDER_RESCUE_VALUE_H

#include "Commander/Perception/WorldModel.h"

class SpellEntry;

namespace Commander
{
    struct WorldModel;

    namespace RescueValue
    {
        static constexpr float HORIZON_SEC = 10.0f;            // = LooseEnemy::HORIZON_SEC
        static constexpr float TAUNT_REACTION_SEC = 0.5f;      // hits taken even when taunting at once
        static constexpr float TAUNT_RESERVE_SEC = 1.0f;       // taunt cooldown cost: this many seconds of enemy damage
        static constexpr float RESERVE_HORIZON_SEC = 10.0f;    // a cooldown this long or longer costs the whole reserve
        static constexpr float STANCE_SWAP_COST_SEC = 1.0f;    // = ThreatBehavior::STANCE_SWAP_COST_SEC
        static constexpr float RUN_YARDS_PER_SEC = 7.0f;
        static constexpr float MELEE_REACH = 5.0f;
        static constexpr float FADE_URGENT_SEC = 4.0f;
        // A long threat wipe cooldown (Vanish) is also missed in the next fights
        // (= SurvivalValue::NEXT_FIGHTS_SEC).
        static constexpr float WIPE_NEXT_FIGHTS_SEC = 120.0f;

        struct Option
        {
            float benefit = 0.0f;
            float cost = 0.0f;
            bool urgent = false;
            float Net() const { return benefit - cost; }
        };

        // Who `e` turns to when `who` has only `threatAfter` left on it: the highest other row
        // on its list beating that by the aggro rule (110% in melee reach, 130% otherwise).
        ThreatEntry const* NextHolder(EnemyFacts const& e, ObjectGuid who, float threatAfter);

        // The member `e` hits instead of a tank (see the header), null = not loose.
        PartyMemberFacts const* LooseVictim(WorldModel const& world, EnemyFacts const& e);

        // Seconds a taunt holds only through its aura (no SPELL_EFFECT_ATTACK_ME: Mocking Blow,
        // Challenging Shout / Roar; ThreatManager::tauntFadeOut takes the lent threat back),
        // 0 = the threat stays (Taunt, Growl).
        float TemporaryTauntSec(SpellEntry const* spell);
        // Seconds the enemy still hits its victim after a taunt (reaction, and for a temporary
        // taunt the time after it fades until `tankRate` closes `gap`), at most `horizon`.
        float TauntSecOnVictim(float gap, float tankRate, float holdSec, float horizon);
        // Damage per second of every acting enemy not under control (what an area taunt in
        // reserve covers: ThreatBehavior's Challenging Shout).
        float EnemyDps(WorldModel const& world);
        // The strongest other acting enemy not under control (`except` left out): what a single
        // taunt kept back would be used on next. Since 2026-11-08 the single taunt reserve
        // (was EnemyDps: with seven enemies the reserve outweighed any loose enemy on a
        // healthy victim, strat-20261108-200046: one Taunt in 83 s).
        float NextTauntDps(WorldModel const& world, ObjectGuid except);

        // A single taunt spent now is not there when an enemy on `tank` wipes its threat before
        // the taunt's cooldown ends (Forecast/ThreatWipe.h, 2026-11-26: Knock Away): the next on
        // that enemy's list takes its hits from the wipe (its middle estimate) until the taunt is
        // back, instead of only the taunt's reaction. Points summed over those enemies, `except`
        // (the one the taunt would be for) and taunt immune ones left out.
        float WipeReserve(WorldModel const& world, PartyMemberFacts const& tank, TauntAbility const& taunt,
                          ObjectGuid except);

        // `distance`: tank to enemy, yards. `tankRate`: LooseEnemy::TankRate.
        // `enemyDps`: NextTauntDps(world, e.guid).
        Option Taunt(WorldModel const& world, EnemyFacts const& e, PartyMemberFacts const& victim,
                     PartyMemberFacts const& tank, TauntAbility const& taunt, float tankRate, float distance,
                     float enemyDps);
        Option Threat(WorldModel const& world, EnemyFacts const& e, PartyMemberFacts const& victim,
                      PartyMemberFacts const& tank, float tankRate, float distance);
        // `a`: one of victim.threatDrops.
        Option ThreatDrop(WorldModel const& world, EnemyFacts const& e, PartyMemberFacts const& victim,
                          ThreatDropAbility const& a);
    }
}

#endif
