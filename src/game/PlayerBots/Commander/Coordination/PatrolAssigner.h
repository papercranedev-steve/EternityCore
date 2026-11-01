/*
 * PartyBot Commander layer 2: an idle patrol walks into the fight before it ends.
 * From strat-20261101-225111: Patchwork Horror (17000 hp elite, ~70 s loop) walked through the
 * fight spot 28 s into a 7-elite pull and the party wiped.
 *
 * Pure (facts and plan only). Per refresh while the party fights and no move is running:
 *   zone       = patrol aggro range against the tank (PatrolFacts::reach) + ZONE_MARGIN
 *                around every living member
 *   arrive     = first point of the patrol's predicted walk (PatrolFacts::track) inside the
 *                zone; ignored when the fight should end first (Value::FightRemainingSec)
 *   benefit    = its melee on the tank (after armor) from arrival until the end of the fight
 *                plus its own kill time, + Value::FightExtension(its kill time)
 *   spots      = rings of SPOT_DISTANCES around the tank where the zone of the party, moved
 *                along with the tank, is never entered by the predicted walk (as far as the
 *                track goes), nearest first, at most MAX_SPOTS
 *   cost       = Value::FightExtension(walk time to the nearest spot): the fight lasts longer
 *                while the tank walks and the enemies follow
 * benefit > cost and a spot: TeamPlan::patrolMove (PatrolBehavior walks the tank there).
 * Otherwise the party stays and the leader is told why. Each patrol is judged once per fight.
 *
 * Healer and ranged bots are listed as followers (TeamPlan.h, PatrolFollower): they do not
 * chase the enemies and walk the tank's shift themselves (PatrolBehavior).
 *
 * Ends when the tank and the followers arrived, no spot passed the live checks, the patrol joined the fight or
 * left the scan, the retreat or a pull took over, or MOVE_TIMEOUT_MS passed.
 *
 * Known limits: enemies that stand back and cast do not follow the tank (they may lose sight
 * of it, which also brings them closer); the track covers at most
 * WorldModelBuilder::PATROL_FIGHT_HORIZON_SEC; the spot geometry (ground, idle creatures,
 * path) is only checked by the behavior.
 */

#ifndef MANGOS_COMMANDER_PATROL_ASSIGNER_H
#define MANGOS_COMMANDER_PATROL_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"

namespace Commander
{
    class PatrolAssigner : public IAssigner
    {
    public:
        static constexpr float ZONE_MARGIN = 5.0f;
        static constexpr float SPOT_DISTANCES[] = { 15.0f, 25.0f, 35.0f };
        static constexpr uint32 SPOT_ANGLES = 12;
        static constexpr uint32 MAX_SPOTS = 8;
        static constexpr float RUN_YARDS_PER_SEC = 7.0f;
        static constexpr float PATH_FACTOR = 1.3f;
        static constexpr float ARRIVED_DISTANCE = 4.0f;
        static constexpr uint32 MOVE_TIMEOUT_MS = 15 * 1000;

        char const* GetId() const override { return "patrol"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
