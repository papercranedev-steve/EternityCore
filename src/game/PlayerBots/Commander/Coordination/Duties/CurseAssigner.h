/*
 * PartyBot Commander layer 2: which damage curse each warlock bot keeps on its victim ("curse"
 * module, user 2026-11-15).
 *
 * Count order (Order): every party member (players, not pets) counts for the curse its class
 * profits from (WarlockCurses::Favoured; tanks and healers count for none), most first, ties in
 * the order Elements, Shadow, Recklessness; a curse nobody profits from is left out.
 *
 * The warlock bots in guid order each take, on their victim, the curse they know with the
 * highest benefit - cost (Perception/Values/CurseValue.h) above 0, that no warlock before them
 * took on the same enemy; ties in the count order. The one it already keeps there stays while it
 * pays (no flipping on small changes). Example: Magistrate Barthilas (attack power 242, melee x9)
 * gains more from +90 attack power on the tank than a warrior and a hunter gain from -640
 * armor, so the warlock takes Shadow for its own damage (user 2026-11-16).
 * No victim yet, or nothing pays on it: the first of the count order it knows (CurseBehavior
 * proposes it, the arbiter drops it while it does not pay: trash that dies first).
 * Not a Duties module: DutyAllocator gives a bot one primary duty, the curse is kept all fight.
 * Runs every refresh (pure function of the world model).
 */

#ifndef MANGOS_COMMANDER_CURSE_ASSIGNER_H
#define MANGOS_COMMANDER_CURSE_ASSIGNER_H

#include "Commander/Coordination/IAssigner.h"
#include "Commander/Perception/Profiles/WarlockCurses.h"
#include <vector>

namespace Commander
{
    struct WorldModel;

    class CurseAssigner : public IAssigner
    {
    public:
        // The curses wanted for this party by count, most wanted first (header).
        static std::vector<CurseKind> Order(WorldModel const& world);

        char const* GetId() const override { return "curse"; }
        AssignStage GetStage() const override { return AssignStage::Tasks; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
