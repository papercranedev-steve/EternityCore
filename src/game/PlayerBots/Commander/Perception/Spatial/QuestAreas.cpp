#include "Commander/Perception/Spatial/QuestAreas.h"
#include "LootMgr.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace Commander
{
namespace Quests
{
    namespace
    {
        // Quest ids from here on are the code-built mirrors (Shared/QuestMirror).
        uint32 const CODE_QUEST_FIRST = 100000;
        uint32 const ALLIANCE_PLAYER_FACTION = 1;
        uint32 const HORDE_PLAYER_FACTION = 2;
        // Tiles loaded around giver and area for the way.
        float const TILE_MARGIN_YD = 250.0f;
        float const SIMPLIFY_YD = 3.0f;
        // Kills acted out: the counter's count, a few more for a drop.
        uint32 const NEED_MIN = 3, NEED_MAX = 15;

        // A creature the population hunts (as Population/FieldSpots).
        bool IsHunted(CreatureInfo const* info)
        {
            if (!info || info->npc_flags || info->civilian || info->rank != CREATURE_ELITE_NORMAL)
                return false;
            if (info->flags_extra & CREATURE_FLAG_EXTRA_GUARD)
                return false;
            switch (info->type)
            {
                case CREATURE_TYPE_CRITTER:
                case CREATURE_TYPE_TOTEM:
                case CREATURE_TYPE_NOT_SPECIFIED:
                    return false;
            }
            return info->level_max > 0;
        }

        struct Spawn
        {
            uint32 mapId;
            RoadPoint p;
        };

        float Dist2d(RoadPoint const& a, RoadPoint const& b)
        {
            return Roads::Distance2d(a, b);
        }

        // DBSCAN over `points` (one map): cluster index per point, -1 noise.
        std::vector<int32> Dbscan(std::vector<RoadPoint> const& points, float eps, uint32 minPts, int32& clusters)
        {
            size_t const n = points.size();
            std::vector<int32> label(n, -2);    // -2 unvisited, -1 noise
            clusters = 0;
            float const eps2 = eps * eps;
            auto neighbours = [&](size_t i, std::vector<size_t>& out)
            {
                out.clear();
                for (size_t j = 0; j < n; ++j)
                {
                    float const dx = points[i].x - points[j].x, dy = points[i].y - points[j].y;
                    if (dx * dx + dy * dy <= eps2)
                        out.push_back(j);
                }
            };
            std::vector<size_t> seeds, more;
            for (size_t i = 0; i < n; ++i)
            {
                if (label[i] != -2)
                    continue;
                neighbours(i, seeds);
                if (seeds.size() < minPts)
                {
                    label[i] = -1;
                    continue;
                }
                int32 const c = clusters++;
                label[i] = c;
                for (size_t k = 0; k < seeds.size(); ++k)
                {
                    size_t const j = seeds[k];
                    if (label[j] == -1)
                        label[j] = c;           // border point
                    if (label[j] != -2)
                        continue;
                    label[j] = c;
                    neighbours(j, more);
                    if (more.size() >= minPts)
                        seeds.insert(seeds.end(), more.begin(), more.end());
                }
            }
            return label;
        }
    }

    bool QuestArea::ServesTeam(uint32 team) const
    {
        return team == HORDE ? horde : alliance;
    }

    void QuestAreaIndex::Load()
    {
        auto const started = std::chrono::steady_clock::now();
        m_areas.clear();
        m_next = 0;
        m_ways = 0;
        m_buildMs = 0;

        struct Wanted
        {
            Quest const* quest = nullptr;
            uint32 creature = 0;    // a kill objective
            uint32 item = 0;        // else an item objective (who drops it)
            uint32 count = 0;
        };
        std::vector<Wanted> wanted;
        std::unordered_set<uint32> items;
        for (auto const& itr : sObjectMgr.GetQuestTemplates())
        {
            Quest const* q = itr.second.get();
            if (!q || q->GetQuestId() >= CODE_QUEST_FIRST || q->GetType() != 0 || q->GetRequiredClasses() ||
                q->GetZoneOrSort() <= 0 || q->IsRepeatable())
                continue;
            Wanted w;
            w.quest = q;
            for (uint32 i = 0; i < QUEST_OBJECTIVES_COUNT && !w.creature; ++i)
                if (q->ReqCreatureOrGOId[i] > 0 && !q->ReqSpell[i] && q->ReqCreatureOrGOCount[i])
                {
                    w.creature = uint32(q->ReqCreatureOrGOId[i]);
                    w.count = q->ReqCreatureOrGOCount[i];
                }
            for (uint32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT && !w.creature && !w.item; ++i)
                if (q->ReqItemId[i] && q->ReqItemCount[i])
                {
                    w.item = q->ReqItemId[i];
                    w.count = q->ReqItemCount[i];
                    items.insert(w.item);
                }
            if (w.creature || w.item)
                wanted.push_back(w);
        }

        // Who gives which quest, who drops which wanted item.
        std::unordered_map<uint32, std::vector<uint32>> givers;     // quest -> creature entries
        std::unordered_map<uint32, std::vector<uint32>> droppers;   // item -> hunted creature entries
        std::vector<uint32> drops;
        for (auto const& itr : sObjectMgr.GetCreatureInfoMap())
        {
            CreatureInfo const* info = itr.second.get();
            if (!info)
                continue;
            QuestRelationsMapBounds const bounds = sObjectMgr.GetCreatureQuestRelationsMapBounds(itr.first);
            for (auto r = bounds.first; r != bounds.second; ++r)
                givers[r->second].push_back(itr.first);
            if (info->loot_id && IsHunted(info))
                if (LootTemplate const* loot = LootTemplates_Creature.GetLootFor(info->loot_id))
                {
                    drops.clear();
                    loot->CollectOwnItems(drops);
                    for (uint32 item : drops)
                        if (items.count(item))
                            droppers[item].push_back(itr.first);
                }
        }

        // The spawns of the givers and the prey (continents).
        std::unordered_set<uint32> entries;
        for (Wanted const& w : wanted)
        {
            auto const g = givers.find(w.quest->GetQuestId());
            if (g == givers.end())
                continue;
            entries.insert(g->second.begin(), g->second.end());
            if (w.creature)
                entries.insert(w.creature);
            else
            {
                auto const d = droppers.find(w.item);
                if (d != droppers.end())
                    entries.insert(d->second.begin(), d->second.end());
            }
        }
        std::unordered_map<uint32, std::vector<Spawn>> spawns;
        auto collect = [&](CreatureDataPair const& pair)
        {
            CreatureData const& d = pair.second;
            if ((d.position.mapId != 0 && d.position.mapId != 1) || (d.spawn_flags & (SPAWN_FLAG_DISABLED | SPAWN_FLAG_DEAD)))
                return false;
            for (uint32 entry : d.creature_id)
                if (entry && entries.count(entry))
                    spawns[entry].push_back({ d.position.mapId, { d.position.x, d.position.y, d.position.z } });
            return false;
        };
        sObjectMgr.DoCreatureData(collect);

        FactionTemplateEntry const* ally = sObjectMgr.GetFactionTemplateEntry(ALLIANCE_PLAYER_FACTION);
        FactionTemplateEntry const* horde = sObjectMgr.GetFactionTemplateEntry(HORDE_PLAYER_FACTION);
        if (!ally || !horde)
            return;

        for (Wanted const& w : wanted)
        {
            auto const g = givers.find(w.quest->GetQuestId());
            if (g == givers.end())
                continue;

            // The giver's spawns and the sides it gives to.
            std::vector<Spawn> giverSpawns;
            uint32 giverEntry = 0;
            bool alliance = false, hordeSide = false;
            for (uint32 entry : g->second)
            {
                CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);
                FactionTemplateEntry const* ft = info ? sObjectMgr.GetFactionTemplateEntry(info->faction) : nullptr;
                auto const s = spawns.find(entry);
                if (!ft || s == spawns.end())
                    continue;
                bool const a = !ft->IsHostileTo(*ally), h = !ft->IsHostileTo(*horde);
                if (!a && !h)
                    continue;
                alliance |= a;
                hordeSide |= h;
                if (!giverEntry)
                    giverEntry = entry;
                giverSpawns.insert(giverSpawns.end(), s->second.begin(), s->second.end());
            }
            if (giverSpawns.empty())
                continue;

            std::vector<uint32> prey;
            if (w.creature)
            {
                if (IsHunted(sObjectMgr.GetCreatureTemplate(w.creature)))
                    prey.push_back(w.creature);
            }
            else
            {
                auto const d = droppers.find(w.item);
                if (d != droppers.end())
                    prey = d->second;
            }

            // The cluster of any prey nearest to a giver spawn.
            QuestArea best;
            float bestDist = MAX_GIVER_YD;
            for (uint32 entry : prey)
            {
                auto const s = spawns.find(entry);
                if (s == spawns.end())
                    continue;
                for (uint32 mapId : { 0u, 1u })
                {
                    std::vector<RoadPoint> points;
                    for (Spawn const& sp : s->second)
                        if (sp.mapId == mapId)
                            points.push_back(sp.p);
                    if (points.size() < CLUSTER_MIN)
                        continue;
                    int32 clusters = 0;
                    std::vector<int32> const label = Dbscan(points, CLUSTER_EPS_YD, CLUSTER_MIN, clusters);
                    for (int32 c = 0; c < clusters; ++c)
                    {
                        std::vector<RoadPoint> members;
                        RoadPoint mean;
                        for (size_t i = 0; i < points.size(); ++i)
                            if (label[i] == c)
                            {
                                members.push_back(points[i]);
                                mean.x += points[i].x;
                                mean.y += points[i].y;
                            }
                        if (members.size() < CLUSTER_MIN)
                            continue;
                        mean.x /= float(members.size());
                        mean.y /= float(members.size());
                        RoadPoint center = members.front();
                        for (RoadPoint const& p : members)
                            if (Dist2d(p, mean) < Dist2d(center, mean))
                                center = p;
                        for (Spawn const& gs : giverSpawns)
                        {
                            if (gs.mapId != mapId)
                                continue;
                            float const d = Dist2d(gs.p, center);
                            if (d >= bestDist)
                                continue;
                            bestDist = d;
                            best = QuestArea();
                            best.mapId = mapId;
                            best.center = center;
                            best.spawns = members;
                            best.preyEntry = entry;
                            best.giver = gs.p;
                        }
                    }
                }
            }
            if (!best.preyEntry)
                continue;

            CreatureInfo const* preyInfo = sObjectMgr.GetCreatureTemplate(best.preyEntry);
            best.id = uint32(m_areas.size() + 1);
            best.questId = w.quest->GetQuestId();
            for (RoadPoint const& p : best.spawns)
                best.radiusYd = std::max(best.radiusYd, Dist2d(p, best.center));
            uint32 const count = w.creature ? w.count : w.count + w.count / 2;
            best.need = std::min(NEED_MAX, std::max(NEED_MIN, count));
            best.preyLevelMin = uint8(std::min<uint32>(preyInfo->level_min, 60));
            best.preyLevelMax = uint8(std::min<uint32>(preyInfo->level_max, 60));
            best.questLevel = uint8(std::min<uint32>(w.quest->GetQuestLevel(), 60));
            best.minLevel = uint8(std::min<uint32>(w.quest->GetMinLevel(), 60));
            best.alliance = alliance;
            best.horde = hordeSide;
            best.giverEntry = giverEntry;
            m_areas.push_back(std::move(best));
        }

        m_loaded = true;
        uint32 const ms = uint32(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PopBot] Quest areas: %u of %u kill and drop quests, %u ms.",
            uint32(m_areas.size()), uint32(wanted.size()), ms);
    }

    bool QuestAreaIndex::Step()
    {
        if (m_next >= m_areas.size())
            return false;
        auto const started = std::chrono::steady_clock::now();
        QuestArea& area = m_areas[m_next++];

        RoadPoint const& g = area.giver;
        RoadPoint const& c = area.center;
        if (m_pather.BeginArea(area.mapId, std::min(g.x, c.x) - TILE_MARGIN_YD, std::min(g.y, c.y) - TILE_MARGIN_YD,
                std::max(g.x, c.x) + TILE_MARGIN_YD, std::max(g.y, c.y) + TILE_MARGIN_YD))
        {
            std::vector<RoadPoint> way;
            if (m_pather.Path(g, c, way))
            {
                area.way = Roads::Simplify(way, SIMPLIFY_YD);
                area.along.assign(area.way.size(), 0.0f);
                for (size_t i = 1; i < area.way.size(); ++i)
                    area.along[i] = area.along[i - 1] + Dist2d(area.way[i - 1], area.way[i]);
                area.wayLength = area.along.back();
                ++m_ways;
            }
        }
        m_pather.EndArea();

        m_buildMs += uint32(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
        if (m_next >= m_areas.size())
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PopBot] Quest ways built: %u of %u areas, %u ms.",
                m_ways, uint32(m_areas.size()), m_buildMs);
        return m_next < m_areas.size();
    }

    std::vector<QuestArea const*> QuestAreaIndex::Near(uint32 mapId, float x, float y, float radius) const
    {
        std::vector<QuestArea const*> out;
        RoadPoint const p = { x, y, 0.0f };
        for (QuestArea const& a : m_areas)
            if (a.mapId == mapId && Dist2d(a.center, p) <= radius + a.radiusYd)
                out.push_back(&a);
        return out;
    }

    float QuestAreaIndex::Project(QuestArea const& area, float x, float y, float& dist)
    {
        dist = 1e9f;
        float best = 0.0f;
        for (size_t i = 1; i < area.way.size(); ++i)
        {
            RoadPoint const& a = area.way[i - 1];
            RoadPoint const& b = area.way[i];
            float const dx = b.x - a.x, dy = b.y - a.y;
            float const len2 = dx * dx + dy * dy;
            float t = len2 > 0.0f ? ((x - a.x) * dx + (y - a.y) * dy) / len2 : 0.0f;
            t = std::min(1.0f, std::max(0.0f, t));
            float const ex = a.x + dx * t - x, ey = a.y + dy * t - y;
            float const d = std::sqrt(ex * ex + ey * ey);
            if (d < dist)
            {
                dist = d;
                best = area.along[i - 1] + t * (area.along[i] - area.along[i - 1]);
            }
        }
        return best;
    }

    RoadPoint QuestAreaIndex::PointAt(QuestArea const& area, float s)
    {
        if (area.way.empty())
            return area.center;
        if (s <= 0.0f)
            return area.way.front();
        for (size_t i = 1; i < area.way.size(); ++i)
            if (s <= area.along[i])
            {
                float const span = area.along[i] - area.along[i - 1];
                float const t = span > 0.0f ? (s - area.along[i - 1]) / span : 0.0f;
                RoadPoint const& a = area.way[i - 1];
                RoadPoint const& b = area.way[i];
                return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
            }
        return area.way.back();
    }
}
}
