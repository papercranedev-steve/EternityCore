#include "Commander/Registry.h"
#include "Commander/Behaviors/ThreatBehavior.h"
#include "Commander/Behaviors/InterruptBehavior.h"
#include "Commander/Coordination/InterruptAssigner.h"

namespace Commander
{
    // One line per behavior module.
    void RegisterBehaviors(BehaviorList& out)
    {
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<ThreatBehavior>()));
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<InterruptBehavior>()));
    }

    // One line per team assignment module.
    void RegisterAssigners(AssignerList& out)
    {
        out.push_back(std::make_unique<AssignerSlot>(std::make_unique<InterruptAssigner>()));
    }
}
