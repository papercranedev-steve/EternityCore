#include "Commander/Registry.h"
#include "Commander/Behaviors/ThreatBehavior.h"
#include "Commander/Behaviors/InterruptBehavior.h"
#include "Commander/Behaviors/PullBehavior.h"
#include "Commander/Coordination/InterruptAssigner.h"
#include "Commander/Coordination/PullPlanner.h"

namespace Commander
{
    // One line per behavior module.
    void RegisterBehaviors(BehaviorList& out)
    {
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<ThreatBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<InterruptBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<PullBehavior>()));
    }

    // One line per team assignment module.
    void RegisterAssigners(AssignerList& out)
    {
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<PullAssigner>()));
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<InterruptAssigner>()));
    }
}
