#include "Commander/Perception/Spatial/PositionFieldBuilder.h"
#include "Commander/Perception/Spatial/AggroRange.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/PatrolZone.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Trace/Trace.h"
#include "Creature.h"
#include "Map.h"
#include "MoveMap.h"
#include "MoveMapSharedDefines.h"
#include "Player.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "Timer.h"
#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <deque>
#include <list>

namespace Commander
{
    namespace
    {
        int32 const CELLS = PositionField::BLOCK_CELLS * PositionField::BLOCK_CELLS;

        uint64 Key(int32 a, int32 b) { return (uint64(uint32(a)) << 32) | uint32(b); }
        uint8 Opposite(uint8 d) { return uint8((d + 4) % FIELD_DIRS); }

        // Distance from `p` to the square of block (bx, by), 2D.
        float BlockDistance(Position3 const& p, int32 bx, int32 by)
        {
            float const x0 = bx * PositionField::BLOCK_YD, y0 = by * PositionField::BLOCK_YD;
            float const dx = std::max(0.0f, std::max(x0 - p.x, p.x - (x0 + PositionField::BLOCK_YD)));
            float const dy = std::max(0.0f, std::max(y0 - p.y, p.y - (y0 + PositionField::BLOCK_YD)));
            return std::sqrt(dx * dx + dy * dy);
        }

        class IdleCheck
        {
        public:
            IdleCheck(std::vector<Player*> const& members, Position3 const& center, float range)
                : m_members(members), m_center(center), m_range(range) {}
            bool operator()(Creature* c)
            {
                if (c->GetDistance2d(m_center.x, m_center.y) > m_range)
                    return false;
                for (Player* m : m_members)
                    if (AggroRange::IsIdleAggressor(c, m))
                        return true;
                return false;
            }

        private:
            std::vector<Player*> const& m_members;
            Position3 m_center;
            float m_range;
        };

        // Distance and height only (the safe side of AggroRange::CoversWandering).
        bool CoversByDistance(Position3 const& from, float range, bool flies, Position3 const& at)
        {
            float const dx = from.x - at.x, dy = from.y - at.y, dz = from.z - at.z;
            if (!flies && std::fabs(dz) > CREATURE_Z_ATTACK_RANGE)
                return false;
            return dx * dx + dy * dy + dz * dz <= range * range;
        }
    }

    PositionFieldBuilder::GroundCell* PositionFieldBuilder::Ground(int32 cx, int32 cy, bool create)
    {
        int32 const bx = PositionField::BlockOf(cx), by = PositionField::BlockOf(cy);
        uint64 const key = Key(bx, by);
        auto it = m_ground.find(key);
        if (it == m_ground.end())
        {
            if (!create)
                return nullptr;
            it = m_ground.emplace(key, GroundBlock()).first;
        }
        int32 const local = (cy - by * PositionField::BLOCK_CELLS) * PositionField::BLOCK_CELLS + (cx - bx * PositionField::BLOCK_CELLS);
        return &it->second.cells[local];
    }

