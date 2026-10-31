/*
 * PartyBot Commander layer 3: threat concern (step 3), scored as benefit - cost
 * (value-scoring doc 5.3, 5.4).
 * Design: .kiro/docs/2026-11-01-threat-behavior.md
 *
 *  Tank    - loose enemy: compare doing nothing, winning it back with threat, and taunting.
 *            The loss of each option is the damage the person being hit takes until the enemy
 *            is back (death penalty if they would not survive). A taunt also costs its
 *            cooldown. Otherwise top up the smallest lead (not valued).
 *  Damage  - near the holder's threat: stop hitting, or switch to a safe target, when the
 *            hits taken after pulling aggro outweigh the damage lost.
 *  Healer  - if the heal the rotation would cast pulls aggro: cast a smaller safe heal, or
 *            veto healing that target this tick (the rest of the rotation still runs).
 *            Targets below HEAL_IGNORE_THREAT_PCT are healed regardless.
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

        // Value scoring (value-scoring doc 5.3, 5.4).
        static constexpr float LOOSE_HORIZON_SEC = 10.0f;     // "do nothing" is judged over this long at most
        static constexpr float TAUNT_REACTION_SEC = 0.5f;     // hits taken even when taunting at once
        static constexpr float TAUNT_RESERVE_SEC = 1.0f;      // taunt cooldown cost: this many seconds of enemy melee
        static constexpr float TANK_RECLAIM_SEC = 2.0f;       // how long an enemy hits a dps/healer who pulled it
        static constexpr float SWITCH_TARGET_SEC = 0.5f;      // lost time when a dps switches target

        char const* GetId() const override { return "threat"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        bool ControlsTaunt(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;

    private:
        void ProposeTank(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const;
        void ProposeDamage(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const;
        void ProposeHealer(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const;
    };
}

#endif
