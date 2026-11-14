#include "Commander/Perception/Spatial/MeleeRoutes.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Trace/Trace.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Timer.h"
#include <algorithm>
#include <memory>

namespace Commander
{
    namespace
    {
        struct Pair
        {
            PartyMemberFacts const* bot = nullptr;
            EnemyFacts const* enemy = nullptr;
            float distance = 0.0f;
            int32 cached = -1;          // index in the cache, -1 = never checked
            bool due = false;
            bool engaged = false;       // fighting the party at its spot (header: ArrivingAt)
        };

        bool WalksToMelee(PartyMemberFacts const& m)
        {
            return m.isBot && !m.isPet && m.alive && (m.role == ROLE_TANK || m.role == ROLE_MELEE_DPS);
        }

        // The enemy and its pack mates (they come with it).
        std::vector<ObjectGuid> PackOf(WorldModel const& world, ObjectGuid enemy)
        {
            std::vector<ObjectGuid> out{ enemy };
            for (PackFacts const& pack : world.packs)
            {
                bool const holds = std::any_of(pack.members.begin(), pack.members.end(),
                    [enemy](PackCreature const& c) { return c.guid == enemy; });
                if (!holds)
                    continue;
                for (PackCreature const& c : pack.members)
                    if (c.guid != enemy)
                        out.push_back(c.guid);
                break;
            }
            return out;
        }
    }