    void PositionFieldBuilder::Build(Map* map, WorldModel& world, TeamPlan const& plan, uint32 nowMs)
    {
        auto const started = std::chrono::steady_clock::now();
        bool const pulling = plan.pull.phase != PullPhase::None;
        // A member in combat counts too (WorldModel::InFight of a bot that joined an ally's fight).
        bool const anyInCombat = std::any_of(world.party.begin(), world.party.end(),
            [](PartyMemberFacts const& m) { return m.inCombat && m.alive; });
        // Travelling (no fight, no pull): only while something near could attack us, for the
        // follow spot and the patrol checks of the brain.
        bool const travelling = !world.HasPartyEnemy() && !pulling && !anyInCombat;
        if (!map || (travelling && !ThreatNear(world)))
        {
            world.field.reset();
            m_field.reset();
            Forget(nowMs);
            return;
        }

        // Participants.
        std::vector<Position3> seeds;
        for (PartyMemberFacts const& m : world.party)
            if (!m.isPet && m.alive)
                seeds.push_back(m.pos);
        for (EnemyFacts const& e : world.enemies)
            seeds.push_back(e.pos);
        if (pulling)
        {
            // (the pull's own spot and the cell the fight spot takes the pile to from there)
            Position3 spot;
            if (plan.pull.PullFightSpot(spot))
                seeds.push_back(spot);
            Position3 planned;
            if (plan.PlannedFightSpot(planned) && PackResolver::Distance(planned, spot) > PositionField::CELL_YD)
                seeds.push_back(planned);
            if (plan.pull.packAt.x != 0.0f || plan.pull.packAt.y != 0.0f)
                seeds.push_back(plan.pull.packAt);
        }
        if (seeds.empty())
        {
            world.field.reset();
            m_field.reset();
            return;
        }

        m_field = std::make_shared<PositionField>();
        PositionField& field = *m_field;
        field.worldVersion = world.version;

        // 1. Blocks within COVER_YD of a participant, nearest first.
        std::unordered_map<uint64, float> nearest;
        for (Position3 const& p : seeds)
        {
            int32 const bx0 = PositionField::BlockOf(PositionField::CellOf(p.x - PositionField::COVER_YD));
            int32 const bx1 = PositionField::BlockOf(PositionField::CellOf(p.x + PositionField::COVER_YD));
            int32 const by0 = PositionField::BlockOf(PositionField::CellOf(p.y - PositionField::COVER_YD));
            int32 const by1 = PositionField::BlockOf(PositionField::CellOf(p.y + PositionField::COVER_YD));
            for (int32 bx = bx0; bx <= bx1; ++bx)
                for (int32 by = by0; by <= by1; ++by)
                {
                    float const d = BlockDistance(p, bx, by);
                    if (d > PositionField::COVER_YD)
                        continue;
                    auto const it = nearest.find(Key(bx, by));
                    if (it == nearest.end() || d < it->second)
                        nearest[Key(bx, by)] = d;
                }
        }
        std::vector<std::pair<float, uint64>> order;
        for (auto const& n : nearest)
            order.emplace_back(n.second, n.first);
        std::sort(order.begin(), order.end());
        if (order.size() > MAX_BLOCKS)
            order.resize(MAX_BLOCKS);
        for (auto const& o : order)
        {
            PositionField::Block b;
            b.bx = int32(uint32(o.second >> 32));
            b.by = int32(uint32(o.second & 0xFFFFFFFF));
            field.m_blockIndex[o.second] = uint32(field.m_blocks.size());
            field.m_blocks.push_back(b);
            m_ground[o.second].usedMs = nowMs;
        }

        // 2-4.
        FloodGround(map, field, seeds, nowMs);
        Position3 lo = seeds[0], hi = seeds[0];
        for (Position3 const& p : seeds)
        {
            lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
            hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
        }
        Position3 center;
        center.x = (lo.x + hi.x) * 0.5f;
        center.y = (lo.y + hi.y) * 0.5f;
        center.z = seeds[0].z;
        float const radius = 0.5f * std::sqrt((hi.x - lo.x) * (hi.x - lo.x) + (hi.y - lo.y) * (hi.y - lo.y)) +
                             PositionField::COVER_YD;
        BuildIdle(map, world, field, center, radius, nowMs);
        BuildPatrols(world, field);
        Forget(nowMs);

        FieldStats& s = field.stats;
        s.blocks = uint32(field.m_blocks.size());
        field.ForEachCell([&s](int32, int32, FieldCell const& c)
        {
            if (!c.standable)
                return;
            ++s.standable;
            if (c.idle)
                ++s.idleCells;
            if (c.patrolSec >= 0.0f)
                ++s.patrolCells;
        });
        s.buildMicros = uint32(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count());
        m_maxBuildMicros = std::max(m_maxBuildMicros, s.buildMicros);
        s.maxBuildMicros = m_maxBuildMicros;

        if (Trace::On())
            Trace::Emit("field", Trace::Format("%u blocks, %u standable, %u idle, %u patrol, %u us",
                    s.blocks, s.standable, s.idleCells, s.patrolCells, s.buildMicros),
                Trace::Format("{\"blocks\":%u,\"standable\":%u,\"idleCells\":%u,\"patrolCells\":%u,\"newCells\":%u,"
                              "\"navQueries\":%u,\"sightChecks\":%u,\"idle\":%u,\"micros\":%u,\"navmesh\":%s,\"budgetHit\":%s}",
                    s.blocks, s.standable, s.idleCells, s.patrolCells, s.newCells, s.navQueries, s.sightChecks,
                    uint32(field.m_idle.size()), s.buildMicros, s.navmesh ? "true" : "false", s.budgetHit ? "true" : "false"));

        world.field = m_field;
    }

