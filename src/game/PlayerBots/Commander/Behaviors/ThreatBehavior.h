/*
 * PartyBot Commander layer 3: threat concern (step 3).
 * Design: .kiro/docs/2026-11-01-threat-behavior.md
 *
 *  Tank    - loose enemy: taunt, or win it back with threat when that is fast enough
 *            for the person being hit (section 3); otherwise top up the smallest lead.
 *  Damage  - stop hitting a target whose headroom is below 10% of the holder's threat;
 *            switch to a safe target if there is one.
 *  Healer  - if the heal the rotation would cast pulls aggro, cast a smaller one or wait;
 *            targets below 30% health are healed regardless.
 */

#ifndef MANGOS_COMMANDER_THREAT_BEHAVIOR_H
#define MANGOS_COMMANDER_THREAT_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    struct EnemyFacts;
    struct PartyMemberFacts;

    class ThreatBehavior : public IBehavior
    {
    public:
        // Tunables (user approved 2026-11-01, see design doc section 2 / 3).
        static constexpr float DPS_STOP_HEADROOM = 0.10f;     // x holder's threat
        static constexpr float DPS_SWITCH_HEADROOM = 0.25f;   // alternative target needs this much
        static constexpr float DPS_SWITCH_RANGE = 30.0f;
        static constexpr float HEAL_IGNORE_THREAT_PCT = 30.0f;
        static constexpr float TANK_LEAD_SWITCH = 0.15f;      // lead / holder threat
        static constexpr float TANK_LEAD_KEEP = 0.30f;        // current target must be at least this safe to leave it
        static constexpr float TOLERANCE_FRAGILE_SEC = 1.0f;  // healers, cloth
        static constexpr float TOLERANCE_STURDY_SEC = 3.0f;
        static constexpr float TOLERANCE_ELITE_FACTOR = 0.7f;
        static constexpr float TOLERANCE_MIN_HEALTH_FACTOR = 0.3f;

        char const* GetId() const override { return "threat"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        bool ControlsTaunt(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;

        // Seconds the unit being hit can take before the tank must taunt. Pure.
        static float Tolerance(PartyMemberFacts const& victim, EnemyFacts const& enemy);

    private:
        void ProposeTank(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const;
        void ProposeDamage(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const;
        void ProposeHealer(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const;
    };
}

#endif
