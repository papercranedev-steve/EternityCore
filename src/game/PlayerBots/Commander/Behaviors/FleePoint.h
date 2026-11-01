/*
 * PartyBot Commander: where a bot runs when it gets away from a melee enemy
 * (PartyBotAI::RunAwayFromTarget: mage Frost Nova + run, priest / hunter / druid / healer
 * distancing, rogue Vanish).
 *
 * The original move goes 15 yd straight away from the enemy. In the first Stockade test run
 * (2026-11-01, combat recording stockade-20261101-133307) the enemies came from the party's
 * side, so the mage ran deeper into the hall twice and pulled 8 more mobs.
 *
 * Find() uses what the server knows (positions and aggro ranges of every creature that is not
 * fighting yet, SafeRoute.h; a player would know the room from looking at it):
 *  - candidates: the tank and the other party members, and a ring of points around the bot;
 *  - rejected: no ground near the bot's height, not farther from the enemy, the end point or
 *    any point of the path inside an idle hostile creature's aggro range plus a margin;
 *  - preferred: the tank (it picks the enemy up), the party's centre, distance from the enemy.
 * False = nowhere safe: the caller stays and fights instead of pulling another pack.
 *
 * Map thread of the bot (reads creatures and pathfinds). Only called when a bot decides to
 * run, a few times per fight.
 */

#ifndef MANGOS_COMMANDER_FLEE_POINT_H
#define MANGOS_COMMANDER_FLEE_POINT_H

#include "Common.h"

class Player;
class Unit;

namespace Commander
{
    namespace FleePoint
    {
        // Ring of candidate points around the bot.
        float const RING_DISTANCES[] = { 12.0f, 18.0f };
        uint32 const RING_ANGLES = 12;
        // Party members farther than this are not run to.
        float const MAX_ALLY_DISTANCE = 35.0f;
        // Idle creature scan, clearance and path sampling: SafeRoute.h.
        // Must end at least this much farther from the enemy than now (the tank: no rule, it
        // takes the enemy over), and never closer than MIN_ENEMY_GAP.
        float const MIN_GAIN = 4.0f;
        float const MIN_ENEMY_GAP = 8.0f;
        // Ground must be within this much of the bot's height (no ledges, no floor above).
        float const MAX_HEIGHT_DIFFERENCE = 6.0f;
        // Path may be at most this much longer than the straight line.
        float const MAX_PATH_DETOUR = 2.0f;
        // Pathfinding is the expensive check: only the best this many candidates are tried.
        uint32 const MAX_PATH_CHECKS = 6;
        // Scoring (yards): enemy distance counts up to ENEMY_DISTANCE_CAP; running to the tank
        // is worth TANK_BONUS; every yard away from the party's centre costs CENTRE_WEIGHT.
        float const ENEMY_DISTANCE_CAP = 20.0f;
        float const TANK_BONUS = 15.0f;
        float const CENTRE_WEIGHT = 0.5f;

        bool Find(Player* me, Unit* enemy, float& x, float& y, float& z);
    }
}

#endif
