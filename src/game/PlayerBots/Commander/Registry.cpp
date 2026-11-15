#include "Commander/Registry.h"
#include "Commander/Behaviors/Combat/ThreatBehavior.h"
#include "Commander/Behaviors/Combat/InterruptBehavior.h"
#include "Commander/Behaviors/Movement/PullBehavior.h"
#include "Commander/Behaviors/Support/DispelBehavior.h"
#include "Commander/Behaviors/Combat/TargetBehavior.h"
#include "Commander/Behaviors/Support/ManaBudgetBehavior.h"
#include "Commander/Behaviors/Support/HealPlanBehavior.h"
#include "Commander/Behaviors/Movement/RetreatBehavior.h"
#include "Commander/Behaviors/Movement/PositioningBehavior.h"
#include "Commander/Behaviors/Movement/HealerStanceBehavior.h"
#include "Commander/Behaviors/Movement/RangedStanceBehavior.h"
#include "Commander/Behaviors/Movement/MeleeStanceBehavior.h"
#include "Commander/Behaviors/Movement/TankStanceBehavior.h"
#include "Commander/Behaviors/Support/RestBehavior.h"
#include "Commander/Behaviors/Movement/EscapeBehavior.h"
#include "Commander/Behaviors/Movement/KiteBehavior.h"
#include "Commander/Behaviors/Combat/AreaDamageBehavior.h"
#include "Commander/Behaviors/Combat/AreaRoomBehavior.h"
#include "Commander/Behaviors/Movement/PatrolBehavior.h"
#include "Commander/Behaviors/Movement/OffRouteBehavior.h"
#include "Commander/Behaviors/Movement/BlastBehavior.h"
#include "Commander/Behaviors/Combat/CrowdControlBehavior.h"
#include "Commander/Behaviors/Combat/RescueBehavior.h"
#include "Commander/Behaviors/Combat/FleeStopBehavior.h"
#include "Commander/Behaviors/Combat/BurstBehavior.h"
#include "Commander/Behaviors/Combat/HarvestBehavior.h"
#include "Commander/Behaviors/Support/HealthTradeBehavior.h"
#include "Commander/Behaviors/Combat/FinishCastBehavior.h"
#include "Commander/Behaviors/Support/BoostBehavior.h"
#include "Commander/Coordination/Duties/BoostAssigner.h"
#include "Commander/Encounters/EncounterBehavior.h"
#include "Commander/Coordination/Duties/CrowdControlAssigner.h"
#include "Commander/Coordination/Duties/RescueAssigner.h"
#include "Commander/Coordination/Duties/FleeStopAssigner.h"
#include "Commander/Coordination/Doctrine/ControlledEnd.h"
#include "Commander/Coordination/Situation/FightSpot/FightSpotAssigner.h"
#include "Commander/Coordination/Situation/Patrol/PatrolAssigner.h"
#include "Commander/Coordination/Situation/Patrol/RouteGuard.h"
#include "Commander/Coordination/Situation/Patrol/PatrolHold.h"
#include "Commander/Coordination/Doctrine/AreaPhase.h"
#include "Commander/Coordination/Doctrine/TacticalDoctrine.h"
#include "Commander/Coordination/Duties/InterruptAssigner.h"
#include "Commander/Coordination/Duties/DispelAssigner.h"
#include "Commander/Coordination/Duties/FocusAssigner.h"
#include "Commander/Coordination/Duties/TargetAssigner.h"
#include "Commander/Coordination/Situation/Pull/PullPlanner.h"
#include "Commander/Coordination/Situation/RetreatPlanner.h"
#include "Commander/Coordination/Situation/RoleAssigner.h"

namespace Commander
{
    // One line per behavior module.
    void RegisterBehaviors(BehaviorList& out)
    {
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<ThreatBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<InterruptBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<PullBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<DispelBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<CrowdControlBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<RescueBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<FleeStopBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<BurstBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<BoostBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<TargetBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<EncounterBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<ManaBudgetBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<HealthTradeBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<HarvestBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<HealPlanBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<RetreatBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<PositioningBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<HealerStanceBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<RangedStanceBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<MeleeStanceBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<TankStanceBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<RestBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<EscapeBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<KiteBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<AreaDamageBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<AreaRoomBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<PatrolBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<OffRouteBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<BlastBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<FinishCastBehavior>()));
    }

    // One line per team assignment module.
    void RegisterAssigners(AssignerList& out)
    {
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<RoleAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<PullAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<TacticalDoctrine>()));
        // Reads this refresh's doctrine holds (the pull's patrol hold); RouteGuard reads it.
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<PatrolHoldAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<InterruptAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<FocusAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<TargetAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<DispelAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<CrowdControlAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<RescueAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<FleeStopAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<BoostAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<ControlledEndAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<RetreatAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<FightSpotAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<PatrolAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<RouteGuardAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<AreaPhaseAssigner>()));
    }
}
