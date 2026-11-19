/*
 * World population bots: the kinds of places in a city or town, read from the world database
 * (shared by CityPopulation and TownPopulation), and how often people go there during their
 * day in town (TownLife.h).
 *
 * NPCs by their flags: inn (innkeeper), smith (repair), flight, trainer, vendor, quest (quest
 * givers, critters excluded: Northshire's chickens). Gameobjects: chair, mail, smith (anvil,
 * forge), fire (cooking fire). Cities also have ah and bank NPCs and manual scene points
 * (idle, showcase).
 * World thread (loading).
 */

#ifndef MANGOS_POPULATION_PLACE_ANCHORS_H
#define MANGOS_POPULATION_PLACE_ANCHORS_H

#include "Common.h"
#include <string>

struct CreatureInfo;
struct GameObjectInfo;

namespace Population
{
    // nullptr: not a place.
    char const* NpcAnchorType(CreatureInfo const* info);
    char const* ObjectAnchorType(GameObjectInfo const* info);

    // Relative weight of going to a place of this type next (0: never). `startArea`: a race's
    // start area, whose chairs are inside buildings while the crowd is outside (user 2026-11-19).
    uint32 VisitWeight(std::string const& type, bool startArea);
}

#endif
