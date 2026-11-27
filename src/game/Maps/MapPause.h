/*
 * Single-player pause (Map::Pause / Map::Resume, Maps/MapPause.cpp).
 */

#ifndef MANGOS_MAP_PAUSE_H
#define MANGOS_MAP_PAUSE_H

class Player;

// True when the player's map is paused: the packet handler drops the action (casting, using an
// item or object, attacking, looting, pet orders). Tells the player's addon again (PBPAUSE;1),
// so its overlay comes back after a /reload.
bool IsRefusedWhilePaused(Player* player);

#endif
