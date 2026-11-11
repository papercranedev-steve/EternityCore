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

namespace Commander
{
    struct WorldModel;
    struct DoctrinePlan;

    namespace AreaIntent
    {
        // Fills d.areaThreat and d.areaSec (d.tactic is Area).
        void Fill(WorldModel const& world, DoctrinePlan& d);
    }
}

#endif
