/*
 * PartyBot Commander: per-bot state that official PartyBot code is allowed to query.
 * Every check added to official code must go through this class and be listed in
 * .kiro/docs/2026-11-01-ai-teammate-code-structure.md section 6.2.
 */

#ifndef MANGOS_COMMANDER_BOT_DIRECTIVES_H
#define MANGOS_COMMANDER_BOT_DIRECTIVES_H

#include "Common.h"
#include "Timer.h"
#include "Commander/CommanderTypes.h"
#include "ObjectGuid.h"
#include <algorithm>
#include <array>
#include <deque>
#include <string>
#include <vector>

namespace Commander
{
    // One proposal as the arbiter saw it (value-scoring doc 6.2).
    struct CandidateRecord
    {
        enum class Result : uint8
        {
            NotTried,   // a better proposal in the same tier executed first
            Executed,
            Failed,     // tried, could not execute (cooldown, range, target gone)
            LowerTier,  // a higher tier had proposals
            Dropped,    // valued and benefit <= cost
            Forbidden,  // breaks a plan constraint (Behaviors/Constraints.h; reason names it)
            VetoSet,    // veto registered
            OrderSet,   // heal order handed to the rotation's heal step
            // The bot's running cast, channel or walk (Behaviors/CurrentAction.h), shown as a
            // candidate: it goes on, or the executed proposal ended it (and paid for that).
            Kept,
            Ended,
        };

        std::string source;
        std::string what;           // action or veto, with target and spell
        ProposalKind kind = ProposalKind::Replace;
        uint8 tier = 0;
        float score = 0.0f;
        bool valued = false;
        float benefit = 0.0f;
        float cost = 0.0f;
        std::string reason;
        Result result = Result::NotTried;
    };

    struct DecisionRecord
    {
        uint32 timeMs = 0;
        std::vector<CandidateRecord> candidates;
    };

    // The last bot action the arbiter executed (not pet actions). Lets a module see that its
    // own order went out, e.g. PullBehavior: the tank's opener was cast.
    struct ExecutedAction
    {
        char const* source = "";       // Proposal::source (a string literal)
        BotActionType type = BotActionType::Hold;
        SpellEntry const* spell = nullptr;
        uint32 timeMs = 0;              // 0 = nothing executed yet
        // The proposal's value when it was executed (Proposal::score, its own part: what
        // ending it early loses, Behaviors/CurrentAction.h). valued false = an order without one.
        bool valued = false;
        float net = 0.0f;
    };

    // A cast proposal the arbiter tried and could not execute (out of range, no line of sight,
    // not enough power, wrong form), per (target, spell), until the spell executes or the
    // failures stop for CAST_FAILURE_MEMORY_MS. The eyes read it (PartyMemberFacts::castFailures):
    // a duty given to a bot that cannot carry it out goes to someone else.
    struct CastFailure
    {
        ObjectGuid target;
        uint32 spellId = 0;
        uint32 firstMs = 0;         // first failure of this run
        uint32 lastMs = 0;
        uint32 count = 0;
    };

    class BotDirectives
    {
    public:
        // 40: the combat recorder polls every 200 ms (recorder.lua rec.interval); leaves room for
        // slow polls (a reload, a long console call) at a bot tick of 100-200 ms
        static size_t const LOG_SIZE = 40;
        // A run of failures is forgotten when the bot has not tried for this long.
        static uint32 const CAST_FAILURE_MEMORY_MS = 5000;

        // While held, TeamBotAI does not issue MoveFollow / MoveChase / run-away moves.
        // `owner` = the module whose MoveTo started it (Proposal::source, a string literal), so
        // that module can tell its own walk from another's (EscapeBehavior stops its run when
        // nobody chases any more). Null = not from a module proposal.
        void HoldMovement(uint32 nowMs, uint32 durationMs, char const* owner = nullptr)
        {
            m_holdStartMs = nowMs;
            m_holdDurationMs = durationMs;
            m_holdOwner = owner;
        }
        void ReleaseMovement() { m_holdDurationMs = 0; m_holdOwner = nullptr; }
        // Module that started the movement hold in force now; null when not held or not known.
        char const* GetMovementOwner(uint32 nowMs) const
        {
            return IsMovementHeld(nowMs) ? m_holdOwner : nullptr;
        }
        bool IsMovementHeld(uint32 nowMs) const
        {
            return m_holdDurationMs && WorldTimer::getMSTimeDiff(m_holdStartMs, nowMs) < m_holdDurationMs;
        }
        uint32 GetMovementHoldRemaining(uint32 nowMs) const
        {
            if (!IsMovementHeld(nowMs))
                return 0;
            return m_holdDurationMs - WorldTimer::getMSTimeDiff(m_holdStartMs, nowMs);
        }

