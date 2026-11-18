#include "Commander/Registry.h"
#include "Commander/Behaviors/Combat/ThreatBehavior.h"
#include "Commander/Behaviors/Combat/InterruptBehavior.h"
#include "Commander/Behaviors/Movement/PullBehavior.h"
#include "Commander/Behaviors/Support/DispelBehavior.h"
#include "Commander/Behaviors/Combat/TargetBehavior.h"
#include "Commander/Behaviors/Support/ManaBudgetBehavior.h"
#include "Commander/Behaviors/Support/HealPlanBehavior.h"
#include "Commander/Behaviors/Movement/RetreatBehavior.h"
#include "Commander/Behaviors/Movement/PushBehavior.h"
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
#include "Commander/Behaviors/Movement/LureBehavior.h"
#include "Commander/Behaviors/Movement/OffRouteBehavior.h"
#include "Commander/Behaviors/Movement/BlastBehavior.h"
#include "Commander/Behaviors/Combat/CrowdControlBehavior.h"
#include "Commander/Behaviors/Combat/RescueBehavior.h"
#include "Commander/Behaviors/Combat/FleeStopBehavior.h"
#include "Commander/Behaviors/Combat/BurstBehavior.h"
#include "Commander/Behaviors/Combat/HarvestBehavior.h"
#include "Commander/Behaviors/Support/HealthTradeBehavior.h"
#include "Commander/Behaviors/Support/PetCareBehavior.h"
#include "Commander/Behaviors/Combat/FinishCastBehavior.h"
#include "Commander/Behaviors/Support/BoostBehavior.h"
#include "Commander/Behaviors/Support/ResurrectBehavior.h"
#include "Commander/Behaviors/Support/WipeBehavior.h"
#include "Commander/Coordination/Situation/Crisis/WipeAssigner.h"
#include "Commander/Coordination/Duties/ResurrectAssigner.h"
#include "Commander/Behaviors/Combat/MitigationBehavior.h"
#include "Commander/Behaviors/Combat/StoneformBehavior.h"
#include "Commander/Behaviors/Combat/RenewSurvivalBehavior.h"
#include "Commander/Behaviors/Combat/CurseBehavior.h"
#include "Commander/Coordination/Duties/CurseAssigner.h"
#include "Commander/Coordination/Duties/BoostAssigner.h"
#include "Commander/Coordination/Duties/BlessingAssigner.h"
#include "Commander/Coordination/Duties/TotemAssigner.h"
#include "Commander/Coordination/Duties/JudgementAssigner.h"
#include "Commander/Encounters/EncounterBehavior.h"
#include "Commander/Coordination/Duties/CrowdControlAssigner.h"
#include "Commander/Coordination/Duties/RescueAssigner.h"
#include "Commander/Coordination/Duties/FleeStopAssigner.h"
#include "Commander/Coordination/Doctrine/ControlledEnd.h"
#include "Commander/Coordination/Situation/FightSpot/FightSpotAssigner.h"
#include "Commander/Coordination/Situation/Patrol/PatrolAssigner.h"
#include "Commander/Coordination/Situation/Los/CasterLureAssigner.h"
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
#include "Commander/Coordination/Situation/Crisis/CrisisAssigner.h"
#include "Commander/Coordination/Situation/Crisis/RunAwayAssigner.h"
#include "Commander/Coordination/Situation/Crisis/SelfRiseAssigner.h"
#include "Commander/Coordination/Situation/RoleAssigner.h"
#include "Commander/Coordination/Situation/Hunt/HuntAssigner.h"
#include "Commander/Behaviors/Combat/HuntBehavior.h"

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
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<RenewSurvivalBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<StoneformBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<FleeStopBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<BurstBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<BoostBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<ResurrectBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<WipeBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<MitigationBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<CurseBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<TargetBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<HuntBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<EncounterBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<ManaBudgetBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<HealthTradeBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<PetCareBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<HarvestBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<HealPlanBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<RetreatBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<PushBehavior>()));
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
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<LureBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<OffRouteBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<BlastBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<FinishCastBehavior>()));
    }

    // One line per team assignment module.
    void RegisterAssigners(AssignerList& out)
    {
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<RoleAssigner>()));
        // A team of bots with no player: its next fight (reads the pull and retreat of the last refresh).
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<HuntAssigner>()));
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
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<ResurrectAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<BlessingAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<TotemAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<JudgementAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<CurseAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<ControlledEndAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<SelfRiseAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<CrisisAssigner>()));
        // Reads this refresh's crisis; RetreatPlanner::Step starts what it asks for.
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<RunAwayAssigner>()));
        // Reads this refresh's crisis and the last refresh's declined retreat.
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<WipeAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<RetreatAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<FightSpotAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<PatrolAssigner>()));
        // Reads this refresh's patrol guard (its path's casters are its own lures).
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<CasterLureAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<RouteGuardAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<AreaPhaseAssigner>()));
    }
}
