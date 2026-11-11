/*
 * PartyBot Commander layer 1 (eyes): what filling a tank's threat shortfall is worth and how
 * fast it goes (.kiro/docs/2026-11-08-threat-modes.md). How much the tank lacks on an enemy is
 * ThreatRunway::Of's shortfall (Perception/Forecast/ThreatRunway.h; until 2026-11-12 this file's
 * Of: enough to keep it through the healer's next heal, nobody's output after that).
 */

#ifndef MANGOS_COMMANDER_THREAT_GAP_H
#define MANGOS_COMMANDER_THREAT_GAP_H

#include "Commander/Perception/WorldModel.h"

namespace Commander
{
    namespace ThreatGap
    {
        // Threat per second the tank adds to the enemy it hits: measured (LooseEnemy::TankRate),
        // before that its damage estimate x its threat modifier (PartyMemberFacts::soloDps,
        // threatMod). At least 1.
        float FillRate(WorldModel const& world, PartyMemberFacts const& tank);

        // Points per second the party loses while its damage dealer bots (not tanks, not
        // healers) wait for the tank: their Value::ContributionPerSec.
        float WaitCostPerSec(WorldModel const& world, ObjectGuid tank);
    }
}

#endif
