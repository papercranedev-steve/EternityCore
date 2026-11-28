/*
 * PartyBot Commander layer 2 (brain, situation): eating and drinking during an event with waves
 * (boss doc 2.3, user 2026-11-28). Read from WorldModel::waves each refresh; PartyCommander
 * writes the result into TeamPlan::noRest / noNewRest.
 *
 *   Player       Free: the player starts the next wave, the party drinks to full meanwhile.
 *   Continuous   StandUp for the whole event: one long fight.
 *   Timer        StandUp while the party fights, or the next wave reaches it within
 *                WaveEvent::STAND_LEAD_SEC (those sitting get up, full or not);
 *                NoNew while it comes within STAND_LEAD_SEC + WaveEvent::SIT_SEC (a new drink
 *                would not pay; one on goes on); Free otherwise (none coming, or far).
 * The boss module's own ForbidsRest comes on top (PartyCommander).
 */

#ifndef MANGOS_COMMANDER_WAVE_REST_H
#define MANGOS_COMMANDER_WAVE_REST_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    namespace WaveRest
    {
        enum class Rule : uint8
        {
            Free,
            NoNew,
            StandUp,
        };

        Rule Of(WorldModel const& world);
        char const* Name(Rule rule);
    }
}

#endif
