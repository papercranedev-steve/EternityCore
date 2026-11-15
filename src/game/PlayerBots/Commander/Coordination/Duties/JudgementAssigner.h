/*
 * PartyBot Commander layer 2: which judgement each paladin bot keeps on its victim
 * ("judgements" assigner, user 2026-11-15, plan A).
 *
 * Paladins that melee (not healers: they stand back, and only the judging paladin's own melee
 * hits keep the 10 s debuff up, Perception/Profiles/PaladinBuffs.h) and know Judgement and a
 * judgement seal. In guid order each takes the first of Wisdom (the hitters' mana), Light (their
 * health) that it knows and nobody has yet; a third paladin gets none and keeps its own seal.
 * A judgement another paladin (a player too) already has on the victim is not put on again
 * (PaladinRotation checks the victim).
 *
 * Not a Duties module: DutyAllocator gives a bot one primary duty, this is its standing seal.
 * Runs every refresh (pure function of the world model).
 */

#ifndef MANGOS_COMMANDER_JUDGEMENT_ASSIGNER_H
#define MANGOS_COMMANDER_JUDGEMENT_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class JudgementAssigner : public IAssigner
    {
    public:
        char const* GetId() const override { return "judgements"; }
        AssignStage GetStage() const override { return AssignStage::Tasks; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
