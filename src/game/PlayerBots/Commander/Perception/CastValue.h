/*
 * PartyBot Commander: value of stopping an enemy cast (value-scoring doc 5.1), in points.
 * Shared by InterruptAssigner (send someone or not) and InterruptBehavior (kick now or not).
 * Pure functions on WorldModel facts plus static spell / creature profiles.
 */

#ifndef MANGOS_COMMANDER_CAST_VALUE_H
#define MANGOS_COMMANDER_CAST_VALUE_H

#include "Common.h"
#include "ObjectGuid.h"

namespace Commander
{
    struct WorldModel;
    struct EnemyFacts;
    struct PartyMemberFacts;
    struct InterruptAbility;

    struct CastValue
    {
        uint32 spellId = 0;
        float points = 0.0f;        // what the party loses if the cast completes
        bool lethal = false;        // the cast would kill its target
    };

    namespace CastValues
    {
        // Fixed values where the effect has no health amount (tunable).
        static constexpr float SUMMON_POINTS = 5.0f;        // summon whose creature has no profile
        static constexpr float OTHER_POINTS = 1.0f;
        // Time between two casts of the same spell when the creature profile has no repeat delay.
        static constexpr float CAST_INTERVAL_SEC = 8.0f;
        // Run speed used for the cost of walking into range.
        static constexpr float RUN_YARDS_PER_SEC = 7.0f;
        // Mana burn / drain: one point of mana is worth this much healing.
        static constexpr float HEAL_PER_MANA = 2.0f;
        static constexpr float MANA_DRAIN_HEALER_MULT = 1.5f;
        // Summons without a spell duration are counted for this long; long ones are capped.
        static constexpr float SUMMON_DEFAULT_SEC = 30.0f;
        static constexpr float SUMMON_MAX_SEC = 60.0f;
        // EventAI health events: the spell counts for planning once the creature's health is
        // within this many percent above the event's threshold.
        static constexpr float HP_EVENT_WINDOW_PCT = 15.0f;
        // Control that breaks on damage, on a target that is being hit: lasts until the next hit.
        static constexpr float WAKE_ON_DAMAGE_SEC = 2.0f;

        // False if `enemy` is immune to every mechanic `ability` stops casts with (stun on a
        // stun-immune boss), or to its school. Unknown creature: true.
        bool CanStop(InterruptAbility const& ability, EnemyFacts const& enemy);

        // Points lost if `enemy` completes `spellId` on `target`. Empty target = not known yet
        // (planning, or no unit target): the enemy's victim, or the party average for control
        // and mana drain. A unit target outside the party (friendly NPC, an enemy) takes no
        // harmful value. Hand-written interrupt multipliers (spell and creature) included.
        //
        // Empty-target results are cached per (world model, world.version, enemy, spell) in a
        // thread_local table (CastValue.cpp, "Planning cache"): the interrupt assigner alone
        // asks for the same values hundreds of times per refresh. A hit returns exactly what
        // the uncached computation would. Calls with a target are never cached.
        CastValue Evaluate(WorldModel const& world, EnemyFacts const& enemy, uint32 spellId, ObjectGuid target);

        // Points lost to `count` creatures of `entry` at `level` joining the fight: their melee
        // while they exist (lifeMs, 0 = SUMMON_DEFAULT_SEC, capped at SUMMON_MAX_SEC) plus the
        // fight lasting as long as it takes us to kill them. < 0 when the creature is unknown.
        float SummonCreaturePoints(WorldModel const& world, uint32 level, uint32 entry, float count, uint32 lifeMs);

        // Drops every thread's planning cache at its next use. Call after the profile store
        // changes without a world refresh (`.partybot ai reload overrides`).
        void InvalidateCache();

        // Most valuable interruptible spell the enemy can cast next: its creature profile
        // (EventAI health events only near their threshold) and its last cast this fight.
        CastValue PlannedCast(WorldModel const& world, EnemyFacts const& enemy);

        // Benefit of interrupting the enemy's current cast (or, when it is not casting,
        // PlannedCast for planning), times the chance it lives long enough to complete it.
        CastValue InterruptBenefit(WorldModel const& world, EnemyFacts const& enemy);

        // Extra benefit of `ability` locking the school of `interruptedSpellId`: other casts of
        // that school the enemy would make during the lockout, plus the enemy walking into melee
        // when its main ranged spell is locked (that spell counts once: the larger of its blocked
        // casts and the walk-in term). 0 for stuns and silences (no lockout). Repeat
        // delays count from the latest cast start seen (EnemyFacts::castStarts, at refresh time).
        float LockoutBenefit(WorldModel const& world, EnemyFacts const& enemy,
                             InterruptAbility const& ability, uint32 interruptedSpellId);

        // Cost of `who` using `ability` on `enemy` to stop `cast`: its global cooldown, walking
        // into range, and the ability being on cooldown when this or another enemy casts
        // something worse than `cast` (repeat delays from the creature profiles, counted from
        // each spell's latest cast start seen; a cast running now is not delayed).
        float InterruptCost(WorldModel const& world, PartyMemberFacts const& who,
                            InterruptAbility const& ability, EnemyFacts const& enemy, CastValue const& cast);
    }
}

#endif
