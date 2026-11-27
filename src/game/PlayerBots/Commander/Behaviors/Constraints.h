/*
 * PartyBot Commander layer 3: the arbiter's check of the brain's constraints.
 *
 * A constraint forbids something (directive types doc,
 * .kiro/docs/2026-11-07/2026-11-07-directive-types.md); the queries and their exceptions are
 * on TeamPlan (EngageBlock, KeepsUnbroken, ForbidsGapCloser, ForbidsRest). Modules read them
 * while building proposals, so they can pick something else; this check is the backstop that
 * drops a proposal breaking one, whichever module made it (Arbiter::Run, logged as Forbidden).
 *
 *   Attack, Chase, PetAttack   the target: TeamPlan::EngageBlock (patrol path, player's
 *                              "leave it alone"; the bot's own intercept is allowed)
 *   Cast with a charge effect  TeamPlan::ForbidsGapCloser
 *   Cast of a kept spell       TeamPlan::HoldsSpell (a flee stop's spell, and a kept
 *                              finisher's other finishers), except FleeStopBehavior's own
 *                              (the rotations: TeamBotAI::IsHeldFromRotation, with
 *                              InterruptKept below)
 *   Cast that deals damage     WakesControl below: the target, the area of an area spell,
 *   or splashes                the enemy a chained hit jumps to, the enemies next to the
 *                              victim of a melee splash buff must not be KeepsUnbroken,
 *                              unless the plan sends this bot to wake it (ControlEndPlan::Breaks).
 *                              The same rule gates every cast of the bot since 2026-11-08
 *                              (TeamBotAI::PassesPartyCastChecks), not only Commander proposals:
 *                              a rotation's Blade Flurry broke a Shackle (strat-20261108-141921).
 *   Cast with a cast or        TeamPlan::ForbidsLongCast (the area phase's reserved caster,
 *   channel time               AreaPhase.h "Reservation"), except its area spells: LongCastHeld
 *   Rest                       TeamPlan::ForbidsRest
 *   Attack, Chase, damage cast HandsOff below: a boss module's damage limit (stop line, keep
 *                              line, no periodic damage) and no-melee mark (PetAttack: the
 *                              stop line only)
 *   Cast time / periodic       WastedCast below: not at an enemy the others kill before it lands,
 *   damage spell               no periodic damage on one dying before it pays
 *                              (every cast of the bot, TeamBotAI::PassesPartyCastChecks)
 * (The threat opening's "everyone but the tank waits" is gone since 2026-11-13: each damage
 * dealer starts when it has threat room on its target, DamageTargets / ThreatRoom; doc
 * .kiro/docs/2026-11-13-tank-threat-priority.md 6.)
 *
 * Casts are not checked against EngageBlock: an interrupt or a control from where the bot
 * stands does not walk it into a patrol's path.
 */

#ifndef MANGOS_COMMANDER_CONSTRAINTS_H
#define MANGOS_COMMANDER_CONSTRAINTS_H

#include "Common.h"
#include "Commander/CommanderTypes.h"

class TeamBotAI;
class SpellEntry;
class Unit;

namespace Commander
{
    struct WorldModel;
    struct TeamPlan;
    struct Position3;

    namespace Constraints
    {
        // The constraint `p` breaks (a short static text for the candidate log), or null.
        char const* Broken(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                           Proposal const& p, uint32 nowMs);

        // The keep-unbroken rule for one cast of `spell` at `target` (the bot itself for a
        // spell around it), or null: the arbiter's check above and every other cast of the bot
        // (TeamBotAI::PassesPartyCastChecks: class rotations, the original loops). Enemies
        // TeamPlan::KeepsUnbroken (unless ControlEndPlan::Breaks sends this bot) must not be:
        //   the target of a spell that deals damage;
        //   within SpellTags::AreaRadius of the centre (the target) for an area spell;
        //   within SpellTags::CHAIN_JUMP_YD of the target for a chained hit (Cleave, Multi-Shot);
        //   within SpellTags::MeleeSplashRadius of the bot's victim for a melee splash buff
        //   (Blade Flurry, Sweeping Strikes; also checked while the buff is on:
        //   TeamBotAI::CancelSplashNearKeptControl).
        char const* WakesControl(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                 Unit const* target, SpellEntry const* spell, uint32 nowMs);

