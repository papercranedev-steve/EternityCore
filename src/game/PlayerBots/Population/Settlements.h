/*
 * World population bots: settlements, the places people gather on a continent (user
 * 2026-11-19: towns look busy, the ring around them has more hunters, the wild few).
 *
 * Built from the world data, one settlement per place:
 *  - capitals (CityDirector::Cities),
 *  - start areas (playercreateinfo positions; most have no inn: Northshire, Coldridge, ...),
 *  - towns (innkeeper spawns: Goldshire, Razor Hill, Booty Bay, ...),
 *  - camps (flight master spawns with no inn nearby),
 *  - manual hubs (scene points of type "hub", .popbot point add world hub).
 * Sources close to each other become one settlement (the stronger kind keeps its centre and
 * side). Who may use it comes from the source NPC's faction: a town hostile to a side is not
 * that side's town (Crossroads is no Alliance gathering place), neutral towns serve both.
 *
 * Each kind has a core radius (the place itself), a falloff length and a weight; the town
 * crowd (inside the core) and the field density (outside it) read them. World thread only.
 */

#ifndef MANGOS_POPULATION_SETTLEMENTS_H
#define MANGOS_POPULATION_SETTLEMENTS_H

#include "Common.h"
#include <string>
#include <utility>
#include <vector>

namespace Population
{
    class ScenePoints;

    // In merge priority order: a source joins a stronger settlement nearby.
    enum class SettlementKind : uint8
    {
        Capital,
        StartArea,
        Town,
        Camp,
        Manual,
    };

    char const* SettlementKindName(SettlementKind kind);

    struct Settlement
    {
        uint32 id = 0;                  // index + 1, stable until the next Load
        SettlementKind kind = SettlementKind::Town;
        uint32 mapId = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float coreYd = 80.0f;           // the place itself
        float falloffYd = 450.0f;       // outside the core: e^(-distance / falloff)
        float weight = 0.7f;            // 1 = capital / start area
        bool alliance = false;          // a gathering place of this side
        bool horde = false;
        uint32 cityZone = 0;            // capitals: the city zone (CityDef::zone)
        uint32 raceMask = 0;            // start areas: the races beginning here (Coldridge: dwarf + gnome)
        std::string label;              // source NPC name, city title or "point #id"
        uint32 sources = 1;             // merged sources (inns, flight masters, ...)

        bool ServesTeam(uint32 team) const;
    };

    class Settlements
    {
    public:
        // Re-reads every source (creature data must be loaded). Manual hubs from `points`.
        void Load(ScenePoints const& points);

        std::vector<Settlement> const& All() const { return m_all; }
        Settlement const* Find(uint32 id) const;

        // Settlements of `mapId` whose core edge is within `rangeYd` (2d) of x/y, with that
        // distance (0 inside the core), nearest first.
        std::vector<std::pair<float, Settlement const*>> Near(uint32 mapId, float x, float y, float rangeYd) const;

        // How strongly the settlements of `team` draw people to x/y (0..1): the strongest of
        // weight * e^(-edge / (falloff * falloffScale)), edge = distance beyond its core (0
        // inside). Negative exponential like a city's population density (Clark).
        float Pull(uint32 mapId, float x, float y, uint32 team, float falloffScale) const;

        // x/y is in a race's start area (within START_AREA_YD of its start spot and not in the
        // core of another settlement such as Goldshire or Dolanaar): its races (race mask), else
        // 0. Only those races are met there (user 2026-11-21: other races in Northshire felt off).
        uint32 StartRacesAt(uint32 mapId, float x, float y) const;

        // Sub-zone name at the settlement (terrain lookup, for commands and logs).
        static std::string AreaName(Settlement const& s);

    private:
        std::vector<Settlement> m_all;
    };
}

#endif