        // A decision a Commander module has taken over from the class rotation (Takeover,
        // CommanderTypes.h). The arbiter renews it every tick the module applies; it expires by
        // itself, so a disabled module or a Commander switched off gives it back.
        void TakeOver(Takeover what, uint32 nowMs, uint32 durationMs)
        {
            TakeoverSlot& s = m_takeovers[size_t(what)];
            s.startMs = nowMs;
            s.durationMs = durationMs;
        }
        bool IsTakenOver(Takeover what, uint32 nowMs) const
        {
            TakeoverSlot const& s = m_takeovers[size_t(what)];
            return s.durationMs && WorldTimer::getMSTimeDiff(s.startMs, nowMs) < s.durationMs;
        }
        // urgent: the order saves someone about to die (tier Urgent or above).
        void SetHealOrder(BotAction const& action, bool urgent, uint32 nowMs, uint32 durationMs)
        {
            m_healOrder = action;
            m_healOrderUrgent = urgent;
            m_healOrderStartMs = nowMs;
            m_healOrderDurationMs = durationMs;
        }
        void ClearHealOrder() { m_healOrderDurationMs = 0; }
        // Null when there is no order for this tick.
        BotAction const* GetHealOrder(uint32 nowMs) const
        {
            if (!m_healOrderDurationMs || WorldTimer::getMSTimeDiff(m_healOrderStartMs, nowMs) >= m_healOrderDurationMs)
                return nullptr;
            return &m_healOrder;
        }
        // An urgent heal order waits for the heal step this tick: the rotation's dispels before
        // it stand down, except for control effects (VetoChecks::IsDispelVetoed).
        bool HasUrgentHealOrder(uint32 nowMs) const
        {
            return m_healOrderUrgent && GetHealOrder(nowMs);
        }
        // The last heal the rotation cast from an order: a chain of heals on one target goes on
        // while the next one starts within `gapMs` after the previous one ended.
        void NoteOrderedHeal(ObjectGuid target, uint32 nowMs, uint32 busyMs)
        {
            m_lastHealTarget = target;
            m_lastHealMs = nowMs;
            m_lastHealBusyMs = busyMs;
        }
        bool IsHealChain(ObjectGuid target, uint32 nowMs, uint32 gapMs) const
        {
            return !m_lastHealTarget.IsEmpty() && m_lastHealTarget == target &&
                   WorldTimer::getMSTimeDiff(m_lastHealMs, nowMs) <= m_lastHealBusyMs + gapMs;
        }

        // Vetoes expire by themselves, so a disabled module cannot leave the bot stuck.
        void AddVeto(Veto const& veto, uint32 nowMs, uint32 durationMs)
        {
            PruneVetoes(nowMs);
            for (ActiveVeto& a : m_vetoes)
            {
                if (a.veto.type == veto.type && a.veto.target == veto.target && a.veto.spellId == veto.spellId)
                {
                    a.startMs = nowMs;
                    a.durationMs = durationMs;
                    return;
                }
            }
            m_vetoes.push_back({ veto, nowMs, durationMs });
        }
        bool IsVetoed(VetoType type, ObjectGuid target, uint32 spellId, uint32 nowMs) const
        {
            for (ActiveVeto const& a : m_vetoes)
            {
                if (a.veto.type != type || WorldTimer::getMSTimeDiff(a.startMs, nowMs) >= a.durationMs)
                    continue;
                if (!a.veto.target.IsEmpty() && a.veto.target != target)
                    continue;
                if (a.veto.spellId && a.veto.spellId != spellId)
                    continue;
                return true;
            }
            return false;
        }
        bool HasVetoes(uint32 nowMs) const
        {
            for (ActiveVeto const& a : m_vetoes)
                if (WorldTimer::getMSTimeDiff(a.startMs, nowMs) < a.durationMs)
                    return true;
            return false;
        }

        // Debug info for .partybot ai bot
        void SetLastDecision(uint32 nowMs, std::string text)
        {
            m_lastDecisionMs = nowMs;
            m_lastDecision = std::move(text);
        }
        std::string const& GetLastDecision() const { return m_lastDecision; }
        uint32 GetLastDecisionMs() const { return m_lastDecisionMs; }

