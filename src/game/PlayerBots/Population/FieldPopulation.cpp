#include "Population/FieldPopulation.h"
#include "Population/CityPopulation.h"
#include "Population/Observers.h"
#include "Population/PopulationMgr.h"
#include "Config/Config.h"
#include "GridMap.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "Utilities/Random.h"
#include <algorithm>
#include <cmath>

namespace Population
{
    namespace
    {
        // Margin over the map's visibility distance for the spawn ring's inner edge.
        float const SPAWN_SIGHT_MARGIN = 25.0f;
        // Ahead of a moving observer: as close to his sight as Observers::CanSee allows (its
        // margin is 15; the login spot is a few yards off the spawn point, checked again).
        float const AHEAD_SIGHT_MARGIN = 16.0f;
        // Of the spots ahead, the nearest this many are tried in random order.
        size_t const AHEAD_SHUFFLED = 8;
        uint32 const TICK_MS = 2 * IN_MILLISECONDS;
        uint32 const SPOT_TRIES = 40;

        bool IsCityZone(uint32 zone)
        {
            for (CityDef const& def : CityDirector::Cities())
                if (def.zone == zone)
                    return true;
            return false;
        }

        uint32 ClampU(int32 value, uint32 lo, uint32 hi)
        {
            return uint32(std::min<int32>(std::max<int32>(value, int32(lo)), int32(hi)));
        }

        template <class T>
        void Shuffle(std::vector<T>& v)
        {
            for (size_t i = v.size(); i > 1; --i)
                std::swap(v[i - 1], v[urand(0, uint32(i - 1))]);
        }

        // A few yards from a creature's spawn point, on the ground, in sight of it.
        bool GroundSpotNear(Map* map, float sx, float sy, float sz, float& x, float& y, float& z)
        {
            float const angle = frand(0.0f, 2.0f * float(M_PI));
            float const dist = frand(3.0f, 7.0f);
            x = sx + std::cos(angle) * dist;
            y = sy + std::sin(angle) * dist;
            z = map->GetHeight(x, y, sz + 3.0f);
            return z > INVALID_HEIGHT && std::fabs(z - sz) <= 3.0f &&
                   map->isInLineOfSight(sx, sy, sz + 2.0f, x, y, z + 2.0f);
        }

        // Of the quest spawns near an observer: on the way between giver and area (walking past
        // him), the others hunting in the area.
        uint32 const QUEST_WAY_PCT = 40;
        uint32 const QUEST_AREA_TRIES = 6;
        // Beyond the sight on the way, as road travellers (TravellerDirector).
        float const WAY_BEYOND_MIN_YD = 20.0f;
        float const WAY_BEYOND_MAX_YD = 90.0f;
        float const WAY_STEP_YD = 10.0f;
        // Hunting in the area: up to this beyond the spawn ring's inner edge.
        float const AREA_RING_DEPTH_YD = 150.0f;
    }

    void FieldDirector::LoadConfig()
    {
        m_enabled = sConfig.GetBoolDefault("PopBot.Field.Enable", true);
        m_maxUnits = ClampU(sConfig.GetIntDefault("PopBot.Field.MaxUnits", 8), 0, 20);
        m_floor = float(ClampU(sConfig.GetIntDefault("PopBot.Field.FloorPct", 15), 0, 100)) / 100.0f;
        m_falloffScale = float(ClampU(sConfig.GetIntDefault("PopBot.Field.FalloffPct", 100), 20, 500)) / 100.0f;
        m_radius = float(ClampU(sConfig.GetIntDefault("PopBot.Field.Radius", 250), 100, 500));
        m_spawnMinYd = float(ClampU(sConfig.GetIntDefault("PopBot.Field.SpawnMinYd", 130), 60, 400));
        m_spawnMaxYd = std::max(m_spawnMinYd + 30.0f, float(ClampU(sConfig.GetIntDefault("PopBot.Field.SpawnMaxYd", 240), 90, 500)));
        m_keepYd = std::max(m_spawnMaxYd + 30.0f, float(ClampU(sConfig.GetIntDefault("PopBot.Field.KeepYd", 320), 150, 800)));
        m_checkMinMs = ClampU(sConfig.GetIntDefault("PopBot.Field.CheckMinSec", 15), 2, 600) * IN_MILLISECONDS;
        m_checkMaxMs = std::max(m_checkMinMs, ClampU(sConfig.GetIntDefault("PopBot.Field.CheckMaxSec", 45), 2, 600) * IN_MILLISECONDS);
        m_questPct = ClampU(sConfig.GetIntDefault("PopBot.Field.QuestPct", 60), 0, 100);
    }