    void MeleeRoutes::Annotate(Map* map, WorldModel& world, uint32 nowMs)
    {
        world.routes.clear();
        if (world.enemies.empty() && world.targeted.empty())
        {
            m_cache.clear();
            return;
        }

        std::vector<EnemyFacts const*> enemies;
        for (EnemyFacts const& e : world.enemies)
            enemies.push_back(&e);
        size_t const fighting = enemies.size();     // the rest are only targeted (not fighting yet)
        for (EnemyFacts const& e : world.targeted)
            enemies.push_back(&e);

        std::vector<Pair> pairs;
        for (PartyMemberFacts const& m : world.party)
        {
            if (!WalksToMelee(m))
                continue;
            for (size_t k = 0; k < enemies.size(); ++k)
            {
                EnemyFacts const* e = enemies[k];
                float const distance = PackResolver::Distance(m.pos, e->pos);
                if (distance > RANGE_YD || distance - e->boundingRadius <= REACH_YD)
                    continue;
                Pair p;
                p.bot = &m;
                p.enemy = e;
                p.distance = distance;
                p.engaged = k < fighting && e->acting && !e->victim.IsEmpty();
                for (size_t i = 0; i < m_cache.size(); ++i)
                    if (m_cache[i].fact.bot == m.guid && m_cache[i].fact.enemy == e->guid)
                        p.cached = int32(i);
                if (p.cached < 0)
                    p.due = true;
                else
                {
                    Entry const& c = m_cache[p.cached];
                    p.due = WorldTimer::getMSTimeDiff(c.atMs, nowMs) >= MAX_AGE_MS ||
                            PackResolver::Distance(c.from, m.pos) >= MOVED_YD ||
                            PackResolver::Distance(c.to, e->pos) >= MOVED_YD;
                }
                pairs.push_back(p);
            }
        }

        // Drop what is no longer a pair (dead enemy, bot in reach, fight over).
        std::vector<Entry> kept;
        for (Pair& p : pairs)
        {
            if (p.cached < 0)
                continue;
            kept.push_back(m_cache[p.cached]);
            p.cached = int32(kept.size() - 1);
        }
        m_cache.swap(kept);

        // Never checked first, then the nearest.
        std::vector<Pair*> due;
        for (Pair& p : pairs)
            if (p.due)
                due.push_back(&p);
        std::stable_sort(due.begin(), due.end(), [](Pair const* a, Pair const* b)
        {
            if ((a->cached < 0) != (b->cached < 0))
                return a->cached < 0;
            return a->distance < b->distance;
        });
        if (due.size() > MAX_CHECKS_PER_REFRESH)
            due.resize(MAX_CHECKS_PER_REFRESH);

        // One idle creature scan per bot and margin, out to its farthest walk checked now.
        struct Scan
        {
            ObjectGuid bot;
            float margin = 0.0f;
            std::unique_ptr<SafeRoute::IdleCreatures> idle;
        };
        std::vector<Scan> scans;
        auto const scanOf = [&](Player* me, Pair const* p, float margin) -> SafeRoute::IdleCreatures const&
        {
            for (Scan const& s : scans)
                if (s.bot == p->bot->guid && s.margin == margin)
                    return *s.idle;
            float farthest = 0.0f;
            for (Pair const* q : due)
                if (q->bot == p->bot)
                    farthest = std::max(farthest, q->distance);
            float const radius = std::min(MAX_SCAN_YD, farthest + SCAN_EXTRA_YD);
            Scan s;
            s.bot = p->bot->guid;
            s.margin = margin;
            s.idle.reset(new SafeRoute::IdleCreatures(me, p->bot->pos, radius, margin));
            scans.push_back(std::move(s));
            return *scans.back().idle;
        };
        // The walk's idle set for one margin (header: LeavingFrom, the pack, ArrivingAt).
        auto const pathIdleOf = [&](Player* me, Pair const* p, float margin)
        {
            SafeRoute::IdleCreatures pathIdle = scanOf(me, p, margin).LeavingFrom(p->bot->pos);
            pathIdle.Ignore(PackOf(world, p->enemy->guid));
            // The party fights it there and nothing else came (header).
            if (p->engaged)
                pathIdle = pathIdle.ArrivingAt(p->enemy->pos);
            return pathIdle;
        };
        for (Pair* p : due)
        {
            Player* me = map->GetPlayer(p->bot->guid);
            if (!me || !me->IsInWorld() || me->GetMap() != map)
                continue;

            Entry e;
            e.fact.bot = p->bot->guid;
            e.fact.enemy = p->enemy->guid;
            e.fact.pathYd = p->distance;
            SafeRoute::Verdict verdict;
            e.fact.safe = SafeRoute::PathIsSafe(me, pathIdleOf(me, p, SafeRoute::IDLE_MARGIN), p->enemy->pos, MAX_DETOUR,
                                                &e.fact.pathYd, nullptr, &verdict);
            e.fact.safeTight = e.fact.safe;
            e.fact.why = verdict.why;
            if (verdict.blocker)
                e.fact.blocker = verdict.blocker->GetObjectGuid();
            // Only an idle creature's margin can differ with the hard line (no path, a detour do not).
            if (!e.fact.safe && verdict.blocker)
                e.fact.safeTight = SafeRoute::PathIsSafe(me, pathIdleOf(me, p, SafeRoute::TIGHT_MARGIN), p->enemy->pos,
                                                         MAX_DETOUR);
            if (Trace::On())
                Trace::Emit("route", Trace::Format("%s -> %s: %s%s%s%s", me->GetName(), Trace::Guid(e.fact.enemy).c_str(),
                        e.fact.safe ? "safe" : verdict.why,
                        verdict.blocker ? " " : "", verdict.blocker ? verdict.blocker->GetName() : "",
                        !e.fact.safe && e.fact.safeTight ? " (safe on the hard line)" : ""),
                    Trace::Format("{\"safe\":%s,\"safeTight\":%s,\"why\":\"%s\",\"pathYd\":%.1f,\"straightYd\":%.1f,\"engaged\":%s,\"blocker\":\"%s\"}",
                        e.fact.safe ? "true" : "false", e.fact.safeTight ? "true" : "false", verdict.why, e.fact.pathYd,
                        p->distance, p->engaged ? "true" : "false", Trace::Guid(e.fact.blocker).c_str()));
            e.from = p->bot->pos;
            e.to = p->enemy->pos;
            e.atMs = nowMs;
            if (p->cached >= 0)
                m_cache[p->cached] = e;
            else
            {
                m_cache.push_back(e);
                p->cached = int32(m_cache.size() - 1);
            }
        }

        for (Pair const& p : pairs)
            if (p.cached >= 0)
                world.routes.push_back(m_cache[p.cached].fact);
    }
}
