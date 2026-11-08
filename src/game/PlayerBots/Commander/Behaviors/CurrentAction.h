/*
 * PartyBot Commander layer 3 (cerebellum): what the bot is doing now is worth something.
 * Design: .kiro/docs/2026-11-08-structural-fixes.md section 3.
 *
 * The arbiter compares this tick's proposals; the cast, channel or walk already running was
 * not one of them, so ending it cost nothing (strat-20261108-141921: three Blizzards ended by
 * a second Blizzard, an Ice Barrier and an area step, ~3500 mana for few ticks; Evocation cast
 * while a walk went on broke at once). Now:
 *
 *   keep      points lost if the running cast or channel stops now, the first that answers:
 *             1. a module that values its own spell (IBehavior::ContinueValue: AreaDamageBehavior
 *                for its channel, the same tick plan as its channel watch);
 *             2. the arbiter's order it was cast for (BotDirectives::GetLastExecuted, same
 *                spell): the order's net, for a channel the share still to come (time left /
 *                duration);
 *             3. otherwise (a rotation cast): a cast loses the time spent on it, a channel one
 *                global cooldown (the old CastBreak rule).
 *   walkKeep  a module's walk under way (movement hold with an owner, the bot moving): the net
 *             of the MoveTo order that started it (BotDirectives::GetLastWalk), 0 without one.
 *
 * InterruptCost(action): what executing `action` adds to its cost by ending them:
 *   keep      any Cast (a new cast replaces the current one: SpellCaster::SetCurrentCastedSpell),
 *             Rest (sitting down), and a walk (MoveTo, Backpedal, Chase, Retreat, UseObject) when
 *             moving ends the current spell (channel: ChannelInterruptFlags moving; cast:
 *             InterruptFlags movement).
 *   walkKeep  a Cast that needs standing still (NeedsStandingStill): the executor stops the walk
 *             for it (PartyBotAI::StandStillFor).
 * Arbiter::Run adds it to every valued replace proposal (unvalued ones are "must" orders: the
 * tier decides), CastBreak uses `keep` as the cost of breaking the cast.
 *
 * Map thread (live spells of the bot).
 */

#ifndef MANGOS_COMMANDER_CURRENT_ACTION_H
#define MANGOS_COMMANDER_CURRENT_ACTION_H

#include "Common.h"
#include "Commander/CommanderTypes.h"

class PartyBotAI;
class SpellEntry;

namespace Commander
{
    struct WorldModel;
    struct TeamPlan;

    namespace CurrentAction
    {
        struct State
        {
            SpellEntry const* spell = nullptr;  // cast (preparing) or channel running, null = none
            bool channel = false;
            uint32 leftMs = 0;
            bool movementEnds = false;          // moving ends it
            float keep = 0.0f;
            char const* keepFrom = "";          // module id, "order" or "rotation"
            char const* keepReason = "";
            char const* walkOwner = nullptr;    // module whose walk runs, null = none
            float walkKeep = 0.0f;

            bool Casting() const { return spell != nullptr; }
        };

        State Read(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan);

        // Points `action` costs on top of its own by ending what runs (header). `what`: what it
        // ends, for the decision log ("" = nothing).
        float InterruptCost(PartyBotAI const* bot, State const& state, BotAction const& action, char const*& what);

        // `spell` must be cast standing: it has a cast time, or it is a channel that moving ends.
        bool NeedsStandingStill(PartyBotAI const* bot, SpellEntry const* spell);
    }
}

#endif
