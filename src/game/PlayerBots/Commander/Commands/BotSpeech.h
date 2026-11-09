/*
 * PartyBot Commander: bots say what they decided, in party chat, so the player sees it in game
 * (user 2026-11-09). `.partybot ai say on|off`, config PartyBot.Commander.Speech (default on).
 *
 * What is said:
 *  - decisions: an action the arbiter executed (Arbiter::Run) above the Routine tier. Casts and
 *    attacks of every module; moves and holds only for the modules in SOURCES marked `moves`
 *    (stance modules walk every tick). Example: "打断：Kick → Scarlet Sorcerer（primary）".
 *  - the brain's notes (TeamPlan::notes, PartyCommander::FlushNotes), said by the main tank bot,
 *    and a healer's low mana warning (ManaBudgetBehavior), said by the healer: Report().
 *  - review anomalies of a recorded fight (DevConsole review.lua, u:partysay): Say().
 * Off: notes and warnings go to the leader as "[PartyBot] ..." system messages as before;
 * decisions and anomalies are not said.
 *
 * Limits against spam, per bot: one line per MIN_GAP_MS; the same module + spell (or module +
 * enemy for attacks, module + action for moves) once per REPEAT_MS. Lines over the limit are
 * dropped, not queued (late lines would mislead).
 *
 * Reporting only: nothing here changes what a bot does.
 */

#ifndef MANGOS_COMMANDER_BOT_SPEECH_H
#define MANGOS_COMMANDER_BOT_SPEECH_H

#include "Common.h"
#include <string>

class Player;
class PartyBotAI;

namespace Commander
{
    struct Proposal;

    class BotSpeech
    {
    public:
        static constexpr uint32 MIN_GAP_MS = 2000;
        static constexpr uint32 REPEAT_MS = 15000;

        // Arbiter::Run, after `p` executed (map thread of the bot).
        static void OnExecuted(PartyBotAI const* bot, Proposal const& p, uint32 nowMs);

        // Speech on: `speaker` says `text` in party chat. Off (or no group): `leader` gets
        // "[PartyBot] text" as a system message when on the speaker's map.
        static void Report(Player* speaker, Player* leader, std::string const& text);

        // Speech on: `speaker` says `text` in party chat. False when off or not in a group.
        static bool Say(Player* speaker, std::string const& text);
    };
}

#endif
