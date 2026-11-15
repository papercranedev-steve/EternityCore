/*
 * PartyBot Commander layer 2 (brain), situation: contingencies, first part (user 2026-11-15:
 * "the tank died, the healer died: can we still win?"). Sees the loss and forecasts who could
 * carry the fight; tells the leader. Nobody acts on it yet (the stand-in, the spare healers
 * and the wipe protection come next), so this is the eyes' forecast put in front of the player
 * and checked against what happens.
 *
 * While the party fights (WorldModel::HasPartyEnemy):
 *   tank loss    the party has tank-role members (not pets) and none is alive.
 *   healer loss  the party has healer-role members and none is alive with at least
 *                HEALER_OOM_PCT mana (players' mana counts too).
 * A controlled tank or healer is not a loss (a few seconds; the rescue and the dispels).
 *
 * Options (FightLoad::FromWorld with a FightLoad::Lineup, the fight as it stands now):
 *   tank loss    every stand-in (Perception/Profiles/StandIn.h) as the tank; with a healer loss
 *                too, every spare healer heals as well (not the stand-in itself).
 *   healer loss  (tank up) as is, each spare healer alone, all of them together.
 * Spare healers: bots that can act, not healer or tank bots, with PartyMemberFacts::spareHealRates
 * (hybrids). Players are not counted: other roles may fail (ai-architecture rule).
 * Best = the highest FightLoad::Score (ties: the earlier, fewer changes).
 *
 * Writes TeamPlan::crisis every refresh; a note when the loss, the best option's stand-in, its
 * spare healers or whether it is bearable changes. Cleared once the fight is over.
 *
 * Takeover (step 2, user 2026-11-15): the best option is run, bearable or not (a stand-in is
 * better than the enemies loose on the casters; the wipe protection comes later). Its stand-in
 * bot gets ROLE_TANK, its spare healers ROLE_HEALER (TeamPlan::emergencyRoles, kept until the
 * fight is over; PartyCommander::ApplyEmergencyRoles sets them on the bots and gives the old
 * roles back after the fight). From then on the whole party treats it as the tank or a
 * healer (Tanks.h, the rescue's taunts, the heal module), so the loss is over and the crisis
 * clears; a stand-in that dies is a new loss with the next stand-in. A pet stand-in gets no
 * role: with no main tank its taunt on autocast holds what hits it (ThreatBehavior).
 * Pure.
 */

#ifndef MANGOS_COMMANDER_CRISIS_ASSIGNER_H
#define MANGOS_COMMANDER_CRISIS_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class CrisisAssigner : public IAssigner
    {
    public:
        static constexpr float HEALER_OOM_PCT = 5.0f;
        // The last best option stays while its FightLoad::Score is at most this much lower.
        static constexpr float STICKY_SCORE = 5.0f;

        char const* GetId() const override { return "crisis"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;

        // The CrisisLoss bits of the party now (header), 0 outside a fight.
        static uint8 LossOf(WorldModel const& world);
    };
}

#endif