    void PositionFieldBuilder::FloodGround(Map* map, PositionField& field, std::vector<Position3> const& seeds, uint32 /*nowMs*/)
    {
        MMAP::MMapManager* mmap = MMAP::MMapFactory::createOrGetMMapManager();
        dtNavMeshQuery const* query = mmap->GetNavMeshQuery(map->GetId());
        field.stats.navmesh = query != nullptr;
        dtQueryFilter filter;
        filter.setIncludeFlags(NAV_GROUND | NAV_WATER);
        filter.setExcludeFlags(NAV_STEEP_SLOPES);   // players do not walk up them
        uint32& queries = field.stats.navQueries;

        // Navmesh polygon and point of a cell, found again in every build (polygon references
        // are not kept: tiles may be reloaded).
        struct NavPoint
        {
            dtPolyRef ref = 0;
            float pt[3] = { 0.0f, 0.0f, 0.0f };     // Detour order: y, z, x
        };
        std::unordered_map<uint64, NavPoint> nav;

        float const half = PositionField::CELL_YD * 0.5f;
        auto const findGround = [&](int32 cx, int32 cy, float refZ, float& z, NavPoint* out) -> bool
        {
            Position3 const c = field.CenterOf(cx, cy);
            ++queries;
            if (query)
            {
                NavPoint np;
                float const center[3] = { c.y, refZ, c.x };
                float const extents[3] = { half, SEARCH_HALF_HEIGHT, half };
                if (dtStatusFailed(query->findNearestPoly(center, extents, &filter, &np.ref, np.pt)) || !np.ref)
                    return false;
                z = np.pt[1];
                if (out)
                    *out = np;
                return true;
            }
            float const h = map->GetHeight(c.x, c.y, refZ + SEARCH_HALF_HEIGHT, true, 2.0f * SEARCH_HALF_HEIGHT);
            if (h <= INVALID_HEIGHT || std::fabs(h - refZ) > SEARCH_HALF_HEIGHT)
                return false;
            z = h;
            return true;
        };
        // By value: a later insert may rehash `nav`.
        auto const navOf = [&](int32 cx, int32 cy, float z, NavPoint& out) -> bool
        {
            auto const it = nav.find(Key(cx, cy));
            if (it != nav.end())
                out = it->second;
            else
            {
                out = NavPoint();
                float found = 0.0f;
                findGround(cx, cy, z, found, &out);
                nav[Key(cx, cy)] = out;
            }
            return out.ref != 0;
        };
        auto const reset = [&](int32 cx, int32 cy, GroundCell& g)
        {
            for (uint8 d = 0; d < FIELD_DIRS; ++d)
            {
                if (!(g.linksKnown & (1 << d)))
                    continue;
                int32 dx, dy;
                PositionField::Step(d, dx, dy);
                if (GroundCell* n = Ground(cx + dx, cy + dy, false))
                {
                    n->linksKnown &= ~(1 << Opposite(d));
                    n->links &= ~(1 << Opposite(d));
                }
            }
            g = GroundCell();
            nav.erase(Key(cx, cy));
        };
        auto const linked = [&](int32 ax, int32 ay, GroundCell const& a, int32 nx, int32 ny, GroundCell const& n, uint8 d) -> bool
        {
            float const step = (d & 1) ? MAX_STEP_YD * 1.41421356f : MAX_STEP_YD;
            if (std::fabs(n.z - a.z) > step)
                return false;
            if (!query)
                return true;
            NavPoint from, to;
            if (!navOf(ax, ay, a.z, from) || !navOf(nx, ny, n.z, to))
                return false;
            ++queries;
            float t = 0.0f;
            float normal[3];
            dtPolyRef path[16];
            int count = 0;
            if (dtStatusFailed(query->raycast(from.ref, from.pt, to.pt, &filter, &t, normal, path, &count, 16)) ||
                t != FLT_MAX || count <= 0)
                return false;   // a wall, or off the mesh on the way
            if (path[count - 1] == to.ref)
                return true;
            // Reached the end on another polygon of the same surface.
            float h = 0.0f;
            return dtStatusSucceed(query->getPolyHeight(path[count - 1], to.pt, &h)) &&
                   std::fabs(h - to.pt[1]) <= LINK_HEIGHT_TOLERANCE;
        };

        std::vector<uint8> reached(field.m_blocks.size() * CELLS, 0);
        std::deque<std::pair<int32, int32>> open;
        for (Position3 const& p : seeds)
        {
            int32 const cx = PositionField::CellOf(p.x), cy = PositionField::CellOf(p.y);
            int32 const i = field.FlatIndex(cx, cy);
            if (i < 0 || reached[i])
                continue;
            GroundCell* g = Ground(cx, cy, true);
            if (g->state != GROUND_YES || std::fabs(g->z - p.z) > SEARCH_HALF_HEIGHT)
            {
                // Not known, or kept from another floor: find it from where the participant stands.
                reset(cx, cy, *g);
                float z = 0.0f;
                NavPoint np;
                g->state = findGround(cx, cy, p.z, z, &np) ? GROUND_YES : GROUND_NO;
                g->z = z;
                if (np.ref)
                    nav[Key(cx, cy)] = np;
                ++field.stats.newCells;
            }
            if (g->state != GROUND_YES)
                continue;
            reached[i] = 1;
            open.emplace_back(cx, cy);
        }

        while (!open.empty())
        {
            int32 const ax = open.front().first, ay = open.front().second;
            open.pop_front();
            GroundCell* a = Ground(ax, ay, false);
            for (uint8 d = 0; d < FIELD_DIRS; ++d)
            {
                int32 dx, dy;
                PositionField::Step(d, dx, dy);
                int32 const nx = ax + dx, ny = ay + dy;
                int32 const ni = field.FlatIndex(nx, ny);
                if (ni < 0)
                    continue;
                GroundCell* n = Ground(nx, ny, true);
                if (n->state == GROUND_UNKNOWN)
                {
                    if (queries >= MAX_NAV_QUERIES)
                    {
                        field.stats.budgetHit = true;
                        continue;
                    }
                    float z = 0.0f;
                    NavPoint np;
                    n->state = findGround(nx, ny, a->z, z, &np) ? GROUND_YES : GROUND_NO;
                    n->z = z;
                    if (np.ref)
                        nav[Key(nx, ny)] = np;
                    ++field.stats.newCells;
                }
                if (n->state != GROUND_YES)
                    continue;
                if (!(a->linksKnown & (1 << d)))
                {
                    if (queries >= MAX_NAV_QUERIES)
                    {
                        field.stats.budgetHit = true;
                        continue;
                    }
                    bool const ok = linked(ax, ay, *a, nx, ny, *n, d);
                    a->linksKnown |= 1 << d;
                    n->linksKnown |= 1 << Opposite(d);
                    if (ok)
                    {
                        a->links |= 1 << d;
                        n->links |= 1 << Opposite(d);
                    }
                }
                if ((a->links & (1 << d)) && !reached[ni])
                {
                    reached[ni] = 1;
                    open.emplace_back(nx, ny);
                }
            }
        }

        for (size_t b = 0; b < field.m_blocks.size(); ++b)
        {
            PositionField::Block& block = field.m_blocks[b];
            GroundBlock const& g = m_ground[Key(block.bx, block.by)];
            for (int32 i = 0; i < CELLS; ++i)
            {
                FieldCell& c = block.cells[i];
                c.z = g.cells[i].z;
                c.links = g.cells[i].links;
                c.standable = reached[b * CELLS + i] != 0;
            }
        }
    }

