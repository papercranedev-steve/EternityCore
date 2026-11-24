/*
 * PartyBot Commander layer 2 (situation), the pull stance in the open world: is a pack one the
 * player wants pulled? (design .kiro/docs/2026-11-24-pull-stance.md §3). Pure on the facts.
 *
 *   Pull     it comes anyway (PackCreature::comesAnyway), he has it selected (not an elite), a
 *            quest of a member still needs it (questNeeded; not an elite), or its camp is one
 *            he wants (Camps.h).
 *   Ask      elites a quest needs or he selected (also while he travels); while he wanders
 *            (LeaderActivityFacts::wanders): packs that give experience and start fights.
 *   Exclude  bosses, other players' (tapped, claimed), more than MAX_LEVEL_ABOVE over him, a camp
 *            he answered "leave", gray or neutral ones (unless a quest's), anything else while he
 *            travels.
 */

#ifndef MANGOS_COMMANDER_PACK_JUDGE_H
#define MANGOS_COMMANDER_PACK_JUDGE_H

#include "Common.h"
#include "Commander/Coordination/TeamPlan.h"
#include <vector>

namespace Commander
{
    namespace PackJudge
    {
        uint32 const MAX_LEVEL_ABOVE = 3;

        enum class Verdict : uint8 { Pull, Ask, Exclude };

        struct Input
        {
            std::vector<CampMemory> const& camps;
            uint32 leaderLevel;
            uint32 grayLevel;       // his gray level (no experience at or below)
            ObjectGuid selection;   // what he has selected
            bool wanders;
        };

        // `camp`: the index in Input::camps of the camp that decided, -1 = none did.
        Verdict Judge(PackFacts const& pack, Input const& in, int32& camp);
    }
}

#endif
