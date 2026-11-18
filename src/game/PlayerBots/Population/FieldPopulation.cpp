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
    }

    void FieldDirector::LoadConfig()
    {
        m_enabled = sConfig.GetBoolDefault("PopBot.Field.Enable", true);
        m_maxUnits = ClampU(sConfig.GetIntDefault("PopBot.Field.MaxUnits", 4), 0, 20);
        m_radius = float(ClampU(sConfig.GetIntDefault("PopBot.Field.Radius", 250), 100, 500));
        m_spawnMinYd = float(ClampU(sConfig.GetIntDefault("PopBot.Field.SpawnMinYd", 130), 60, 400));
        m_spawnMaxYd = std::max(m_spawnMinYd + 30.0f, float(ClampU(sConfig.GetIntDefault("PopBot.Field.SpawnMaxYd", 240), 90, 500)));
        m_keepYd = std::max(m_spawnMaxYd + 30.0f, float(ClampU(sConfig.GetIntDefault("PopBot.Field.KeepYd", 320), 150, 800)));
        m_checkMinMs = ClampU(sConfig.GetIntDefault("PopBot.Field.CheckMinSec", 15), 2, 600) * IN_MILLISECONDS;
        m_checkMaxMs = std::max(m_checkMinMs, ClampU(sConfig.GetIntDefault("PopBot.Field.CheckMaxSec", 45), 2, 600) * IN_MILLISECONDS);
    }

    bool FieldDirector::Fills(Observer const& o) const
    {
        if (o.travelling || (o.mapId != 0 && o.mapId != 1))
            return false;
        return !IsCityZone(o.zone);
    }

    bool FieldDirector::BuildRequest(Observer const& o, Observers const& observers, SpawnRequest& out)
    {
        Map* map = sMapMgr.FindMap(o.mapId, 0);
        FactionTemplateEntry const* own = sObjectMgr.GetFactionTemplateEntry(o.factionTemplate);
        if (!map || !own)
            return false;

        float const inner = std::max(m_spawnMinYd, map->GetVisibilityDistance() + SPAWN_SIGHT_MARGIN);
        float const outer = std::max(m_spawnMaxYd, inner + 30.0f);
        std::vector<FieldSpot const*> spots = m_spots.Around(o.mapId, o.x, o.y, inner, outer);
        Shuffle(spots);

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

            // A few yards from the spawn point, on the ground, in sight of it.
            float const angle = frand(0.0f, 2.0f * float(M_PI));
            float const dist = frand(3.0f, 7.0f);
            float const x = spot->x + std::cos(angle) * dist;
            float const y = spot->y + std::sin(angle) * dist;
            float const z = map->GetHeight(x, y, spot->z + 3.0f);
            if (z <= INVALID_HEIGHT || std::fabs(z - spot->z) > 3.0f ||
                !map->isInLineOfSight(spot->x, spot->y, spot->z + 2.0f, x, y, z + 2.0f))
                continue;
            if (observers.CanSee(o.mapId, x, y, z))
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
            out.prewarm = o.arriving;
            return true;
        }
        return false;
    }

    void FieldDirector::Update(PopulationMgr& mgr, Observers const& observers, uint32 nowMs)
    {
        if (nowMs < m_nextTickMs)
            return;
        m_nextTickMs = nowMs + TICK_MS;

        if (!m_enabled)
        {
            mgr.DespawnField();
            return;
        }

        // Out of sight and not needed here any more: gone.
        mgr.RetireFieldBots(observers, m_keepYd);

        for (Observer const& o : observers.All())
        {
            if (!Fills(o))
                continue;
            uint32& next = m_nextCheckMs[o.guid];
            if (!o.arriving && nowMs < next)
                continue;
            next = nowMs + urand(m_checkMinMs, m_checkMaxMs);

            // One more unit per check; an arriving observer gets the whole crowd at once.
            uint32 const want = o.arriving ? m_maxUnits : 1;
            for (uint32 i = 0; i < want; ++i)
            {
                if (mgr.CountField(o.mapId, o.x, o.y, m_radius) >= m_maxUnits)
                    break;
                SpawnRequest request;
                if (!BuildRequest(o, observers, request) || !mgr.QueueSpawn(request))
                    break;
            }
        }
    }
}
