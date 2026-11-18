/*
 * PartyBot Commander layer 3: stop the bot's own cast when something cannot wait for it.
 *
 * The arbiter does not run while a bot casts or channels (PartyBotAI::UpdateAI returns
 * before it), so a damage dealer three seconds into a Frostbolt could not take its interrupt
 * duty or step out of an area until the cast ended. PartyBotAI::UpdateAI calls Update every
 * frame, before its bot update throttle; the check itself runs at most every
 * CHECK_INTERVAL_MS while a cast (generic spell, preparing) or a channel is in progress.
 *
 * Each enabled behavior that applies to the bot answers IBehavior::CastBreakBenefit: points it
 * loses when the bot finishes the cast first. The best answer is compared with what stopping
 * costs: the running cast's own worth (CurrentAction::State::keep, since 2026-11-08): the
 * module's live value of its spell (IBehavior::ContinueValue), the net of the order it was
 * cast for, or for a rotation cast the time spent on it / one GCD for a channel (the rule
 * before, which charged a Blizzard just started as one GCD).
 * Better than the cost: the cast is interrupted and the bot update runs in the same frame
 * (the caller resets the bot's update timer), where the arbiter's Propose acts on it.
 *
 * At most one stop every COOLDOWN_MS: when the follow-up fails (no spot to step to, the
 * interrupt out of range) the rotation casts again and is not stopped in a loop.
 * Commander on, in combat. Each stop is written to the bot's decision log
 * (source = the behavior's id, what = "cast-break spellN").
 *
 * The healer heal checks (CastWatch.h) and the area channel check (AreaDamageBehavior
 * WatchChannel) are separate and run before this one.
 *
 * Map thread only (reads live units and the commander of the bot's own map).
 */

#ifndef MANGOS_COMMANDER_CAST_BREAK_H
#define MANGOS_COMMANDER_CAST_BREAK_H

#include "Common.h"

class TeamBotAI;

namespace Commander
{
    // Per bot (TeamBotAI member). Plain data.
    struct CastBreakState
    {
        uint32 lastCheckMs = 0;
        uint32 lastBreakMs = 0;     // 0 = never
    };

    namespace CastBreak
    {
        constexpr uint32 CHECK_INTERVAL_MS = 200;
        constexpr uint32 COOLDOWN_MS = 2000;

        // Every frame from PartyBotAI::UpdateAI. True = the cast was stopped.
        bool Update(TeamBotAI* bot, CastBreakState& state);
    }
}

#endif
