#include "Commander/Registry.h"
#include "Commander/Behaviors/ThreatBehavior.h"
#include "Commander/Behaviors/InterruptBehavior.h"
#include "Commander/Behaviors/PullBehavior.h"
#include "Commander/Behaviors/DispelBehavior.h"
#include "Commander/Behaviors/FocusBehavior.h"
#include "Commander/Behaviors/ManaBudgetBehavior.h"
#include "Commander/Behaviors/RetreatBehavior.h"
#include "Commander/Behaviors/PositioningBehavior.h"
#include "Commander/Encounters/EncounterBehavior.h"
#include "Commander/Coordination/InterruptAssigner.h"
#include "Commander/Coordination/FocusAssigner.h"
#include "Commander/Coordination/PullPlanner.h"
#include "Commander/Coordination/RetreatPlanner.h"

namespace Commander
{
    // One line per behavior module.
    void RegisterBehaviors(BehaviorList& out)
    {
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<ThreatBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<InterruptBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<PullBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<DispelBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<FocusBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<EncounterBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<ManaBudgetBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<RetreatBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<PositioningBehavior>()));
    }

    // One line per team assignment module.
    void RegisterAssigners(AssignerList& out)
    {
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<PullAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<InterruptAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<FocusAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<RetreatAssigner>()));
    }
}
