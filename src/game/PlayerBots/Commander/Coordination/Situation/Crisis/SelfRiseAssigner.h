/*
 * PartyBot Commander layer 2 (brain), situation: a dead tank or healer bot that keeps a self
 * resurrection (Soulstone, Reincarnation) rises in the fight when that wins it (user
 * 2026-11-15: "the shaman's Reincarnation: will we win once it is up?"). Otherwise it keeps
 * the stone for after the fight (CorpseRun: rises once it is safe, then resurrects the rest).
 *
 * While the party fights, for every dead bot (not a pet) with PartyMemberFacts::selfRes whose
 * role is tank or healer:
 *   as is   FightLoad::FromWorld (no tank alive: it cannot be run, counts as lost);
 *   risen   the same with it back at riseHealth / riseMana (FightLoad::Lineup::risen; a tank
 *           as the tank, a healer with its heals).
 * It rises only when risen is bearable and as is is not (a fight won anyway keeps the stone
 * for afterwards: the wipe protection's reviver), and no acting enemy that is not on a tank
 * stands within DANGER_YD of its body (it would be the first thing hit, at a fraction of its
 * health). One per refresh (the next decision sees it up). A rise calls the wipe off
 * (TeamPlan::wipe: the fight is not lost any more).
 * Writes TeamPlan::rises (kept until the fight is over), one note per rise.
 * Pure.
 */

#ifndef MANGOS_COMMANDER_SELF_RISE_ASSIGNER_H
#define MANGOS_COMMANDER_SELF_RISE_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class SelfRiseAssigner : public IAssigner
    {
    public:
        static constexpr float DANGER_YD = 10.0f;   // untested guess

        char const* GetId() const override { return "selfrise"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
