/*
 * PartyBot Commander: debug speech. Bots say what they decided, in party chat, so the developer
 * sees it in game (user 2026-11-09). `.partybot ai debugsay on|off` (PartyBot_Dev's diagnostics
 * window), config PartyBot.Commander.DebugSpeech (default off since 2026-11-22: it is data and
 * frequent, not for players; what players hear is Commands/Voice/PartyVoice.h).
 *
 * What is said:
 *  - decisions: an action the arbiter executed (Arbiter::Run) above the Routine tier. Casts and
 *    attacks of every module; moves and holds only for the modules in SOURCES marked `moves`
 *    (stance modules walk every tick). Example: "打断：Kick → Scarlet Sorcerer（primary）".
 *  - the brain's notes (TeamPlan::notes, PartyCommander::FlushNotes), said by the main tank bot,
 *    and a healer's low mana warning (ManaBudgetBehavior), said by the healer: Report().
 *  - review anomalies of a recorded fight (DevConsole review.lua, u:partysay): Say().
 * Off: nothing is said; the notes stay in the party's note log (PartyCommander::GetNoteLog,
 * `.partybot ai plan`, the combat recorder).
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
class TeamBotAI;

namespace Commander
{
    struct Proposal;

    class BotSpeech
    {
    public:
        static constexpr uint32 MIN_GAP_MS = 2000;
        static constexpr uint32 REPEAT_MS = 15000;

        // Arbiter::Run, after `p` executed (map thread of the bot).
        static void OnExecuted(TeamBotAI const* bot, Proposal const& p, uint32 nowMs);

        // A note or warning (debug data): said in party chat while debug speech is on.
        static void Report(Player* speaker, std::string const& text);

        // Debug speech on: `speaker` says `text` in party chat. False when off or not in a group.
        static bool Say(Player* speaker, std::string const& text);
    };
}

#endif