    void PositionFieldBuilder::BuildIdle(Map* map, WorldModel const& world, PositionField& field,
                                         Position3 const& center, float radius, uint32 nowMs)
    {
        std::vector<Player*> members;
        for (PartyMemberFacts const& m : world.party)
            if (!m.isPet && m.alive)
                if (Player* p = map->GetPlayer(m.guid))
                    members.push_back(p);
        if (members.empty())
            return;

        float const scan = std::min(radius + AggroRange::MAX_ATTACK_DISTANCE + 20.0f, 200.0f);
        std::list<Creature*> found;
        IdleCheck check(members, center, scan);
        MaNGOS::CreatureListSearcher<IdleCheck> searcher(found, check);
        Cell::VisitGridObjects(center.x, center.y, map, searcher, scan);

        struct Candidate
        {
            Creature* creature = nullptr;
            FieldIdle idle;
            float distance = 0.0f;
        };
        std::vector<Candidate> candidates;
        for (Creature* c : found)
        {
            Candidate k;
            k.creature = c;
            for (Player* m : members)
                if (AggroRange::IsIdleAggressor(c, m))
                    k.idle.reach = std::max(k.idle.reach, AggroRange::Reach(c, m, SafeRoute::IDLE_MARGIN));
            if (k.idle.reach <= 0.0f)
                continue;
            AggroRange::Wander const w = AggroRange::WanderOf(c);
            k.idle.guid = c->GetObjectGuid();
            k.idle.home = w.home;
            k.idle.wander = w.radius;
            k.distance = SafeRoute::Distance2d(w.home, center);
            candidates.push_back(k);
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](Candidate const& a, Candidate const& b) { return a.distance < b.distance; });

