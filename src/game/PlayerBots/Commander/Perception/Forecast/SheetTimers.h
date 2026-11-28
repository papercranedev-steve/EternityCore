/*
 * PartyBot Commander layer 1: estimated timers of a C++ boss script's abilities (dungeon boss doc
 * .kiro/docs/2026-11-28-dungeon-boss-encounters.md, gap G1). A C++ script keeps its timers in
 * its own members, so EnemyFacts::spellList is empty for it; this fills spellList from its
 * ability sheet (Profiles/BossSheets.h, profile spells of source Sheet) with an estimate of
 * each row's cooldown, and sets EnemyFacts::spellListEstimated. IncomingTimeline and
 * ThreatWipe then read the rows like creature_spells timers.
 *
 * Per row, at the refresh (after ThreatHistory::Annotate: EnemyFacts::firstSeenMs):
 *   - health above its belowPct line: left out (the script does not cast it yet);
 *   - seen cast (EnemyFacts::castStarts, spells with a cast time only): once a fight -> left
 *     out; else the shortest repeat delay minus the time since that cast;
 *   - not seen, the fight younger than the first delay: the shortest first delay minus the
 *     fight's age (from the enemy's first snapshot);
 *   - not seen, older: once a fight -> 0 (due); else half the average repeat delay (the mean
 *     wait at an unknown point of the cycle: instants are never seen cast).
 * Probability 100, the sheet's castTarget, spellCheckInMs 0.
 *
 * Known limits: instant casts are never seen, so their cooldown never runs down to the real
 * one; a phase change that restarts the script's timers (Balnazzar) is not seen (counted from
 * the fight's start); belowPct is the only gate (no "only above").
 */

#ifndef MANGOS_COMMANDER_SHEET_TIMERS_H
#define MANGOS_COMMANDER_SHEET_TIMERS_H

#include "Common.h"

namespace Commander
{
    struct WorldModel;

    namespace SheetTimers
    {
        // Every enemy with no live spell list and an ability sheet.
        void Annotate(WorldModel& world, uint32 nowMs);
    }
}

#endif