        // Every rule for one cast (the gate the arbiter and TeamBotAI::PassesPartyCastChecks
        // use): never a neutral (yellow) creature outside the fight, not as the target (one in
        // the party's fight, WorldModel::FindEnemy, is an enemy), not within an area
        // spell's radius, a chained hit's jump or a melee splash buff's reach (user 2026-11-10,
        // Tags::NeutralNear); no damage at an enemy the doctrine stamped HoldFire (its damage
        // shield / reflection, RoleStamps.h); then WakesControl.
        char const* CastForbidden(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                  Unit const* target, SpellEntry const* spell, uint32 nowMs);

        // TeamPlan::ForbidsLongCast: `spell` has a cast or channel time and is not one of the
        // bot's area spells (AreaDamageBehavior::Judges), and the area phase reserves the bot.
        // The arbiter's check above and every other cast of the bot (TeamBotAI::PassesPartyCastChecks).
        char const* LongCastHeld(TeamBotAI const* bot, TeamPlan const& plan, SpellEntry const* spell);

        // An interrupt the plan keeps for `bot` (user 2026-11-26): it is the primary or backup of
        // an interrupt duty (TeamPlan::interrupts) or covers a later cast (TeamPlan::reserves,
        // DutyReserve). `spell` is that interrupt or shares its cooldown category (the shaman's
        // shocks: category 19, 6 s), or null. For the class rotations (ClassRotation::Cast):
        // only InterruptBehavior spends it.
        char const* InterruptKept(TeamBotAI const* bot, TeamPlan const& plan, SpellEntry const* spell);

        // CastForbidden for a ground area spell cast at the point `centre` (BotAction::atPoint):
        // no neutral creature and no kept control within its radius of `centre`.
        char const* CastAtForbidden(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                    Position3 const& centre, SpellEntry const* spell, uint32 nowMs);

        // A fact, not a plan rule (user 2026-11-15). `spell` is a single target damage spell (not a
        // channel, not an auto repeat shot) at an enemy (Perception/Forecast/FinishForecast.h):
        //   cast time   the others kill it before it lands (cast + flight from now);
        //   periodic    (Corruption, Serpent Sting, Immolate's ticks) the ticks it gets in before
        //               the enemy dies (DamageSpellValue::DeathMs: the earlier of the damage
        //               sources on it and the party's shared rate, the bot's own hits counted
        //               after the spell's busy time), plus the direct part if it lands, are worth less
        //               than the bot's own damage over that busy time (PartyMemberFacts::soloDps).
        // Every cast of the bot (TeamBotAI::PassesPartyCastChecks): the rotation goes on to an
        // instant spell (Arcane Shot after the Aimed Shot) or waits.
        char const* WastedCast(TeamBotAI const* bot, WorldModel const& world, Unit const* target,
                               SpellEntry const* spell, uint32 nowMs);
        // Cheap test before the commander lookup: WastedCast may answer for `spell`.
        bool MayBeWasted(TeamBotAI const* bot, SpellEntry const* spell);

        // A hit's estimate (DamageSpells::Estimate, no crit) times this against a damage limit:
        // a crit, and the others' hits landing in the same moment.
        static constexpr float LIMIT_HIT_MARGIN = 2.0f;

        // The doctrine's hands-off marks for `bot` hitting `target` now (live health), or null:
        //   TeamPlan::DamageHeld  its damage limit's stop line is reached;
        //   TeamPlan::MeleeBlock  STAMP_NO_MELEE and the bot fights in melee (`spell` null: an
        //                         attack or a swing) or `spell` is a melee ability.
        // With a damage `spell` also (DamageLimit): no periodic damage on a limited unit, and
        // no hit that would take it below the limit's keep line. Used by Broken (Attack, Chase,
        // PetAttack), CastForbidden (every cast of the bot) and TeamBotAI (a victim it already
        // fights is dropped, its pet called back).
        char const* HandsOff(TeamBotAI const* bot, TeamPlan const& plan, Unit const* target, SpellEntry const* spell);
    }
}

#endif
