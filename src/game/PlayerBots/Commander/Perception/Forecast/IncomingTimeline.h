/*
 * PartyBot Commander layer 1: the hits a party member (or pet) is about to take, one by one, in
 * the next HORIZON_SEC, instead of an average rate (user 2026-11-17: the heal module could not
 * tell whether the next hit kills). Facts only, no choice.
 *
 * Sources, per enemy that acts and is not controlled:
 *   melee   its victim only, in reach (a rooted one beyond Value::ROOTED_REACH_YD: none; a
 *           chasing one: from its arrival). Main and off hand swing timers, each swing waits
 *           while a cast or channel runs (Unit::UpdateMeleeAttackingState). An EventAI enrage
 *           still to come (EnemyFacts::enrages) boosts the swings from the moment the party's
 *           damage brings the enemy to its threshold (at once below it): haste shortens the
 *           period, damage done raises the hits (2026-11-17).
 *             expected  average weapon damage x armor x hit table (Value::MeleeAvoidance, crit
 *                       x2, crushing x1.5, SpellCaster::RollMeleeOutcomeAgainst)
 *             worst     maximum weapon damage x armor, never avoided
 *   cast    the spell it is casting now on this member (or an area spell reaching it): lands at
 *           cast end + flight; a channel spreads over its time left.
 *   list    creature_spells rows (EnemyFacts::spellList; for a C++ boss its ability sheet rows
 *           with estimated timers, Forecast/SheetTimers.h; a row once a fight fires at most
 *           once and adds no steady rate) simulated on the 1.2 s list check:
 *           a row fires at the first check after its cooldown, one non triggered cast per check
 *           and none while casting (CreatureAI::DoSpellsListCasts), then waits its average
 *           repeat delay. Its target from castTarget: the victim, second / last on the threat
 *           list, nearest / farthest, random (expected = share 1 / candidates, worst = full).
 *           expected x probability, worst = full. An area spell hits everyone in its radius
 *           (around the caster, or around a sure target; cones counted as circles); a chain
 *           spell (Cleave, Chain Lightning) its jumps from a sure target as Spell.cpp picks them.
 *   damage  SpellProfiles::Estimate (direct at landing, periodic spread over the duration),
 *           armor for physical spells.
 *
 * The member's damage taken factor and immunities apply. Measured damage the model does not
 * explain (DamageIntake rate above the model's steady rate: area damage of scripts, DoTs of
 * C++ boss scripts) is a steady residual rate. Beyond the horizon the steady rate goes on.
 *
 * Lockouts: the same casts, when they control the member (Lockout below), not as damage.
 *
 * Worst case = every hit lands at its maximum and the biggest hit so far crits (one bad roll).
 * Heals are added by the caller (AddHeal, AddHots): HealPlan counts the HoTs on the target,
 * SurvivalValue also the pending heals and the healers' rate.
 */

#ifndef MANGOS_COMMANDER_INCOMING_TIMELINE_H
#define MANGOS_COMMANDER_INCOMING_TIMELINE_H

#include "Common.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"
#include <limits>
#include <vector>

namespace Commander
{
    struct WorldModel;
    struct PartyMemberFacts;

namespace IncomingTimeline
{
    static constexpr float HORIZON_SEC = 8.0f;
    static constexpr uint32 SPELL_CHECK_MS = 1200;      // CreatureAI.cpp CREATURE_CASTING_DELAY
    static constexpr float SPELL_CRIT_MULT = 1.5f;
    static constexpr float MELEE_CRIT_MULT = 2.0f;
    static constexpr float CRUSH_MULT = 1.5f;
    static constexpr float NO_LIMIT = std::numeric_limits<float>::infinity();   // = ThreatView::NO_LIMIT

    struct Event
    {
        float atSec = 0.0f;
        float expected = 0.0f;      // damage; heals are negative
        float worst = 0.0f;
        float worstSpike = 0.0f;    // extra damage if this hit crits (worst case: the biggest one does)
        uint32 spellId = 0;         // 0 = melee swing, heals: the heal
        ObjectGuid source;
    };

    // What is done against the damage on top of the events (SurvivalValue: the healers' rate,
    // a defensive cooldown's damage factor for its duration).
    struct Relief
    {
        float healPerSec = 0.0f;    // from healDelaySec on
        float healDelaySec = 0.0f;
        float factor = 1.0f;        // damage x factor for the first factorSec seconds
        float factorSec = 0.0f;
    };

    // The member will lose control (stun, fear, sleep, silence, ...: SpellProfile::controls)
    // from startSec to endSec: a spell sure to reach it (its victim, a sure target, or in the
    // radius of an area spell around the caster; random targets do not count). From the
    // current cast and the spell list rows like the hits, within HORIZON_SEC (Ground Tremor:
    // boss doc G7). A healer cannot heal meanwhile (HealPlan::Healer::lockout*).
    struct Lockout
    {
        float startSec = 0.0f;
        float endSec = 0.0f;
        uint32 spellId = 0;
        ObjectGuid source;
        // SpellProfile::controlMechanics, without those the member is immune to
        // (PartyMemberFacts::immuneMechanics: Fear Ward, Berserker Rage): none left, no lockout.
        uint32 mechanics = 0;
        bool Fear() const { return (mechanics & (1u << MECHANIC_FEAR)) != 0; }
    };

    struct Timeline
    {
        std::vector<Event> events;  // sorted by atSec, within HORIZON_SEC
        std::vector<Lockout> lockouts;  // sorted by startSec
        float steadyDps = 0.0f;     // modelled expected damage per second (beyond the horizon)
        float residualDps = 0.0f;   // measured damage the model does not explain
        float headroom = 0.0f;      // max health - health: heals beyond it are overheal

        void AddHeal(float atSec, float amount, uint32 spellId = 0, ObjectGuid source = ObjectGuid());
        // Every tick of the periodic heals on `member` within the horizon.
        void AddHots(PartyMemberFacts const& member);

        // Seconds until `amount` health is lost (health + absorb = time to die). NO_LIMIT = never.
        float TimeToLose(float amount, bool worst, Relief const& relief = Relief()) const;
        // The first lockout still to come (startSec > 0), null = none.
        Lockout const* NextLockout() const;
    };

    Timeline Of(WorldModel const& world, PartyMemberFacts const& member);
}
}

#endif
