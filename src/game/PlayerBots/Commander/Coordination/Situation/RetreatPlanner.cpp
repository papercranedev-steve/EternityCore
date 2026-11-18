#include "Commander/Coordination/Situation/RetreatPlanner.h"
#include "Commander/Team.h"
#include "Commander/Coordination/Situation/ClearPlanner.h"
#include "Commander/PartyCommander.h"
#include "Commander/BotDirectives.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Forecast/HealCapacity.h"
#include "Commander/Perception/Forecast/ManaBudget.h"
#include "Commander/Perception/Spatial/TravelTime.h"
#include "Commander/Perception/Spatial/WayIn.h"
#include "TeamBotAI.h"
#include "Creature.h"
#include "Group.h"
#include "Map.h"
#include "PathFinder.h"
#include "Player.h"
#include "Timer.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <list>

namespace Commander
{
    namespace
    {
        Position3 PositionOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }

        std::string Format(char const* fmt, ...)
        {
            char buf[256];
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(buf, sizeof(buf), fmt, ap);
            va_end(ap);
            return buf;
        }

        // Plain point distance. The server's IsWithinDist3d also subtracts the bounding radius,
        // which SAFETY_MARGIN covers.
        float Dist3(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        float Dist2(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        // WoW map axes: +x is north, +y is west.
        char const* Compass(float dx, float dy)
        {
            static char const* const names[8] = { "north", "north-west", "west", "south-west",
                                                  "south", "south-east", "east", "north-east" };
            float angle = std::atan2(dy, dx);
            if (angle < 0.0f)
                angle += 2.0f * M_PI_F;
            int const index = int(std::floor(angle / (M_PI_F / 4.0f) + 0.5f)) % 8;
            return names[index];
        }

        std::string NameIn(RetreatPlan const& r, WorldModel const& world, ObjectGuid guid)
        {
            if (guid == world.leader)
                return "you";
            for (PullMemberName const& n : r.names)
                if (n.guid == guid)
                    return n.name;
            return "a party member";
        }

        // Threat area of one enemy (Creature::IsOutOfThreatArea).
        struct ThreatArea
        {
            Position3 center;   // combat start position
            float radius = 0.0f;
        };

        // Hostile creatures that are not fighting yet: a rally point next to them starts a new fight.
        // Wider than world.packs (80 yd around the leader): candidate points lie up to 100 yd away.
        class IdleHostileCheck
        {
        public:
            IdleHostileCheck(Player const* leader, float range) : m_leader(leader), m_range(range) {}
            bool operator()(Creature* c)
            {
                return c->IsAlive() && !c->IsInCombat() && !c->GetOwnerGuid() &&
                    c->GetCreatureType() != CREATURE_TYPE_CRITTER &&
                    !c->HasExtraFlag(CREATURE_FLAG_EXTRA_NO_AGGRO) &&
                    c->IsHostileTo(m_leader) && m_leader->IsWithinDist(c, m_range, false);
            }

        private:
            Player const* m_leader;
            float m_range;
        };

        struct IdleCreature
        {
            Position3 pos;
            float clearance = 0.0f;     // max(IDLE_CREATURE_CLEARANCE, its aggro range on the leader)
        };

        std::vector<Player*> BotsOf(Player* leader)
        {
            std::vector<Player*> bots;
            for (Player* m : Team::Members(leader))
            {
                if (!m || !m->IsInWorld() || m->GetMap() != leader->GetMap() || !m->AI())
                    continue;
                if (dynamic_cast<TeamBotAI*>(m->AI()))
                    bots.push_back(m);
            }
            return bots;
        }

        // Cheap point checks (no pathfinding). Sets point.z to the ground. Returns the reason it
        // failed, nullptr = worth a path check.
        char const* CheckPointCheap(Player* leader, Position3& point, std::vector<ThreatArea> const& areas,
                                    std::vector<IdleCreature> const& idle)
        {
            Map* map = leader->GetMap();

            // Ground must exist (not over a cliff or the void) near the leader's height.
            float const ground = map->GetHeight(point.x, point.y, leader->GetPositionZ() + RetreatPlanner::MAX_HEIGHT_DIFFERENCE,
                true, 2.0f * RetreatPlanner::MAX_HEIGHT_DIFFERENCE);
            if (ground <= INVALID_HEIGHT || std::fabs(ground - leader->GetPositionZ()) > RetreatPlanner::MAX_HEIGHT_DIFFERENCE)
                return "no ground";
            point.z = ground;

            // Out of every enemy's threat area, plus margin: creature and victim both outside is
            // what makes it give up (scenario 11, 1.5). Measured from the combat start, not from
            // the fight spot: a patrol that walked in started its fight somewhere else.
            for (ThreatArea const& a : areas)
                if (Dist3(point, a.center) <= a.radius + RetreatPlanner::SAFETY_MARGIN)
                    return "threat area";

            for (IdleCreature const& c : idle)
                if (Dist3(point, c.pos) <= c.clearance)
                    return "idle creatures";
            return nullptr;
        }

        // Expensive check: reachable on foot. Returns the reason it failed, nullptr = usable.
        char const* CheckPath(Player* leader, Position3 const& point)
        {
            Map* map = leader->GetMap();

            // Reachable on foot. Without mmaps (NOT_USING_PATH) fall back to line of sight.
            PathInfo path(leader);
            path.calculate(point.x, point.y, point.z);
            uint32 const type = path.getPathType();
            if (type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE))
                return "no path";
            if (type & PATHFIND_NOT_USING_PATH)
            {
                if (!map->isInLineOfSight(leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ() + 2.0f,
                                          point.x, point.y, point.z + 2.0f))
                    return "no path";
            }
            else
            {
                float const straight = Dist3(PositionOf(leader), point);
                if (path.Length() > straight * RetreatPlanner::MAX_PATH_DETOUR + RetreatPlanner::SEARCH_STEP)
                    return "detour";
            }
            return nullptr;
        }
    }