        // `ownNet`: the proposal's net without what it paid for ending the bot's previous action
        // (Proposal::interruptCost): the worth of this action itself.
        void NoteExecuted(Proposal const& p, uint32 nowMs)
        {
            ExecutedAction a;
            a.source = p.source;
            a.type = p.action.type;
            a.spell = p.action.spell;
            a.timeMs = nowMs;
            a.valued = p.valued;
            a.net = p.valued ? p.benefit - (p.cost - p.interruptCost) : 0.0f;
            m_lastExecuted = a;
            if (a.type == BotActionType::MoveTo)
                m_lastWalk = a;
        }
        ExecutedAction const& GetLastExecuted() const { return m_lastExecuted; }

        // Arbiter: a Cast proposal failed / executed (CastFailure).
        void NoteCastFailed(ObjectGuid target, uint32 spellId, uint32 nowMs)
        {
            PruneCastFailures(nowMs);
            for (CastFailure& f : m_castFailures)
            {
                if (f.target == target && f.spellId == spellId)
                {
                    f.lastMs = nowMs;
                    ++f.count;
                    return;
                }
            }
            CastFailure f;
            f.target = target;
            f.spellId = spellId;
            f.firstMs = nowMs;
            f.lastMs = nowMs;
            f.count = 1;
            m_castFailures.push_back(f);
        }
        void NoteCastExecuted(uint32 spellId)
        {
            m_castFailures.erase(std::remove_if(m_castFailures.begin(), m_castFailures.end(),
                [spellId](CastFailure const& f) { return f.spellId == spellId; }), m_castFailures.end());
        }
        // Runs still remembered at `nowMs`.
        std::vector<CastFailure> GetCastFailures(uint32 nowMs) const
        {
            std::vector<CastFailure> out;
            for (CastFailure const& f : m_castFailures)
                if (WorldTimer::getMSTimeDiff(f.lastMs, nowMs) < CAST_FAILURE_MEMORY_MS)
                    out.push_back(f);
            return out;
        }
        // The last MoveTo the arbiter executed (the walk a movement hold keeps going).
        ExecutedAction const& GetLastWalk() const { return m_lastWalk; }

        void RecordDecision(DecisionRecord record)
        {
            m_log.push_back(std::move(record));
            while (m_log.size() > LOG_SIZE)
                m_log.pop_front();
        }
        std::deque<DecisionRecord> const& GetDecisionLog() const { return m_log; }

    private:
        struct ActiveVeto
        {
            Veto veto;
            uint32 startMs;
            uint32 durationMs;
        };

        void PruneVetoes(uint32 nowMs)
        {
            for (auto itr = m_vetoes.begin(); itr != m_vetoes.end();)
            {
                if (WorldTimer::getMSTimeDiff(itr->startMs, nowMs) >= itr->durationMs)
                    itr = m_vetoes.erase(itr);
                else
                    ++itr;
            }
        }

        void PruneCastFailures(uint32 nowMs)
        {
            m_castFailures.erase(std::remove_if(m_castFailures.begin(), m_castFailures.end(),
                [nowMs](CastFailure const& f) { return WorldTimer::getMSTimeDiff(f.lastMs, nowMs) >= CAST_FAILURE_MEMORY_MS; }),
                m_castFailures.end());
        }

        uint32 m_holdStartMs = 0;
        uint32 m_holdDurationMs = 0;
        char const* m_holdOwner = nullptr;
        struct TakeoverSlot
        {
            uint32 startMs = 0;
            uint32 durationMs = 0;
        };
        std::array<TakeoverSlot, size_t(Takeover::Count)> m_takeovers{};
        BotAction m_healOrder;
        bool m_healOrderUrgent = false;
        uint32 m_healOrderStartMs = 0;
        uint32 m_healOrderDurationMs = 0;
        ObjectGuid m_lastHealTarget;
        uint32 m_lastHealMs = 0;
        uint32 m_lastHealBusyMs = 0;
        uint32 m_lastDecisionMs = 0;
        std::string m_lastDecision;
        ExecutedAction m_lastExecuted;
        ExecutedAction m_lastWalk;
        std::vector<ActiveVeto> m_vetoes;
        std::vector<CastFailure> m_castFailures;
        std::deque<DecisionRecord> m_log;
    };
}

#endif
