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
 *  - Conflicts go by points over the whole allocation (2026-11-17, was greedy need by need):
 *    the primaries are the set worth the most in total, needs acting now first (their total
 *    decides, the others' only among equal totals of those). Each answer is worth the
 *    candidate's `value` (DutyCandidate). Example: the rogue alone reaches the fear caster,
 *    rogue and mage reach the healer: the mage kicks the heal, the rogue the fear, although
 *    the heal is worth more and the rogue ranks first on it.
 *  - Search: needs in order (acting now first, then points, then tieMs, smaller first;
 *    stable), each one's candidates by rank (highest first; stable), then "left unanswered";
 *    branch and bound. The first answer found is the old greedy one (each need takes its
 *    best-ranked free candidate). Another answer replaces it only when it is worth at least
 *    SWITCH_MARGIN more (no flapping between near-equal allocations from refresh to refresh);
 *    the search stops after MAX_NODES steps with the best found so far.
 *  - Team cooldown budget (2026-11-17): the reserves (DutyReserve, later casts) take what the
 *    allocation's spends lose off the total of the needs acting now. So the team keeps one
 *    kick for the heal coming in 3 s instead of spending both on a small cast now, but spends
 *    one freely when another bot still covers the heal.
 * Per need:
 *  primary = the search's candidate (canBePrimary, primary for nothing else);
 *  declined = no primary although a free candidate was fit (LessWorth): the allocation is
 *            worth more without it (a reserve). Dropped with its backup: a backup would spend
 *            the kick the allocation keeps.
 *  backup  = (wantsBackup only) first other candidate that is not primary anywhere; if there is
 *            none, the first other candidate even if it is primary elsewhere.
 * A need with neither is dropped. Candidates ranked above the primary are recorded with the
 * reason it is not them (DutyResult::passed, shown by `.partybot ai plan`).
 * Alternatives (DutyNeed::blockedBy / marks, 2026-11-07 team rescue): a need blocked by a key an
 * earlier answered need marked is skipped (a control and a taunt on the same loose enemy, or a
 * taunt and Shield Wall for the same member, would both spend a bot on one problem).
 *
 * Pure function of its input (no game objects): replayable offline, the place where Jev would
 * choose among the same offers later.
 */

#ifndef MANGOS_COMMANDER_DUTY_ALLOCATOR_H
#define MANGOS_COMMANDER_DUTY_ALLOCATOR_H

#include "Common.h"
#include "ObjectGuid.h"
#include <string>
#include <vector>

namespace Commander
{
    enum class DutyKind : uint8
    {
        Interrupt,      // InterruptAssigner
        Intercept,      // FocusAssigner: kill a must-kill enemy without moving the focus
        Dispel,         // DispelAssigner: remove a debuff from a party member
        CrowdControl,   // CrowdControlAssigner: take an enemy out of the fight for a while
        Rescue,         // RescueAssigner: get an enemy off a member who is not a tank
        Save,           // RescueAssigner: keep a member who is about to die alive (Shield Wall, a shield)
        FleeStop,       // FleeStopAssigner: stun / root / slow an enemy running away (a reservation until it is due)
        Boost,          // BoostAssigner: a cooldown on a party member (Innervate, Power Infusion, Mana Tide)
        Resurrect,      // ResurrectAssigner: a dead healer or tank back in the fight (Rebirth)
    };

    // Needs of different kinds that solve the same problem (team rescue, 2026-11-07): a need is
    // skipped when one of its `blockedBy` keys was marked by an earlier answered need (the
    // order above: the most valuable first); an answered need marks its `marks` keys.
    enum class DutyKeyKind : uint8
    {
        // The enemy is dealt with: taken off its victim (rescue), controlled (single target
        // control) or killed by one bot (intercept of a summon on a non-tank). Key = enemy.
        EnemyHandled,
        // A loose enemy was taken off this member (rescue). Key = the member: no cooldown is
        // burnt to save it in the same refresh.
        LooseTaken,
        // A cooldown or shield keeps this member alive (save). Key = the member: no loose enemy
        // on it is taunted or faded in the same refresh.
        Saved,
        // The enemy's cast running now is stopped: an interrupter is on it (interrupt), or one
        // bot kills it before the cast ends (intercept "kill before its cast ends"). Key = enemy.
        CastStopped,
    };

    struct DutyKey
    {
        DutyKeyKind kind = DutyKeyKind::EnemyHandled;
        ObjectGuid guid;
        bool operator==(DutyKey const& o) const { return kind == o.kind && guid == o.guid; }
    };
    struct DutyCandidate
    {
        ObjectGuid bot;
        uint32 spellId = 0;         // ability it would use, 0 = none in particular
        // The spell's shared cooldown (SpellEntry::Category with a CategoryRecoveryTime, 0 =
        // its own only), filled by PartyCommander before Allocate: spending Frost Shock spends
        // the Earth Shock a reserve counts on (DutyReserve).
        uint32 cooldownCategory = 0;
        float rank = 0.0f;          // module's own order of preference, highest first
        // Points answering the need with this candidate is worth (its net, the module's sticky
        // bonus included): what the allocator compares across needs. Not `valued`: the
        // need's points (every candidate alike, rank alone tells them apart).
        float value = 0.0f;
        bool valued = false;
        bool canBePrimary = true;   // false: backup at most (e.g. cannot reach a live cast in time)
        // For the log only (TeamPlan::offers, the recorder's `offers` lines): how the module
        // valued it (e.g. ControlValue::Describe). Empty = not given.
        std::string why;
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
        std::vector<DutyKey> blockedBy;
        std::vector<DutyKey> marks;
        std::vector<DutyCandidate> candidates;
        // For the log only: what the module judged and left out (net <= 0), rank = that net.
        // The allocator ignores them. Without these a need nobody was offered for ("why was the
        // Patchwork Horror not shackled for 50 s", strat-20261108-141921) left no trace.
        std::vector<DutyCandidate> rejected;
    };

    // A need expected later (team cooldown budget, 2026-11-17): an enemy cast the listed
    // abilities could stop. Answering a need acting now with one of them (that bot, that spell
    // or one of its shared cooldown, Cover::cooldownCategory: a flee stop's Frost Shock spends
    // the Earth Shock) spends it for its cooldown. An allocation that spends every cover loses
    // (points - the least valuable need a cover was spent on) x the smallest cover share (the
    // chance the cast comes before the first of them is back): keeping that kick would have
    // traded the cheaper cast for this one; a cast worth less than what was kicked loses nothing
    // (one kick per cooldown either way). Per `group` (the enemy: it casts one thing at a time)
    // only the largest loss counts.
    // Spending some but not all costs nothing: the others cover it. Bots only (a player may
    // not kick). Offered by IAssigner::OfferReserves.
    struct DutyReserve
    {
        struct Cover
        {
            ObjectGuid bot;
            uint32 spellId = 0;
            uint32 cooldownCategory = 0;    // as DutyCandidate::cooldownCategory
            float share = 0.0f;         // chance the cast comes within its cooldown if spent now
            float shareOnGroup = 0.0f;  // the same when spent on `group` itself (its school lockout)
        };
        ObjectGuid group;
        uint32 spellId = 0;             // the cast expected (log)
        float points = 0.0f;
        std::vector<Cover> covers;
    };

    enum class DutyPassReason : uint8
    {
        Busy,           // primary for another need (the allocation is worth more that way)
        NotPrimary,     // candidate only fit to be backup
        LessWorth,      // free, but another candidate (or none) makes the allocation worth more
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
        // Points the best allocation must beat the greedy one by (about a module's sticky bonus).
        static constexpr float SWITCH_MARGIN = 0.5f;
        // Search steps before it settles for the best allocation found (5 bots: rarely reached).
        static constexpr uint32 MAX_NODES = 20000;

        std::vector<DutyResult> Allocate(std::vector<DutyNeed> needs, std::vector<DutyReserve> const& reserves = {});

        // Points `reserves` lose to the `now` primaries (bot, spell, target) of an allocation (DutyReserve).
        struct Spend
        {
            ObjectGuid bot;
            uint32 spellId = 0;
            ObjectGuid target;
            float points = 0.0f;    // the need's points
            uint32 cooldownCategory = 0;    // a cover of the same bot and category is spent too
        };
        float ReserveLoss(std::vector<DutyReserve> const& reserves, std::vector<Spend> const& spent);

        char const* KindName(DutyKind kind);
        // Why `pass` is not the primary, for `.partybot ai plan` (busyName: the busy target's name).
        std::string DescribePass(DutyPass const& pass, std::string const& busyName);
    }
}

#endif
