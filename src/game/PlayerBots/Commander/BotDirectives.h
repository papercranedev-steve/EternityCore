/*
 * PartyBot Commander: per-bot state that official PartyBot code is allowed to query.
 * Every check added to official code must go through this class and be listed in
 * .kiro/docs/2026-11-01-ai-teammate-code-structure.md section 6.2.
 */

#ifndef MANGOS_COMMANDER_BOT_DIRECTIVES_H
#define MANGOS_COMMANDER_BOT_DIRECTIVES_H

#include "Common.h"
#include "Timer.h"
#include <string>

namespace Commander
{
    class BotDirectives
    {
    public:
        // While held, PartyBotAI does not issue MoveFollow / MoveChase / run-away moves.
        void HoldMovement(uint32 nowMs, uint32 durationMs)
        {
            m_holdStartMs = nowMs;
            m_holdDurationMs = durationMs;
        }
        void ReleaseMovement() { m_holdDurationMs = 0; }
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

        // Debug info for .partybot ai bot
        void SetLastDecision(uint32 nowMs, std::string text)
        {
            m_lastDecisionMs = nowMs;
            m_lastDecision = std::move(text);
        }
        std::string const& GetLastDecision() const { return m_lastDecision; }
        uint32 GetLastDecisionMs() const { return m_lastDecisionMs; }

    private:
        uint32 m_holdStartMs = 0;
        uint32 m_holdDurationMs = 0;
        uint32 m_lastDecisionMs = 0;
        std::string m_lastDecision;
    };
}

#endif
