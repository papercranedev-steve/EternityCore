/*
 * PartyBot Commander layer 2 (brain, doctrine): the area spell the Area tactic counts on, as
 * planned threat (.kiro/docs/2026-11-11-aoe-threat.md 4.1 and 4.3, user 2026-11-12: the
 * opening holds the pile through the whole area spell).
 *
 * Every damage dealer bot that can act with an area spell it can cast now (AreaSpellFacts::
 * ready) or channels now: its spell with the most threat per second (tickThreat / period) on
 * each enemy it hits. Which one the area phase picks is not known yet, so each of them is a
 * rival of the tank (ThreatRunway takes the shortest). areaSec = the longest of these spells
 * (ticks x period): how far ahead the runway is judged.
 */

#ifndef MANGOS_COMMANDER_AREA_INTENT_H
#define MANGOS_COMMANDER_AREA_INTENT_H

#include "Commander/Perception/Forecast/ThreatRunway.h"
#include <vector>

namespace Commander
{
    struct WorldModel;
    struct DoctrinePlan;
    struct TeamPlan;
    struct PartyMemberFacts;
    struct AreaSpellFacts;

    namespace AreaIntent
    {
        // The list (header); `sec`: the longest spell's length (may be null).
        std::vector<PlannedThreat> Of(WorldModel const& world, float* sec);

        // A member's area spell with the most threat per second it can cast now (or channels).
        AreaSpellFacts const* BestSpell(PartyMemberFacts const& m);

        // Fills d.areaThreat and d.areaSec (d.tactic is Area).
        void Fill(WorldModel const& world, DoctrinePlan& d);

        // The caster the Area tactic counts on (the one with the most threat per second of the
        // list), null = none. Before the fight (no tactic yet) a pull of
        // TacticalDoctrine::AREA_MIN or more counts as Area. (Was AreaPhase's until 2026-11-14;
        // the fight spot asks it too.)
        PartyMemberFacts const* PlannedCaster(WorldModel const& world, TeamPlan const& plan);
    }
}

#endif
