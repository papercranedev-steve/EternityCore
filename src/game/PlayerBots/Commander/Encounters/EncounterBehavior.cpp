#include "Commander/Encounters/EncounterBehavior.h"
#include "Commander/Encounters/EncounterRegistry.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "PartyBotAI.h"

namespace Commander
{
    bool EncounterBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return EncounterRegistry::HasEncounterOnMap(bot->me->GetMapId());
    }

    void EncounterBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                    TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        // Same key as the commander that called the arbiter (group, map, instance), so this is
        // the commander whose world / plan we were given. Map thread of that commander.
        std::shared_ptr<PartyCommander> commander = sCommanderMgr.FindCommander(bot->me);
        if (!commander)
            return;
        if (IEncounter const* encounter = commander->GetActiveEncounter())
            encounter->Propose(bot, world, plan, out);
    }
}