    void FieldDirector::LoadQuests()
    {
        if (!m_quests.IsLoaded())
            m_quests.Load();
    }

    bool FieldDirector::BuildQuestRequest(Observer const& o, Observers const& observers, Settlements const& settlements, SpawnRequest& out)
    {
        if (!m_questPct || !m_quests.IsLoaded() || urand(0, 99) >= m_questPct)
            return false;
        Map* map = sMapMgr.FindMap(o.mapId, 0);
        if (!map)
            return false;

        std::vector<Commander::Quests::QuestArea const*> areas;
        for (Commander::Quests::QuestArea const* a : m_quests.Near(o.mapId, o.x, o.y, QUEST_NEAR_YD))
            if (a->ServesTeam(o.team))
                areas.push_back(a);
        Shuffle(areas);

        float const visible = map->GetVisibilityDistance();
        uint32 tries = 0;
        for (Commander::Quests::QuestArea const* area : areas)
        {
            if (++tries > QUEST_AREA_TRIES)
                break;

            QuestPlan plan;
            plan.questId = area->questId;
            plan.areaId = area->id;
            plan.preyEntry = area->preyEntry;
            plan.need = area->need;
            plan.areaX = area->center.x;
            plan.areaY = area->center.y;
            plan.areaZ = area->center.z;
            plan.areaRadius = area->radiusYd;
            plan.giverX = area->giver.x;
            plan.giverY = area->giver.y;
            plan.giverZ = area->giver.z;
            for (Commander::Roads::RoadPoint const& p : area->way)
                plan.way.push_back({ p.x, p.y, p.z });

            float x = 0.0f, y = 0.0f, z = 0.0f;
            bool found = false;

            // On the way, coming towards him: from the giver's side to the area, or back.
            if (!area->way.empty() && urand(0, 99) < QUEST_WAY_PCT)
            {
                float dist = 0.0f;
                float const here = Commander::Quests::QuestAreaIndex::Project(*area, o.x, o.y, dist);
                float const minYd = visible + WAY_BEYOND_MIN_YD, maxYd = visible + WAY_BEYOND_MAX_YD;
                for (float step = WAY_STEP_YD; step <= maxYd * 2.0f && !found; step += WAY_STEP_YD)
                {
                    for (int32 side : { -1, 1 })
                    {
                        float const s = here + float(side) * step;
                        if (s <= 0.0f || s >= area->wayLength)
                            continue;
                        Commander::Roads::RoadPoint const p = Commander::Quests::QuestAreaIndex::PointAt(*area, s);
                        float const d = std::sqrt((p.x - o.x) * (p.x - o.x) + (p.y - o.y) * (p.y - o.y));
                        if (d < minYd || d > maxYd)
                            continue;
                        float const ground = map->GetHeight(p.x, p.y, p.z + 5.0f);
                        if (ground <= INVALID_HEIGHT || std::fabs(ground - p.z) > 8.0f ||
                            IsCityZone(sTerrainMgr.GetZoneId(o.mapId, p.x, p.y, ground)) ||
                            observers.CanSeeSoon(o.mapId, p.x, p.y, ground, LOGIN_LEAD_SEC))
                            continue;
                        x = p.x;
                        y = p.y;
                        z = ground;
                        plan.startAlong = s;
                        // Giver's side of him: on its way out; area's side: on its way back.
                        plan.step = side < 0 ? QuestStep::ToArea : QuestStep::ToGiver;
                        found = true;
                        break;
                    }
                }
            }

            // Hunting in the area, beyond his sight.
            if (!found)
            {
                float const inner = std::max(m_spawnMinYd, visible + SPAWN_SIGHT_MARGIN);
                float const outer = inner + AREA_RING_DEPTH_YD;
                std::vector<Commander::Roads::RoadPoint> spawns = area->spawns;
                Shuffle(spawns);
                for (Commander::Roads::RoadPoint const& sp : spawns)
                {
                    float const d = std::sqrt((sp.x - o.x) * (sp.x - o.x) + (sp.y - o.y) * (sp.y - o.y));
                    if (d < inner || d > outer)
                        continue;
                    if (IsCityZone(sTerrainMgr.GetZoneId(o.mapId, sp.x, sp.y, sp.z)))
                        continue;
                    if (!GroundSpotNear(map, sp.x, sp.y, sp.z, x, y, z) || observers.CanSeeSoon(o.mapId, x, y, z, LOGIN_LEAD_SEC))
                        continue;
                    plan.step = QuestStep::Hunt;
                    found = true;
                    break;
                }
            }
            if (!found)
                continue;

            // The levels its creature is a fight for (as the plain field bots), not below the quest's.
            int32 lo = std::max(int32(area->preyLevelMax) - 1, int32(area->minLevel));
            int32 hi = int32(area->preyLevelMin) + 4;
            if (hi < lo)
                hi = lo + 3;

            out = SpawnRequest();
            out.kind = BotKind::Field;
            out.team = o.team;
            out.mapId = o.mapId;
            out.x = x;
            out.y = y;
            out.z = z;
            out.o = frand(0.0f, 2.0f * float(M_PI));
            out.minLevel = uint8(ClampU(lo, 1, 60));
            out.maxLevel = uint8(ClampU(hi, 1, 60));
            out.raceMask = settlements.StartRacesAt(o.mapId, x, y);
            out.prewarm = o.arriving;
            out.quest = std::move(plan);
            return true;
        }
        return false;
    }

