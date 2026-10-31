/*
 * PartyBot Commander layer 2: team assignment modules.
 * Assign runs once per perception refresh of a party, after the world model is rebuilt.
 * Assigners are shared by all parties on all map threads: no member state, read the
 * previous assignment from `plan` instead (it still holds the last result).
 * Assigners are pure functions of WorldModel + TeamPlan, so they can be replayed offline.
 * Jev hooks (BuildQuestion / ApplyAnswer, code-structure doc 3.2) come later.
 */

#ifndef MANGOS_COMMANDER_IASSIGNER_H
#define MANGOS_COMMANDER_IASSIGNER_H

#include "Common.h"

namespace Commander
{
    struct WorldModel;
    struct TeamPlan;

    class IAssigner
    {
    public:
        virtual ~IAssigner() = default;
        virtual char const* GetId() const = 0;
        virtual void Assign(WorldModel const& world, TeamPlan& plan) const = 0;
        // Called instead of Assign when the module is disabled.
        virtual void Clear(TeamPlan& plan) const = 0;
    };
}

#endif