    namespace RetreatPlanner
    {
        bool Start(Player* leader, PartyCommander& commander, std::vector<std::string>& report)
        {
            Map* map = leader->GetMap();
            WorldModel const& world = commander.GetWorld();

            // IsOutOfThreatArea returns false in dungeons: running only drags the pack along
            // (combat scenario 12, T+18s). The door is the way out there.
            if (map->IsDungeon())
                return StartDoor(map, commander, report);
            if (!world.HasPartyEnemy())
            {
                report.push_back("Your party is not fighting anything.");
                return false;
            }

            // Living party members (pets excluded): an enemy's area radius depends on its victim.
            std::vector<Unit*> members;
            for (PartyMemberFacts const& m : world.party)
                if (!m.isPet && m.alive)
                    if (Unit* u = map->GetUnit(m.guid))
                        members.push_back(u);

            std::vector<ThreatArea> areas;
            Position3 startCentroid;
            for (EnemyFacts const& e : world.enemies)
            {
                Creature* c = map->GetCreature(e.guid);
                if (!c || !c->IsAlive())
                    continue;
                if (c->HasExtraFlag(CREATURE_FLAG_EXTRA_NO_LEASH_EVADE))
                {
                    report.push_back(Format("No safe retreat: %s never gives up the chase.", c->GetName()));
                    return false;
                }
                ThreatArea a;
                c->GetCombatStartPosition(a.center.x, a.center.y, a.center.z);
                if (a.center.x == 0.0f && a.center.y == 0.0f && a.center.z == 0.0f)
                    a.center = PositionOf(c);   // not set (should not happen in combat)
                a.radius = THREAT_RADIUS;
                for (Unit* u : members)
                    a.radius = std::max(a.radius, c->GetAttackDistance(u) * ATTACK_DISTANCE_FACTOR);
                startCentroid.x += a.center.x;
                startCentroid.y += a.center.y;
                startCentroid.z += a.center.z;
                areas.push_back(a);
            }
            if (areas.empty())
            {
                report.push_back("Your party is not fighting anything.");
                return false;
            }
            startCentroid.x /= areas.size();
            startCentroid.y /= areas.size();
            startCentroid.z /= areas.size();

            std::vector<IdleCreature> idle;
            {
                std::list<Creature*> found;
                IdleHostileCheck check(leader, SEARCH_MAX_DISTANCE + IDLE_CREATURE_CLEARANCE);
                MaNGOS::CreatureListSearcher<IdleHostileCheck> searcher(found, check);
                Cell::VisitGridObjects(leader, searcher, SEARCH_MAX_DISTANCE + IDLE_CREATURE_CLEARANCE);
                for (Creature* c : found)
                    idle.push_back({ PositionOf(c), std::max(IDLE_CREATURE_CLEARANCE, c->GetAttackDistance(leader)) });
            }

            // Away from the enemies, i.e. roughly back the way the party came (no recorded path yet).
            Position3 const from = PositionOf(leader);
            float baseAngle;
            if (Dist2(from, startCentroid) >= 1.0f)
                baseAngle = std::atan2(from.y - startCentroid.y, from.x - startCentroid.x);
            else
                baseAngle = leader->GetOrientation() + M_PI_F;  // standing on the start: away from where the player faces

            // Pass 1, cheap checks only (ground height, threat areas, idle hostiles) on every candidate.
            struct Candidate
            {
                Position3 pos;
                float score = 0.0f;     // lower is better
            };
            static float const offsetsDeg[] = { 0.0f, 25.0f, -25.0f, 50.0f, -50.0f };
            std::vector<Candidate> candidates;
            uint32 tried = 0;
            for (float d = 0.0f; d <= SEARCH_MAX_DISTANCE; d += SEARCH_STEP)
            {
                for (float deg : offsetsDeg)
                {
                    if (d == 0.0f && deg != 0.0f)
                        break;      // the leader's own position has no direction
                    float const angle = baseAngle + deg * M_PI_F / 180.0f;
                    Candidate c;
                    c.pos.x = from.x + std::cos(angle) * d;
                    c.pos.y = from.y + std::sin(angle) * d;
                    c.pos.z = from.z;
                    ++tried;
                    if (CheckPointCheap(leader, c.pos, areas, idle))
                        continue;
                    // Nearest usable point first: less running, less time being hit from behind.
                    // Stable sort keeps the smaller turn first at equal distance.
                    c.score = d;
                    candidates.push_back(c);
                }
            }
            std::stable_sort(candidates.begin(), candidates.end(),
                [](Candidate const& a, Candidate const& b) { return a.score < b.score; });

            // Pass 2, pathfinding (expensive, world thread): only the best MAX_PATH_CHECKS candidates.
            Position3 best;
            bool found = false;
            uint32 pathChecks = 0;
            uint32 const maxPathChecks = MAX_PATH_CHECKS;
            for (Candidate const& c : candidates)
            {
                if (pathChecks >= maxPathChecks)
                    break;
                ++pathChecks;
                if (!CheckPath(leader, c.pos))
                {
                    best = c.pos;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                if (candidates.empty())
                    report.push_back(Format("No safe retreat point within %.0f yd (%u points checked: each must be %.0f+ yd "
                        "from every enemy's combat start, on ground, away from idle hostiles). Not retreating.",
                        SEARCH_MAX_DISTANCE, tried, THREAT_RADIUS + SAFETY_MARGIN));
                else
                    report.push_back(Format("No reachable retreat point within %.0f yd (%u of %u points are out of every "
                        "threat area; the nearest %u have no walkable path without a long detour). Not retreating.",
                        SEARCH_MAX_DISTANCE, uint32(candidates.size()), tried, pathChecks));
                return false;
            }

            TeamPlan& plan = commander.GetPlanMutable();
            RetreatPlan& r = plan.retreat;
            bool const keepAdvice = r.adviceSent;
            r = RetreatPlan();
            r.adviceSent = keepAdvice;
            r.active = true;
            r.point = best;
            r.startedMs = WorldTimer::getMSTime();
            r.enemiesAtStart = uint32(areas.size());
            for (Unit* u : members)
                r.names.push_back({ u->GetObjectGuid(), u->GetName() });

            // Pulling or clearing more packs while running away makes no sense, and the tank's
            // pull would hit a new enemy.
            if (plan.pull.phase != PullPhase::None)
            {
                plan.pull = PullPlan();
                plan.lastPullOutcome = PullOutcome::None;
                plan.notes.push_back("Pull cancelled: retreating.");
            }
            ClearPlanner::Stop(commander, "retreating.");   // sets clear.active = false and notes it

            // UpdateAI returns while a bot casts, before the arbiter runs: a 3 s cast would keep it
            // standing, and finishing it hits the enemy again. GM commands run on the world thread
            // while maps are idle, so touching the bots here is safe.
            for (Player* bot : BotsOf(leader))
            {
                if (!bot->IsAlive())
                    continue;
                bot->InterruptNonMeleeSpells(false);
                if (bot->GetVictim())
                    bot->AttackStop();
            }

            float const distance = Dist3(from, best);
            if (distance < 1.0f)
                report.push_back(Format("Retreating to where you stand: every enemy's combat start is %.0f+ yd away. "
                    "Stay here; bots stop attacking and come to you.", THREAT_RADIUS + SAFETY_MARGIN));
            else
                report.push_back(Format("Retreating %.0f yd %s of you (%.1f, %.1f, %.1f). Run there too: bots stop attacking, "
                    "call pets back and gather there.", distance, Compass(best.x - from.x, best.y - from.y),
                    best.x, best.y, best.z));
            report.push_back("Enemies give up once they and their target are 50+ yd from where they started and nobody hit "
                "or stunned them for 12 s. Keep moving: a chaser that catches you standing still for 5 s keeps chasing. "
                "'.partybot ai retreat stop' to fight again.");
            return true;
        }

        bool StartDoor(Map* map, PartyCommander& commander, std::vector<std::string>& report)
        {
            WorldModel const& world = commander.GetWorld();
            if (!world.HasPartyEnemy())
            {
                report.push_back("Your party is not fighting anything.");
                return false;
            }
            Way const way = WayIn::Copy(world.leader, map->GetId(), map->GetInstanceId());
            if (way.points.empty() || !way.fromEntrance)
            {
                report.push_back("No way back to the door is known (the way in was not recorded from the door).");
                return false;
            }
            auto nameOf = [map](ObjectGuid g)
            {
                Player const* p = map->GetPlayer(g);
                return std::string(p ? p->GetName() : "a party member");
            };

            float longest = 0.0f;
            for (PartyMemberFacts const& m : world.party)
            {
                if (m.isPet || !m.alive)
                    continue;
                WayBack const back = WayIn::Back(way, m.pos);
                if (!back.found)
                {
                    report.push_back(nameOf(m.guid) + " is not near the way we came in: no run for the door.");
                    return false;
                }
                if (back.oneWays)
                {
                    report.push_back(nameOf(m.guid) + " would have to climb back up a drop: no run for the door.");
                    return false;
                }
                if (back.lengthYd > MAX_DOOR_YD)
                {
                    report.push_back(Format("The door is %.0f yd back for %s: too far to outrun them.", back.lengthYd,
                        nameOf(m.guid).c_str()));
                    return false;
                }
                float dps = 0.0f;
                for (EnemyFacts const& e : world.enemies)
                    if (e.victim == m.guid && e.acting && !e.control.spellId)
                        dps += std::max(e.meleeDps, e.rangedDps);
                float const runSec = back.lengthYd / RUN_YD_PER_SEC;
                if (dps > 0.0f && float(m.health) / dps < runSec * RUN_MARGIN)
                {
                    report.push_back(Format("%s would not make it to the door (%.0f yd, about %.0f s, dies in about %.0f s).",
                        nameOf(m.guid).c_str(), back.lengthYd, runSec, float(m.health) / dps));
                    return false;
                }
                longest = std::max(longest, back.lengthYd);
            }

            TeamPlan& plan = commander.GetPlanMutable();
            RetreatPlan& r = plan.retreat;
            bool const keepAdvice = r.adviceSent;
            r = RetreatPlan();
            r.adviceSent = keepAdvice;
            r.active = true;
            r.toDoor = true;
            r.doorWay = way;
            r.point = way.points.front().pos;
            r.startedMs = WorldTimer::getMSTime();
            r.enemiesAtStart = uint32(world.enemies.size());
            for (PartyMemberFacts const& m : world.party)
                if (!m.isPet)
                    r.names.push_back({ m.guid, nameOf(m.guid) });
            if (plan.pull.phase != PullPhase::None)
            {
                plan.pull = PullPlan();
                plan.lastPullOutcome = PullOutcome::None;
                plan.notes.push_back("Pull cancelled: retreating.");
            }
            ClearPlanner::Stop(commander, "retreating.");

            // (see Start: a cast in progress would keep the bot standing)
            for (PartyMemberFacts const& m : world.party)
            {
                if (!m.isBot || m.isPet)
                    continue;
                Player* bot = map->GetPlayer(m.guid);
                if (!bot || !bot->IsAlive())
                    continue;
                bot->InterruptNonMeleeSpells(false);
                if (bot->GetVictim())
                    bot->AttackStop();
            }
            report.push_back(Format("Out through the door: up to %.0f yd back the way we came in. Bots stop attacking, "
                "run it and step out at the door. '.partybot ai retreat stop' to fight again.", longest));
            return true;
        }

        void Step(TeamBotAI* bot, PartyCommander& commander)
        {
            TeamPlan& plan = commander.GetPlanMutable();
            WorldModel const& world = commander.GetWorld();
            RetreatPlan& r = plan.retreat;
            Map* map = bot->me->GetMap();
            if (!world.HasPartyEnemy())
                r.declineNoted = false;

            if (r.requested)
            {
                r.requested = false;
                std::string const why = r.requestWhy;
                std::vector<std::string> report;
                bool ok = false;
                if (map->IsDungeon())
                    ok = StartDoor(map, commander, report);
                else if (Player* leader = map->GetPlayer(world.leader))
                    ok = Start(leader, commander, report);
                else
                    report.push_back("you are not here.");
                if (ok)
                {
                    r.automatic = true;
                    std::string text = "Running with you (" + why + ").";
                    for (std::string const& line : report)
                        text += " " + line;
                    plan.notes.push_back(text);
                }
                else
                {
                    r.declinedAtMs = world.refreshedAtMs;
                    if (!r.declineNoted)
                    {
                        r.declineNoted = true;
                        std::string text = "You run (" + why + "), but we cannot retreat:";
                        for (std::string const& line : report)
                            text += " " + line;
                        plan.notes.push_back(text + " Fighting on.");
                    }
                }
            }

            if (!r.active || !r.toDoor)
                return;
            // Bots at the door end step out (header DOOR_REACH_YD).
            WorldLocation outside;
            if (!TravelTime::OutsidePoint(WorldLocation(map->GetId(), r.point.x, r.point.y, r.point.z), outside))
                return;
            for (PartyMemberFacts const& m : world.party)
            {
                if (!m.isBot || m.isPet || !m.alive || Dist3(m.pos, r.point) > DOOR_REACH_YD)
                    continue;
                Player* p = map->GetPlayer(m.guid);
                if (p && p->IsAlive() && !p->IsBeingTeleported())
                    p->TeleportTo(outside);
            }
        }

        void Stop(Player* leader, PartyCommander& commander, std::string const& why)
        {
            RetreatPlan& r = commander.GetPlanMutable().retreat;
            if (!r.active)
                return;
            r.active = false;
            r.lastOutcome = RetreatOutcome::Stopped;
            commander.GetPlanMutable().notes.push_back("Retreat stopped: " + why);
            // The MoveTo hold would keep the bots walking to the point for up to its length.
            for (Player* bot : BotsOf(leader))
                if (TeamBotAI* ai = dynamic_cast<TeamBotAI*>(bot->AI()))
                    ai->Directives().ReleaseMovement();
        }
    }

