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
 *   Cast that deals damage     WakesControl below: the target, the area of an area spell,
 *   or splashes                the enemy a chained hit jumps to, the enemies next to the
 *                              victim of a melee splash buff must not be KeepsUnbroken,
 *                              unless the plan sends this bot to wake it (ControlEndPlan::Breaks).
 *                              The same rule gates every cast of the bot since 2026-11-08
 *                              (PartyBotAI::PassesPartyCastChecks), not only Commander proposals:
 *                              a rotation's Blade Flurry broke a Shackle (strat-20261108-141921).
 *   Rest                       TeamPlan::ForbidsRest
 *   Attack, Chase, PetAttack   TeamPlan::threat.Holds: the threat opening, everyone but the
 *                              tank waits (Coordination/Doctrine/ThreatOpening.h)
 *   Cast                       while held: OpeningForbids below (no creature over the tank's
 *                              lead); every cast of the bot too (PartyBotAI::PassesPartyCastChecks)
 *
 * Casts are not checked against EngageBlock: an interrupt or a control from where the bot
 * stands does not walk it into a patrol's path.
 */

#ifndef MANGOS_COMMANDER_CONSTRAINTS_H
#define MANGOS_COMMANDER_CONSTRAINTS_H

#include "Common.h"
#include "Commander/CommanderTypes.h"

class PartyBotAI;
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
        char const* Broken(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                           Proposal const& p, uint32 nowMs);

        // The keep-unbroken rule for one cast of `spell` at `target` (the bot itself for a
        // spell around it), or null: the arbiter's check above and every other cast of the bot
        // (PartyBotAI::PassesPartyCastChecks: class rotations, the original loops). Enemies
        // TeamPlan::KeepsUnbroken (unless ControlEndPlan::Breaks sends this bot) must not be:
        //   the target of a spell that deals damage;
        //   within SpellTags::AreaRadius of the centre (the target) for an area spell;
        //   within SpellTags::CHAIN_JUMP_YD of the target for a chained hit (Cleave, Multi-Shot);
        //   within SpellTags::MeleeSplashRadius of the bot's victim for a melee splash buff
        //   (Blade Flurry, Sweeping Strikes; also checked while the buff is on:
        //   PartyBotAI::CancelSplashNearKeptControl).
        char const* WakesControl(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                 Unit const* target, SpellEntry const* spell, uint32 nowMs);

        // Every rule for one cast (the gate the arbiter and PartyBotAI::PassesPartyCastChecks
        // use): never a neutral (yellow) creature, not as the target, not within an area
        // spell's radius, a chained hit's jump or a melee splash buff's reach (user 2026-11-10,
        // Tags::NeutralNear); no damage at an enemy the doctrine stamped HoldFire (its damage
        // shield / reflection, RoleStamps.h); then WakesControl.
        char const* CastForbidden(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                  Unit const* target, SpellEntry const* spell, uint32 nowMs);

        // CastForbidden for a ground area spell cast at the point `centre` (BotAction::atPoint):
        // no neutral creature and no kept control within its radius of `centre`.
        char const* CastAtForbidden(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                    Position3 const& centre, SpellEntry const* spell, uint32 nowMs);

        // The threat opening's rule for one cast (user 2026-11-12; before: no cast at all, user
        // 2026-11-08, strat-20261108-200046): a bot the opening holds (TeamPlan::threat.Holds)
        // may cast what puts no creature over the tank's lead: every creature's headroom for the
        // bot (ThreatView::Headroom) after ThreatView::PredictCastThreat stays above 0, and a
        // harmful cast reaches no creature not in combat yet. Ice Barrier before anything fights
        // the mage, buffs, a heal small enough pass; a Counterspell at an enemy the tank has no
        // threat on does not. Walking was never held. Null: allowed.
        char const* OpeningForbids(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                   Unit* target, SpellEntry const* spell);
    }
}

#endif