    bool FieldDirector::Fills(Observer const& o) const
    {
        if (o.travelling || (o.mapId != 0 && o.mapId != 1))
            return false;
        return !IsCityZone(o.zone);
    }

    float FieldDirector::DensityAt(Settlements const& settlements, uint32 mapId, float x, float y, uint32 team) const
    {
        return m_floor + (1.0f - m_floor) * settlements.Pull(mapId, x, y, team, m_falloffScale);
    }

    uint32 FieldDirector::BudgetAt(Settlements const& settlements, uint32 mapId, float x, float y, uint32 team) const
    {
        return uint32(std::lround(DensityAt(settlements, mapId, x, y, team) * float(m_maxUnits)));
    }

    void FieldDirector::PreferBusySpots(std::vector<FieldSpot const*>& spots, Observer const& o, Settlements const& settlements) const
    {
        // Each spot goes first with its density relative to the busiest one (the others keep
        // their order after them, still tried when the first ones do not fit).
        std::vector<float> density(spots.size());
        float top = 0.0f;
        for (size_t i = 0; i < spots.size(); ++i)
        {
            density[i] = DensityAt(settlements, o.mapId, spots[i]->x, spots[i]->y, o.team);
            top = std::max(top, density[i]);
        }
        if (top <= 0.0f)
            return;
        std::vector<FieldSpot const*> first, later;
        for (size_t i = 0; i < spots.size(); ++i)
            (frand(0.0f, top) <= density[i] ? first : later).push_back(spots[i]);
        first.insert(first.end(), later.begin(), later.end());
        spots.swap(first);
    }

    std::vector<FieldSpot const*> FieldDirector::CandidateSpots(Observer const& o, float visibleYd, Settlements const& settlements)
    {
        if (o.IsMoving())
        {
            // Just beyond his sight where he will be once the bot is in: the observers' sight
            // margin (Observers::CanSee) plus the way he goes during the login.
            float const speed = o.Speed();
            float const inner = visibleYd + AHEAD_SIGHT_MARGIN + speed * LOGIN_LEAD_SEC;
            float const outer = inner + AHEAD_DEPTH_YD;
            float const cosLimit = std::cos(AHEAD_HALF_ANGLE);
            std::vector<FieldSpot const*> ahead;
            for (FieldSpot const* s : m_spots.Around(o.mapId, o.x, o.y, inner, outer))
            {
                float const dx = s->x - o.x, dy = s->y - o.y;
                float const dist = std::sqrt(dx * dx + dy * dy);
                if (dist > 0.0f && (dx * o.vx + dy * o.vy) / (dist * speed) >= cosLimit)
                    ahead.push_back(s);
            }
            if (!ahead.empty())
            {
                // Nearest to his sight first (a few at random among them).
                std::sort(ahead.begin(), ahead.end(), [&o](FieldSpot const* a, FieldSpot const* b)
                {
                    float const da = (a->x - o.x) * (a->x - o.x) + (a->y - o.y) * (a->y - o.y);
                    float const db = (b->x - o.x) * (b->x - o.x) + (b->y - o.y) * (b->y - o.y);
                    return da < db;
                });
                size_t const head = std::min<size_t>(ahead.size(), AHEAD_SHUFFLED);
                for (size_t i = head; i > 1; --i)
                    std::swap(ahead[i - 1], ahead[urand(0, uint32(i - 1))]);
                PreferBusySpots(ahead, o, settlements);
                return ahead;
            }
        }

        float const inner = std::max(m_spawnMinYd, visibleYd + SPAWN_SIGHT_MARGIN);
        float const outer = std::max(m_spawnMaxYd, inner + 30.0f);
        std::vector<FieldSpot const*> spots = m_spots.Around(o.mapId, o.x, o.y, inner, outer);
        Shuffle(spots);
        PreferBusySpots(spots, o, settlements);
        return spots;
    }

