/*
 * PartyBot Commander layer 3 (cerebellum): step out of a self-destruct's blast (doctrine todo 9,
 * user 2026-11-10: kill it when the party can, step out when it cannot; doc
 * .kiro/docs/2026-11-09-doctrine/2026-11-10-roles-sources.md).
 *
 * For every enemy the doctrine stamped STAMP_STAND_CLEAR (Coordination/Doctrine/RoleStamps.h)
 * that this bot is not the victim of (it follows its victim: walking away does not help) and
 * whose blast (Tags::BlastOf) reaches this bot: walk straight away from it to its radius +
 * CLEAR_MARGIN_YD and hold there until its latest blast. Urgent tier;
 *   benefit = Points(the blast after armor) + the death penalty if it would kill the bot
 *   cost    = LostAction(the walk out and back)
 * The nearest such enemy only. Wakes the bot's tick while it stands in a blast.
 */

#ifndef MANGOS_COMMANDER_BLAST_BEHAVIOR_H
#define MANGOS_COMMANDER_BLAST_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class BlastBehavior : public IBehavior
    {
    public:
        static constexpr float CLEAR_MARGIN_YD = 2.0f;
        static constexpr float PATH_FACTOR = 1.2f;
        static constexpr uint32 HOLD_EXTRA_MS = 500;
        static constexpr uint32 HOLD_MAX_MS = 10 * 1000;

        char const* GetId() const override { return "blast"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
        bool WantsTickNow(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const override;
    };
}

#endif
