/*
 * PartyBot Commander layer 2: which damage curse each warlock bot keeps on its victim ("curse"
 * module, user 2026-11-15).
 *
 * Every party member (players, not pets) counts for the curse its class profits from
 * (WarlockCurses::Favoured; tanks and healers count for none). The curses go by that count,
 * most first, ties in the order Elements, Shadow, Recklessness; a curse nobody profits from is
 * left out. The warlock bots in guid order each take the first curse of that list they know
 * that no warlock before them took. The dead count too: the curses do not swap when someone
 * dies.
 *
 * Whether it goes on an enemy at all is the value's (Perception/Values/CurseValue.h): trash
 * dies before it pays, bosses and long fights get it. CurseBehavior casts.
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
        // The curses wanted for this party, most wanted first (header).
        static std::vector<CurseKind> Order(WorldModel const& world);

        char const* GetId() const override { return "curse"; }
        AssignStage GetStage() const override { return AssignStage::Tasks; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
