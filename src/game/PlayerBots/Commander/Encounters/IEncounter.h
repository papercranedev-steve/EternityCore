/*
 * PartyBot Commander: boss modules, layered on top of the generic modules
 * (code-structure doc 3.4, architecture-layers doc).
 *
 * Unlike behaviors and assigners, encounter modules keep state (phase, when the adds came,
 * ...). So they are not shared: every PartyCommander creates its own instances from the
 * explicit list in EncounterRegistry.cpp (only the ones for its map), and only that
 * commander's map thread touches them.
 *
 * Call order on a perception refresh (PartyCommander::Update, map thread):
 *   WorldModel refresh
 *   -> pick the active module (Matches), Refresh (live objects allowed), AddFacts
 *   -> assigners
 *   -> AdjustPlan (overrides plan fields such as plan.focus)
 * Every combat tick of a bot: EncounterBehavior ("encounter") forwards Propose to the
 * active module of that bot's commander.
 * OnReset: when Matches turns false (fight over: no boss and no boss adds left), when the
 * "encounter" module is switched off, or when another module takes over.
 */

#ifndef MANGOS_COMMANDER_IENCOUNTER_H
#define MANGOS_COMMANDER_IENCOUNTER_H

#include "Common.h"
#include "Commander/CommanderTypes.h"
#include <memory>
#include <vector>

class PartyBotAI;

namespace Commander
{
    struct WorldModel;
    struct TeamPlan;

    class IEncounter
    {
    public:
        virtual ~IEncounter() = default;

        // Short id for logs and notes.
        virtual char const* GetId() const = 0;

        // Cheap: map id plus "is one of my entries in world.enemies". Called on every refresh
        // of a commander on this module's map.
        virtual bool Matches(WorldModel const& world, uint32 mapId) const = 0;

        // Map thread, once per refresh while active. The only place a module may read live
        // game objects (instance data, auras); `bot` is whichever party bot triggered the refresh.
        virtual void Refresh(PartyBotAI* /*bot*/, WorldModel const& /*world*/) {}

        // Optional: add facts to the world model before the assigners run.
        virtual void AddFacts(WorldModel& /*world*/) {}

        // After the assigners: change the plan they wrote (focus, notes, ...).
        virtual void AdjustPlan(WorldModel const& /*world*/, TeamPlan& /*plan*/) {}

        // Every combat tick of a bot of this party (through EncounterBehavior). Must not change
        // state: one call per bot, several bots per refresh.
        virtual void Propose(PartyBotAI const* /*bot*/, WorldModel const& /*world*/,
                             TeamPlan const& /*plan*/, std::vector<Proposal>& /*out*/) const {}

        // True while the active module needs the Commander hook out of combat too, and bots must
        // not sit down to eat or drink (an event with waves: the next group is already on its
        // way). Read by PartyCommander::WantsOutOfCombatControl on every bot tick, so it must
        // be cheap: return a value computed in Refresh.
        virtual bool WantsOutOfCombatControl() const { return false; }

        // Fight over or module switched off: forget everything about this attempt.
        virtual void OnReset() = 0;
    };

    using EncounterList = std::vector<std::unique_ptr<IEncounter>>;
}

#endif
