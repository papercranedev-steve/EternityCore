#include "Commander/Registry.h"
#include "Commander/Behaviors/ThreatBehavior.h"

namespace Commander
{
    // One line per behavior module.
    void RegisterBehaviors(BehaviorList& out)
    {
        out.push_back(std::make_unique<BehaviorSlot>(std::make_unique<ThreatBehavior>()));
    }
}
