/*
 * PartyBot Commander: explicit module registry.
 * Self-registering static objects do not work here: game is a static library and
 * the linker drops .obj files nobody references, so every module is listed by hand
 * in Registry.cpp.
 */

#ifndef MANGOS_COMMANDER_REGISTRY_H
#define MANGOS_COMMANDER_REGISTRY_H

#include "Commander/Behaviors/IBehavior.h"
#include <atomic>
#include <memory>
#include <vector>

namespace Commander
{
    struct BehaviorSlot
    {
        explicit BehaviorSlot(std::unique_ptr<IBehavior> b) : behavior(std::move(b)) {}
        std::unique_ptr<IBehavior> behavior;
        std::atomic<bool> enabled{ true };
    };

    using BehaviorList = std::vector<std::unique_ptr<BehaviorSlot>>;

    // Called once at startup. Order only matters for ties in the arbiter.
    void RegisterBehaviors(BehaviorList& out);
}

#endif
