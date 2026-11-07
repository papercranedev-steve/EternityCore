/*
 * PartyBot Commander: explicit module registry.
 * Self-registering static objects do not work here: game is a static library and
 * the linker drops .obj files nobody references, so every module is listed by hand
 * in Registry.cpp.
 */

#ifndef MANGOS_COMMANDER_REGISTRY_H
#define MANGOS_COMMANDER_REGISTRY_H

#include "Commander/Behaviors/IBehavior.h"
#include "Commander/Coordination/IAssigner.h"
#include <atomic>
#include <memory>
#include <vector>

namespace Commander
{
    template <class T>
    struct ModuleSlot
    {
        explicit ModuleSlot(std::unique_ptr<T> m) : module(std::move(m)) {}
        std::unique_ptr<T> module;
        std::atomic<bool> enabled{ true };
    };

    using BehaviorSlot = ModuleSlot<IBehavior>;
    using AssignerSlot = ModuleSlot<IAssigner>;
    using BehaviorList = std::vector<std::unique_ptr<BehaviorSlot>>;
    using AssignerList = std::vector<std::unique_ptr<AssignerSlot>>;

    // Called once at startup. Behavior order only matters for ties in the arbiter.
    // Assigners run by stage (IAssigner.h); inside the Situation and Tasks stages in this order.
    void RegisterBehaviors(BehaviorList& out);
    void RegisterAssigners(AssignerList& out);
}

#endif
