#include "Commander/Registry.h"
#include "Commander/Behaviors/ThreatBehavior.h"
#include "Commander/Behaviors/InterruptBehavior.h"
#include "Commander/Behaviors/PullBehavior.h"
#include "Commander/Behaviors/DispelBehavior.h"
#include "Commander/Behaviors/TargetBehavior.h"
#include "Commander/Behaviors/ManaBudgetBehavior.h"
#include "Commander/Behaviors/HealPlanBehavior.h"
#include "Commander/Behaviors/RetreatBehavior.h"
#include "Commander/Behaviors/PositioningBehavior.h"
#include "Commander/Behaviors/HealerStanceBehavior.h"
#include "Commander/Behaviors/RangedStanceBehavior.h"
#include "Commander/Behaviors/EscapeBehavior.h"
#include "Commander/Behaviors/AreaDamageBehavior.h"
#include "Commander/Behaviors/PatrolBehavior.h"
#include "Commander/Behaviors/CrowdControlBehavior.h"
#include "Commander/Encounters/EncounterBehavior.h"
#include "Commander/Coordination/CrowdControlAssigner.h"
#include "Commander/Coordination/ControlledEnd.h"
#include "Commander/Coordination/PatrolAssigner.h"
#include "Commander/Coordination/InterruptAssigner.h"
#include "Commander/Coordination/DispelAssigner.h"
#include "Commander/Coordination/FocusAssigner.h"
#include "Commander/Coordination/TargetAssigner.h"
#include "Commander/Coordination/PullPlanner.h"
#include "Commander/Coordination/RetreatPlanner.h"
#include "Commander/Coordination/RoleAssigner.h"

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
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<TargetBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<EncounterBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<ManaBudgetBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<HealPlanBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<RetreatBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<PositioningBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<HealerStanceBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<RangedStanceBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<EscapeBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<AreaDamageBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<PatrolBehavior>()));
    }

    // One line per team assignment module.
    void RegisterAssigners(AssignerList& out)
    {
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<RoleAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<PullAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<InterruptAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<FocusAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<TargetAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<DispelAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<CrowdControlAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<ControlledEndAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<RetreatAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<PatrolAssigner>()));
    }
}
