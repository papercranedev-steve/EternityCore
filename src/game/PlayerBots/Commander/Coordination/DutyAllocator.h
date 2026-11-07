/*
 * PartyBot Commander layer 2 (brain), second step: one allocation of team duties.
 * Design: .kiro/docs/2026-11-03-discussion/2026-11-03-architecture-refinement.md section 3.2.
 *
 * Duty modules (IAssigner with AssignStage::Duties) do not write the plan themselves. Each one
 * offers needs (Offer): "enemy X needs a <kind>, worth this many points, these bots could do it,
 * best first". The allocator answers all needs of all modules together, then every module
 * writes its own part of the plan from the results (Apply).
 *
 * Rule (user, 2026-11-03):
 *  - A bot holds at most one primary duty at a time, of any kind (interrupt, intercept, later
 *    crowd control, rescue). Primary duties exclude each other.
 *  - Backups do not claim: a bot already primary elsewhere may still be a backup.
 *  - Conflicts go by points: needs are answered in order (acting now first, then points, then
 *    tieMs, smaller first; stable), so the more valuable need claims the bot and the other need
 *    takes its next candidate.
 * Per need:
 *  primary = first candidate (rank, highest first; stable) that canBePrimary and is not primary
 *            for an earlier need;
 *  backup  = (wantsBackup only) first other candidate that is not primary anywhere; if there is
 *            none, the first other candidate even if it is primary elsewhere.
 * A need with neither is dropped. Candidates passed over for the primary are recorded with the
 * reason (DutyResult::passed, shown by `.partybot ai plan`).
 *
 * Pure function of its input (no game objects): replayable offline, the place where Jev would
 * choose among the same offers later.
 */

#ifndef MANGOS_COMMANDER_DUTY_ALLOCATOR_H
#define MANGOS_COMMANDER_DUTY_ALLOCATOR_H

#include "Common.h"
#include "ObjectGuid.h"
#include <vector>

namespace Commander
{
    enum class DutyKind : uint8
    {
        Interrupt,      // InterruptAssigner
        Intercept,      // FocusAssigner: kill a must-kill enemy without moving the focus
        Dispel,         // DispelAssigner: remove a debuff from a party member
    };

    struct DutyCandidate
    {
        ObjectGuid bot;
        uint32 spellId = 0;         // ability it would use, 0 = none in particular
        float rank = 0.0f;          // module's own order of preference, highest first
        bool canBePrimary = true;   // false: backup at most (e.g. cannot reach a live cast in time)
    };

    struct DutyNeed
    {
        DutyKind kind = DutyKind::Interrupt;
        ObjectGuid target;          // the enemy; for dispels the party member
        uint32 auraSpellId = 0;     // dispels: the debuff to remove (one need per member and debuff)
        bool now = false;           // the need is acting now (live cast), not a pre-assignment
        float points = 0.0f;        // what answering it is worth (ValueModel points)
        uint32 tieMs = 0;           // among equal points, smaller first (interrupt: cast time left)
        bool wantsBackup = false;
        uint8 detail = 0;           // module data passed through to its result (interrupt: CastCategory)
        char const* reason = "";    // static text, passed through (intercept: why it must die)
        std::vector<DutyCandidate> candidates;
    };

    enum class DutyPassReason : uint8
    {
        Busy,           // primary for an earlier (more valuable) need
        NotPrimary,     // candidate only fit to be backup
    };

    struct DutyPass
    {
        ObjectGuid bot;
        DutyPassReason reason = DutyPassReason::Busy;
        DutyKind busyKind = DutyKind::Interrupt;    // Busy: the duty that holds it
        ObjectGuid busyTarget;
    };

    struct DutyResult
    {
        DutyKind kind = DutyKind::Interrupt;
        ObjectGuid target;
        uint32 auraSpellId = 0;
        bool now = false;
        float points = 0.0f;
        uint8 detail = 0;
        char const* reason = "";
        ObjectGuid primary;
        uint32 primarySpellId = 0;
        ObjectGuid backup;
        uint32 backupSpellId = 0;
        std::vector<DutyPass> passed;   // better-ranked candidates not made primary, best first
    };

    namespace DutyAllocator
    {
        std::vector<DutyResult> Allocate(std::vector<DutyNeed> needs);

        char const* KindName(DutyKind kind);
    }
}

#endif