    bool RetreatAssigner::InDanger(WorldModel const& world, std::string& reason, bool& sustained)
    {
        float healing = 0.0f;
        bool healerCanAct = false;
        PartyMemberFacts const* lowTank = nullptr;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet)
                continue;
            // Tanks: bots and players (Perception/Tanks.h). Healing: bots only (a player's heals are not known).
            if (Tanks::IsTank(m) && m.alive && m.healthPct < TANK_LOW_HEALTH_PCT)
                lowTank = &m;
            if (!m.isBot)
                continue;
            if (m.role == ROLE_HEALER && m.CanAct() && (m.manaPct < 0.0f || m.manaPct >= HEALER_MIN_MANA_PCT))
            {
                healerCanAct = true;
                // Its real heals (HealCapacity: +healing gear, talents, crit), the level guess
                // only for a healer without direct heals.
                float const hps = HealCapacity::MaxHps(m.healRates);
                healing += hps > 0.0f ? hps * (1.0f - ManaBudget::OVERHEAL) : HEALER_HPS_PER_LEVEL * m.level;
            }
        }

        if (lowTank && !healerCanAct)
        {
            sustained = false;
            reason = Format("the tank is at %.0f%% health and no healer can heal", lowTank->healthPct);
            return true;
        }
        if (healerCanAct && world.rates.incomingDps > healing)
        {
            sustained = true;
            reason = Format("incoming damage %.0f/s is more than the healers can heal (about %.0f/s)",
                world.rates.incomingDps, healing);
            return true;
        }
        return false;
    }

    void RetreatAssigner::Advise(WorldModel const& world, TeamPlan& plan)
    {
        RetreatPlan& r = plan.retreat;
        // A new fight may get its own hint.
        if (!world.HasPartyEnemy())
        {
            r.adviceSent = false;
            r.dangerSinceMs = 0;
            return;
        }
        if (r.adviceSent)
            return;

        std::string reason;
        bool sustained = false;
        if (!InDanger(world, reason, sustained))
        {
            r.dangerSinceMs = 0;
            return;
        }
        uint32 const now = world.refreshedAtMs;
        if (!r.dangerSinceMs)
            r.dangerSinceMs = now;
        // A pull starts with a burst before healing catches up: rule (a) must last a while.
        if (sustained && WorldTimer::getMSTimeDiff(r.dangerSinceMs, now) < DANGER_SUSTAIN_MS)
            return;
        r.adviceSent = true;
        plan.notes.push_back("Danger: " + reason + ". Run away (we follow you) or use '.partybot ai retreat'. "
            "Bots do not run on their own.");
    }

    void RetreatAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        RetreatPlan& r = plan.retreat;
        if (!r.active)
        {
            Advise(world, plan);
            return;
        }

        uint32 const now = world.refreshedAtMs;
        auto finish = [&](RetreatOutcome outcome, std::string const& text)
        {
            r.active = false;
            r.lastOutcome = outcome;
            plan.notes.push_back(text);
        };

        // Out through the door: done once nobody here fights any more (the ones still here
        // shook them off, the others are outside).
        if (r.toDoor)
        {
            if (!world.HasPartyEnemy())
                finish(RetreatOutcome::Done, "Retreat done: out of the fight.");
            else if (WorldTimer::getMSTimeDiff(r.startedMs, now) > TIMEOUT_MS)
                finish(RetreatOutcome::TimedOut, Format("Retreat ended after %u s: %u enemies still fighting us. "
                    "Bots fight normally again.", TIMEOUT_MS / 1000, uint32(world.enemies.size())));
            return;
        }

        // Everyone alive at the point: the leash only breaks once the victim is out of the area too.
        std::string missing;
        uint32 missingCount = 0;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet || !m.alive)
                continue;
            if (Dist3(m.pos, r.point) <= ARRIVED_DISTANCE)
                continue;
            if (missingCount++ < 3)
                missing += (missing.empty() ? "" : ", ") + NameIn(r, world, m.guid);
        }

        // Enemies leave world.enemies when they drop us from their threat list (evade clears it).
        if (!world.HasPartyEnemy() && !missingCount)
        {
            finish(RetreatOutcome::Done, "Retreat done: the enemies gave up and everyone is at the retreat point.");
            return;
        }
        if (WorldTimer::getMSTimeDiff(r.startedMs, now) > TIMEOUT_MS)
        {
            std::string text = Format("Retreat ended after %u s:", TIMEOUT_MS / 1000);
            if (world.HasPartyEnemy())
                text += Format(" %u enemies still fighting us.", uint32(world.enemies.size()));
            if (missingCount)
                text += Format(" Not at the point: %s%s.", missing.c_str(), missingCount > 3 ? " and others" : "");
            finish(RetreatOutcome::TimedOut, text + " Bots fight normally again.");
        }
    }

    void RetreatAssigner::Clear(TeamPlan& plan) const
    {
        RetreatPlan& r = plan.retreat;
        if (!r.active)
            return;
        r.active = false;
        r.lastOutcome = RetreatOutcome::Stopped;
        plan.notes.push_back("Retreat stopped: module 'retreat' is off.");
    }
}