        uint32 checks = 0;
        for (Candidate& k : candidates)
        {
            if (field.m_idle.size() >= PositionField::MAX_IDLE)
                break;
            Creature* c = k.creature;
            float const range = k.idle.reach + k.idle.wander;
            // Moving along a path (not a wanderer): where it stands now, distance only.
            bool const moving = c->IsMoving() && k.idle.wander <= 0.0f;
            Position3 from = k.idle.home;
            if (moving)
            {
                from.x = c->GetPositionX();
                from.y = c->GetPositionY();
                from.z = c->GetPositionZ();
            }

            IdleCover* cover = nullptr;
            if (!moving)
            {
                for (IdleCover& ic : m_idleCovers)
                    if (ic.guid == k.idle.guid)
                        cover = &ic;
                if (!cover)
                {
                    m_idleCovers.emplace_back();
                    cover = &m_idleCovers.back();
                    cover->guid = k.idle.guid;
                }
                if (SafeRoute::Distance2d(cover->home, k.idle.home) > IDLE_MOVED_YD ||
                    std::fabs(cover->home.z - k.idle.home.z) > IDLE_MOVED_YD || std::fabs(cover->reach - k.idle.reach) > 0.1f)
                    cover->covered.clear();
                cover->home = k.idle.home;
                cover->reach = k.idle.reach;
                cover->usedMs = nowMs;
            }

            uint32 const bit = uint32(field.m_idle.size());
            bool any = false;
            bool allSight = !moving;
            int32 const bx0 = PositionField::BlockOf(PositionField::CellOf(from.x - range));
            int32 const bx1 = PositionField::BlockOf(PositionField::CellOf(from.x + range));
            int32 const by0 = PositionField::BlockOf(PositionField::CellOf(from.y - range));
            int32 const by1 = PositionField::BlockOf(PositionField::CellOf(from.y + range));
            for (int32 bx = bx0; bx <= bx1; ++bx)
            {
                for (int32 by = by0; by <= by1; ++by)
                {
                    auto const bi = field.m_blockIndex.find(Key(bx, by));
                    if (bi == field.m_blockIndex.end() || BlockDistance(from, bx, by) > range)
                        continue;
                    PositionField::Block& block = field.m_blocks[bi->second];
                    GroundBlock const& ground = m_ground[bi->first];

                    uint64 bits = 0;
                    bool kept = false;
                    if (cover)
                    {
                        auto const ci = cover->covered.find(bi->first);
                        if (ci != cover->covered.end())
                        {
                            bits = ci->second;
                            kept = true;
                        }
                    }
                    if (!kept)
                    {
                        // Kept only when every cell's ground is known and every cell got its sight check.
                        bool complete = !moving;
                        for (int32 i = 0; i < CELLS; ++i)
                        {
                            GroundCell const& g = ground.cells[i];
                            if (g.state == GROUND_UNKNOWN)
                            {
                                complete = false;
                                continue;
                            }
                            if (g.state != GROUND_YES)
                                continue;
                            Position3 at = field.CenterOf(bx * PositionField::BLOCK_CELLS + i % PositionField::BLOCK_CELLS,
                                                          by * PositionField::BLOCK_CELLS + i / PositionField::BLOCK_CELLS);
                            at.z = g.z;
                            if (!CoversByDistance(from, range, c->CanFly(), at))
                                continue;
                            bool covers = true;
                            if (!moving && checks < MAX_SIGHT_CHECKS)
                            {
                                ++checks;
                                covers = AggroRange::CoversWandering(c, k.idle.reach, at);
                            }
                            else if (!moving)
                            {
                                complete = false;
                                allSight = false;
                                field.stats.budgetHit = true;
                            }
                            if (covers)
                                bits |= uint64(1) << i;
                        }
                        if (cover && complete)
                            cover->covered[bi->first] = bits;
                    }
                    for (int32 i = 0; i < CELLS; ++i)
                    {
                        if (!(bits & (uint64(1) << i)) || !block.cells[i].standable)
                            continue;
                        block.cells[i].idle |= uint64(1) << bit;
                        any = true;
                    }
                }
            }
            if (!any)
                continue;   // covers no standable cell: the next creature takes this bit
            k.idle.sightChecked = allSight;
            field.m_idle.push_back(k.idle);
        }
        field.stats.sightChecks = checks;
    }

    bool PositionFieldBuilder::ThreatNear(WorldModel const& world)
    {
        auto const nearMember = [&world](Position3 const& at, float reach)
        {
            float const range = PositionField::COVER_YD + reach + SafeRoute::IDLE_MARGIN;
            for (PartyMemberFacts const& m : world.party)
                if (!m.isPet && m.alive && SafeRoute::Distance2d(m.pos, at) <= range)
                    return true;
            return false;
        };
        for (PackFacts const& pack : world.packs)
            for (PackCreature const& c : pack.members)
                if (c.aggressive && !c.noAggro && !c.tappedByOther && nearMember(c.pos, c.reach))
                    return true;
        for (PatrolFacts const& p : world.patrols)
        {
            if (!p.aggressive || p.noAggro || p.tappedByOther)
                continue;
            if (nearMember(p.pos, p.reach))
                return true;
            for (Position3 const& at : p.track)
                if (nearMember(at, p.reach))
                    return true;
        }
        return false;
    }

    void PositionFieldBuilder::BuildPatrols(WorldModel const& world, PositionField& field)
    {
        if (field.m_blocks.empty())
            return;
        // The field's bounds: track points farther off than a patrol's zone mark nothing (a
        // lap is up to PATROL_TRACK_MAX_SEC points, most of them far from us).
        int32 bx0 = field.m_blocks[0].bx, bx1 = bx0, by0 = field.m_blocks[0].by, by1 = by0;
        for (PositionField::Block const& b : field.m_blocks)
        {
            bx0 = std::min(bx0, b.bx); bx1 = std::max(bx1, b.bx);
            by0 = std::min(by0, b.by); by1 = std::max(by1, b.by);
        }
        float const minX = bx0 * PositionField::BLOCK_YD, maxX = (bx1 + 1) * PositionField::BLOCK_YD;
        float const minY = by0 * PositionField::BLOCK_YD, maxY = (by1 + 1) * PositionField::BLOCK_YD;
        // The rule of PatrolZone.h, stamped cell by cell (the cell's value = PatrolZone::ArriveSec
        // at its centre). The tracks start at the pack refresh (WorldModel::packsRefreshedAtMs,
        // every PACK_REFRESH_MS), the field is rebuilt more often: times from this refresh.
        float const ageSec = PatrolZone::TrackAgeSec(world);

        for (PatrolFacts const& p : world.patrols)
        {
            if (!PatrolZone::Counts(p))
                continue;
            float const radius = PatrolZone::Radius(p);
            for (size_t t = 0; t < p.track.size(); ++t)
            {
                Position3 const& at = p.track[t];
                if (at.x + radius < minX || at.x - radius > maxX || at.y + radius < minY || at.y - radius > maxY)
                    continue;
                float const sec = PatrolZone::SecOf(p, t, ageSec);
                int32 const cx0 = PositionField::CellOf(at.x - radius), cx1 = PositionField::CellOf(at.x + radius);
                int32 const cy0 = PositionField::CellOf(at.y - radius), cy1 = PositionField::CellOf(at.y + radius);
                for (int32 cx = cx0; cx <= cx1; ++cx)
                    for (int32 cy = cy0; cy <= cy1; ++cy)
                    {
                        FieldCell* c = field.MutableAt(cx, cy);
                        if (!c || !c->standable || (c->patrolSec >= 0.0f && c->patrolSec <= sec))
                            continue;
                        if (PackResolver::Distance(at, field.CenterOf(cx, cy)) <= radius)
                            c->patrolSec = sec;
                    }
            }
        }
    }

    void PositionFieldBuilder::Forget(uint32 nowMs)
    {
        for (auto it = m_ground.begin(); it != m_ground.end();)
        {
            if (WorldTimer::getMSTimeDiff(it->second.usedMs, nowMs) > GROUND_KEEP_MS)
                it = m_ground.erase(it);
            else
                ++it;
        }
        if (m_ground.size() > MAX_KEPT_BLOCKS)
        {
            for (auto it = m_ground.begin(); it != m_ground.end();)
                it = it->second.usedMs != nowMs ? m_ground.erase(it) : std::next(it);
        }
        m_idleCovers.erase(std::remove_if(m_idleCovers.begin(), m_idleCovers.end(),
            [nowMs](IdleCover const& c) { return c.usedMs != nowMs; }), m_idleCovers.end());
    }
}
