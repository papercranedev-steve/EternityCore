/*
 * PartyBot Commander: adapter that plugs boss modules into the arbiter.
 * Registered as a normal behavior (id "encounter"); its Propose finds the bot's
 * PartyCommander and forwards to that commander's active boss module.
 * Switching "encounter" off (CommanderMgr::SetModuleEnabled / PartyBot.Commander.DisabledModules)
 * also stops Refresh / AdjustPlan (PartyCommander::Update checks the same id).
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_BEHAVIOR_H
#define MANGOS_COMMANDER_ENCOUNTER_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

namespace Commander
{
    class EncounterBehavior : public IBehavior
    {
    public:
        static char const* Id() { return "encounter"; }

        char const* GetId() const override { return Id(); }
        // Only on maps that have a boss module: elsewhere the commander lookup is skipped.
        bool AppliesTo(TeamBotAI const* bot) const override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
    };
}

#endif