    bool FieldDirector::BuildRequest(Observer const& o, Observers const& observers, Settlements const& settlements, SpawnRequest& out)
    {
        Map* map = sMapMgr.FindMap(o.mapId, 0);
        FactionTemplateEntry const* own = sObjectMgr.GetFactionTemplateEntry(o.factionTemplate);
        if (!map || !own)
            return false;

        // Near a quest area: most of them are doing its quest.
        if (BuildQuestRequest(o, observers, settlements, out))
            return true;

        std::vector<FieldSpot const*> const spots = CandidateSpots(o, map->GetVisibilityDistance(), settlements);

        uint32 tries = 0;
        for (FieldSpot const* spot : spots)
        {
            if (++tries > SPOT_TRIES)
                break;
            FactionTemplateEntry const* faction = sObjectMgr.GetFactionTemplateEntry(spot->faction);
            if (!faction || faction->IsFriendlyTo(*own))
                continue;
            if (IsCityZone(sTerrainMgr.GetZoneId(o.mapId, spot->x, spot->y, spot->z)))
                continue;

            float x, y, z;
            if (!GroundSpotNear(map, spot->x, spot->y, spot->z, x, y, z))
                continue;
            if (observers.CanSeeSoon(o.mapId, x, y, z, LOGIN_LEAD_SEC))
                continue;

            // The levels the creatures here are a fight for (Commander HuntAssigner's window:
            // a creature from 4 below to 1 above the bot).
            int32 lo = int32(spot->levelMax) - 1;
            int32 hi = int32(spot->levelMin) + 4;
            if (hi < lo)
                hi = lo + 3;

            out = SpawnRequest();
            out.kind = BotKind::Field;
            out.team = o.team;
            out.mapId = o.mapId;
            out.x = x;
            out.y = y;
            out.z = z;
            out.o = std::atan2(spot->y - y, spot->x - x);
            if (out.o < 0.0f)
                out.o += 2.0f * float(M_PI);
            out.minLevel = uint8(ClampU(lo, 1, 60));
            out.maxLevel = uint8(ClampU(hi, 1, 60));
            // In a start area: only its races (an orc player in Northshire meets nobody there).
            out.raceMask = settlements.StartRacesAt(o.mapId, x, y);
            out.prewarm = o.arriving;
            return true;
        }
        return false;
    }

    void FieldDirector::Update(PopulationMgr& mgr, Observers const& observers, uint32 nowMs)
    {
        // The quests' ways, one per update after the roads (both load navmesh tiles).
        if (m_enabled && m_quests.IsBuilding() && !mgr.GetTravellerDirector().Roads().IsBuilding())
            m_quests.Step();

        if (nowMs < m_nextTickMs)
            return;
        m_nextTickMs = nowMs + TICK_MS;

        if (!m_enabled)
        {
            mgr.DespawnField();
            return;
        }

        // Out of sight and not needed here any more: gone.
        mgr.RetireFieldBots(observers, m_keepYd, BEHIND_SPENT_YD);

        Settlements const& settlements = mgr.GetSettlements();
        for (Observer const& o : observers.All())
        {
            if (!Fills(o))
                continue;
            uint32& next = m_nextCheckMs[o.guid];
            if (!o.arriving && nowMs < next)
                continue;
            // Pacing (PacingDirector): nobody new mid-fight; a long quiet checks sooner and
            // wants more people around.
            Pace const pace = mgr.GetPacing().For(o);
            uint32 const interval = o.IsMoving() ? urand(MOVING_CHECK_MIN_MS, MOVING_CHECK_MAX_MS)
                                                 : urand(m_checkMinMs, m_checkMaxMs);
            next = nowMs + uint32(float(interval) / pace.rate);
            if (pace.relax)
                continue;

            // As many as the place is busy (fewer than there are: the extra ones leave in their
            // own time, out of sight). One more per check; an arriving observer gets them all.
            uint32 const budget = uint32(std::lround(float(BudgetAt(settlements, o.mapId, o.x, o.y, o.team)) * pace.density));
            uint32 const want = o.arriving ? budget : 1;
            for (uint32 i = 0; i < want; ++i)
            {
                if (mgr.CountField(o, m_radius, BEHIND_SPENT_YD) >= budget)
                    break;
                SpawnRequest request;
                if (!BuildRequest(o, observers, settlements, request) || !mgr.QueueSpawn(request))
                    break;
            }
        }
    }
}
