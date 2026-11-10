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
 *  - candidates: the tank and the other party members (ground within MAX_HEIGHT_DIFFERENCE of
 *    the bot), and since 2026-11-10 (position field step B) the cells of the position field
 *    CELL_MIN_DISTANCE..CELL_MAX_DISTANCE from the bot (FieldSpots::Collect: on the ground
 *    linked to the party, so a slope counts; no idle creature; no patrol before the fight
 *    ends; a grid walk around idle creatures gets there). Before: a ring at 12 and 18 yd with
 *    a height check against the bot's own. No field (no world model): party members only;
 *  - rejected: not farther from the enemy, the end point or any point of the path inside an
 *    idle hostile creature's aggro range plus a margin (this bot's own scan, best first);
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
    struct WorldModel;

    namespace FleePoint
    {
        // Band of field cells around the bot (the ring before was at 12 and 18 yd).
        float const CELL_MIN_DISTANCE = 10.0f;
        float const CELL_MAX_DISTANCE = 20.0f;
        // Party members farther than this are not run to.
        float const MAX_ALLY_DISTANCE = 35.0f;
        // Idle creature scan, clearance and path sampling: SafeRoute.h.
        // Must end at least this much farther from the enemy than now (the tank: no rule, it
        // takes the enemy over), and never closer than MIN_ENEMY_GAP.
        float const MIN_GAIN = 4.0f;
        float const MIN_ENEMY_GAP = 8.0f;
        // Party members: ground within this much of the bot's height (no ledges, no floor above).
        float const MAX_HEIGHT_DIFFERENCE = 6.0f;
        // Path may be at most this much longer than the straight line.
        float const MAX_PATH_DETOUR = 2.0f;
        // Pathfinding is the expensive check: only the best this many candidates are tried
        // (candidates this bot's own idle scan rejects at the end do not count).
        uint32 const MAX_PATH_CHECKS = 6;
        // Scoring (yards): enemy distance counts up to ENEMY_DISTANCE_CAP; running to the tank
        // is worth TANK_BONUS; every yard away from the party's centre costs CENTRE_WEIGHT.
        float const ENEMY_DISTANCE_CAP = 20.0f;
        float const TANK_BONUS = 15.0f;
        float const CENTRE_WEIGHT = 0.5f;

        // `world`: the party's world model (who the tanks are, Perception/Tanks.h); may be null.
        bool Find(Player* me, Unit* enemy, WorldModel const* world, float& x, float& y, float& z);
    }
}

#endif
