#include "Commander/Coordination/PullPlanner.h"
#include "Commander/Coordination/ClearPlanner.h"
#include "Commander/Coordination/LosBreak.h"
#include "Commander/Coordination/PullMethod.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/AssistPredictor.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/PatrolTracker.h"
#include "Commander/Perception/PullOdds.h"
#include "Commander/Perception/ShotSpot.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/ValueModel.h"
#include "PartyBotAI.h"
#include "Creature.h"
#include "CreatureGroups.h"
#include "DBCStores.h"
#include "Group.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "Map.h"
#include "Player.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Commander
{
    namespace
    {
        // Warrior ranged weapon spells (learned with the weapon skill) and Auto Shot.
        uint32 const SPELL_SHOOT_BOW = 2480;
        uint32 const SPELL_SHOOT_GUN = 7918;
        uint32 const SPELL_SHOOT_CROSSBOW = 7919;
        uint32 const SPELL_THROW = 2764;
        uint32 const SPELL_AUTO_SHOT = 75;

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

        // The puller: the main tank among the bots (Perception/Tanks.h; players cannot be ordered).
        Player* FindTank(WorldModel const& world, Map* map)
        {
            PartyMemberFacts const* tank = Tanks::Main(world, true);
            Player* player = tank ? map->GetPlayer(tank->guid) : nullptr;
            return (player && player->IsAlive()) ? player : nullptr;
        }

        bool Contains(std::vector<ObjectGuid> const& list, ObjectGuid guid)
        {
            return std::find(list.begin(), list.end(), guid) != list.end();
        }

        void AddUnique(std::vector<ObjectGuid>& list, std::vector<ObjectGuid> const& more)
        {
            for (ObjectGuid g : more)
                if (!Contains(list, g))
                    list.push_back(g);
        }

        PackFacts const* FindPackOf(WorldModel const& world, ObjectGuid guid)
        {
            for (PackFacts const& pack : world.packs)
                for (PackCreature const& c : pack.members)
                    if (c.guid == guid)
                        return &pack;
            return nullptr;
        }

        PackCreature const* FindMemberFacts(PackFacts const* pack, ObjectGuid guid)
        {
            if (!pack)
                return nullptr;
            for (PackCreature const& c : pack->members)
                if (c.guid == guid)
                    return &c;
            return nullptr;
        }

        PackCreature const* FindPackCreature(WorldModel const& world, ObjectGuid guid)
        {
            return FindMemberFacts(FindPackOf(world, guid), guid);
        }

        template <class Pred>
        bool AnyMember(PackFacts const& pack, Pred pred)
        {
            return std::any_of(pack.members.begin(), pack.members.end(), pred);
        }

        // Stands back and casts its main ranged spell (creature_spells CF_MAIN_RANGED_SPELL):
        // does not follow the tank.
        bool IsRangedCaster(Creature const* c)
        {
            return LosBreak::IsRangedCaster(c->GetEntry());
        }

        // Point on the line from the target towards the tank, `distance` from the target
        // (the tank's position if it is closer already). Straight line: pathing is not checked.
        Position3 StandPoint(Creature const* target, Unit const* tank, float distance)
        {
            Position3 const t = PositionOf(target);
            Position3 const k = PositionOf(tank);
            float const dx = k.x - t.x;
            float const dy = k.y - t.y;
            float const len = std::sqrt(dx * dx + dy * dy);
            if (len < 0.1f || len <= distance)
                return len < 0.1f ? t : k;
            float const f = distance / len;
            Position3 p;
            p.x = t.x + dx * f;
            p.y = t.y + dy * f;
            p.z = t.z + (k.z - t.z) * f;
            return p;
        }

        float MeleeStandDistance(Creature const* target)
        {
            return target->GetCombatReach() + PullPlanner::MELEE_STAND_EXTRA;
        }

        // Stand point of a walk-in pull: in melee on the tank's side.
        Position3 MeleeStandPoint(Creature const* target, Unit const* tank)
        {
            return StandPoint(target, tank, MeleeStandDistance(target));
        }

        float SpellMinRange(uint32 spellId)
        {
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
            SpellRangeEntry const* range = spell ? sSpellRangeStore.LookupEntry(spell->rangeIndex) : nullptr;
            return range ? range->minRange : 0.0f;
        }

        // Where the tank can shoot `target` from (Perception/ShotSpot.h).
        bool FindShotSpot(Map* map, Creature const* target, Player* tank, uint32 spellId, float range,
                          std::vector<Position3> const& avoid, ShotSpot::Spot& spot, ShotSpot::Failure& why)
        {
            ShotSpot::Request request;
            request.target = target;
            request.shooter = tank;
            request.maxRange = range;
            request.minRange = SpellMinRange(spellId);
            request.avoid = avoid;
            return ShotSpot::Find(map, request, spot, &why);
        }

        std::vector<ObjectGuid> AggroRangeGuids(std::vector<AssistPredictor::Joiner> const& joiners)
        {
            std::vector<ObjectGuid> out;
            for (AssistPredictor::Joiner const& j : joiners)
                if (j.reason == AssistPredictor::JoinReason::AggroRange)
                    out.push_back(j.guid);
            return out;
        }

        uint32 CountElites(Map* map, std::vector<ObjectGuid> const& guids)
        {
            uint32 elites = 0;
            for (ObjectGuid g : guids)
                if (Creature* e = map->GetCreature(g))
                    if (e->IsElite())
                        ++elites;
            return elites;
        }

        std::string NamesOf(Map* map, std::vector<ObjectGuid> const& guids, std::vector<ObjectGuid> const* marked = nullptr,
                            char const* mark = "")
        {
            std::string names;
            for (ObjectGuid g : guids)
            {
                if (!names.empty())
                    names += ", ";
                Creature* e = map->GetCreature(g);
                names += e ? e->GetName() : "?";
                if (marked && Contains(*marked, g))
                    names += mark;
            }
            return names;
        }

        // Name of a party member for notes written by the pure assigner.
        std::string NameIn(PullPlan const& pull, WorldModel const& world, ObjectGuid guid)
        {
            if (guid == world.leader)
                return "you";
            for (PullMemberName const& n : pull.names)
                if (n.guid == guid)
                    return n.name;
            return "a party member";
        }

        std::string DescribeIssues(std::vector<PullPlanner::Issue> const& issues, WorldModel const& world, PullPlan const& pull)
        {
            std::string text;
            for (PullPlanner::Issue const& i : issues)
            {
                if (!text.empty())
                    text += ", ";
                text += NameIn(pull, world, i.who);
                if (!strcmp(i.what, "hp") || !strcmp(i.what, "mana"))
                    text += Format(" %s %.0f%%", i.what, i.value);
                else if (!strcmp(i.what, "far"))
                    text += Format(" %.0f yd away", i.value);
                else
                    text += std::string(" ") + i.what;
            }
            return text;
        }

        // Enemy in the fight that the pull did not predict. Adds summoned by a predicted
        // creature belong to the pull.
        bool IsUnexpected(EnemyFacts const& e, PullPlan const& pull)
        {
            if (Contains(pull.expected, e.guid))
                return false;
            return !(e.summoned && Contains(pull.expected, e.summoner));
        }

        // Pulled enemy the tank waits for after a ranged pull fought in place: in the fight,
        // acting, coming for the tank with melee on (a caster standing back is not coming).
        bool IsComingToTank(EnemyFacts const& e, PullPlan const& pull)
        {
            return e.acting && !e.crowdControlled && e.meleeEnabled && e.victim == pull.tank;
        }

        // Every enemy coming to the tank is next to it (PullAssigner::ARRIVAL_DISTANCE).
        bool PackArrived(WorldModel const& world, PullPlan const& pull, Position3 const& tankAt)
        {
            for (ObjectGuid g : pull.expected)
            {
                EnemyFacts const* e = world.FindEnemy(g);
                if (e && IsComingToTank(*e, pull) &&
                    PackResolver::Distance(e->pos, tankAt) - e->boundingRadius > PullAssigner::ARRIVAL_DISTANCE)
                    return false;
            }
            return true;
        }

        // How long the pack may take to reach the tank (PullAssigner::ARRIVAL_*).
        uint32 ArrivalTimeoutMs(WorldModel const& world, PullPlan const& pull, Position3 const& tankAt)
        {
            float farthest = 0.0f;
            for (ObjectGuid g : pull.expected)
                if (EnemyFacts const* e = world.FindEnemy(g))
                    farthest = std::max(farthest, PackResolver::Distance(e->pos, tankAt));
            float const walkMs = farthest / PullAssigner::ARRIVAL_SPEED * 1000.0f * PullAssigner::ARRIVAL_TIME_FACTOR;
            // (copies: std::min takes references, the constants have no definition)
            uint32 const maxMs = PullAssigner::ARRIVAL_TIMEOUT_MAX_MS;
            return std::min(maxMs, uint32(walkMs) + PullAssigner::ARRIVAL_EXTRA_MS);
        }

        // One way to pull one candidate.
        struct Option
        {
            Creature* pullTarget = nullptr;
            std::vector<ObjectGuid> expected;
            std::vector<ObjectGuid> aggroAdds;      // subset of expected joining by aggro range
            uint32 elites = 0;
            float distance = 0.0f;
            bool caster = false;                    // stands back and casts (LosBreak::IsRangedCaster)
            bool ranged = false;
            std::string rangedWhy;                  // why it opens from range
            ShotSpot::Spot spot;                    // ranged: where the tank shoots from
            // A ranged pull was wanted but no shot spot works: walks in instead.
            bool noSpot = false;
            ShotSpot::Failure noSpotWhy = ShotSpot::Failure::None;
            // Walking in would bring this many by aggro range and the tank has no ranged pull.
            uint32 meleeAggroAddsNoRanged = 0;
        };

        // Target choice (user, 2026-11-02): fewest joiners; then a usable shot spot when a
        // ranged pull is wanted; fewest elites; a melee creature before one that stands back
        // and casts (it comes to the tank and the pack follows; the caster's spot is usually
        // deeper in the pack); nearest to the tank.
        bool Better(Option const& o, Option const& best)
        {
            if (!best.pullTarget)
                return true;
            if (o.expected.size() != best.expected.size())
                return o.expected.size() < best.expected.size();
            if (o.noSpot != best.noSpot)
                return !o.noSpot;
            if (o.elites != best.elites)
                return o.elites < best.elites;
            if (o.caster != best.caster)
                return !o.caster;
            return o.distance < best.distance;
        }

        // Hostile pack creature near the target that the prediction does not bring:
        // what the prediction cannot see (line of sight, wandering) is a risk of walking in.
        Creature* UnpredictedNeighbour(WorldModel const& world, Map* map, Creature* target,
                                       std::vector<ObjectGuid> const& expected)
        {
            Position3 const at = PositionOf(target);
            for (PackFacts const& p : world.packs)
            {
                for (PackCreature const& c : p.members)
                {
                    if (!c.aggressive || c.noAggro || Contains(expected, c.guid))
                        continue;
                    if (PackResolver::Distance(c.pos, at) > PullPlanner::RANGED_PULL_NEIGHBOR_RADIUS)
                        continue;
                    if (Creature* n = map->GetCreature(c.guid))
                        return n;
                }
            }
            return nullptr;
        }

        // `forceRanged` (PullMethod::Ranged): open from range whenever there is a spell, even with
        // no reason for it or when it brings more.
        // A ranged pull needs a shot spot (`avoid`: spots that failed live); none: walks in
        // (`noSpot`, ranked after the candidates that have one).
        Option Evaluate(WorldModel const& world, Map* map, Creature* c, Player* tank, uint32 rangedSpell, float rangedRange,
                        bool forceRanged, std::vector<Position3> const& avoid = std::vector<Position3>())
        {
            Option o;
            o.pullTarget = c;
            o.distance = tank->GetDistance(c);

            Position3 const meleeAt = MeleeStandPoint(c, tank);
            std::vector<AssistPredictor::Joiner> const melee = AssistPredictor::PredictDetailed(c, tank, &meleeAt);
            o.expected = AssistPredictor::Guids(melee);
            o.aggroAdds = AggroRangeGuids(melee);

            bool const caster = IsRangedCaster(c);
            o.caster = caster;
            Creature const* neighbour = UnpredictedNeighbour(world, map, c, o.expected);
            bool const walkInAdds = !o.aggroAdds.empty();
            if (!rangedSpell)
                o.meleeAggroAddsNoRanged = uint32(o.aggroAdds.size());
            if (rangedSpell && (forceRanged || caster || walkInAdds || neighbour))
            {
                ShotSpot::Spot spot;
                if (!FindShotSpot(map, c, tank, rangedSpell, rangedRange, avoid, spot, o.noSpotWhy))
                {
                    o.noSpot = true;
                    o.elites = CountElites(map, o.expected);
                    return o;
                }
                Position3 const rangedAt = spot.at;
                std::vector<AssistPredictor::Joiner> const ranged = AssistPredictor::PredictDetailed(c, tank, &rangedAt);
                std::vector<ObjectGuid> rangedExpected = AssistPredictor::Guids(ranged);
                std::vector<ObjectGuid> rangedAggro = AggroRangeGuids(ranged);
                if (caster)
                {
                    // It does not come to the tank: the original AI walks to it after the
                    // opening shot, so walking-in aggro range still applies later.
                    AddUnique(rangedExpected, o.aggroAdds);
                    AddUnique(rangedAggro, o.aggroAdds);
                }
                if (forceRanged || rangedExpected.size() <= o.expected.size())
                {
                    o.ranged = true;
                    o.spot = spot;
                    if (forceRanged)
                        o.rangedWhy = "you chose ranged pulls";
                    else if (caster)
                        o.rangedWhy = Format("%s stands back and casts", c->GetName());
                    else if (walkInAdds)
                        o.rangedWhy = Format("walking in would bring %u more by aggro range", uint32(o.aggroAdds.size()));
                    else
                        o.rangedWhy = Format("%s is within %.0f yd of the target", neighbour->GetName(),
                            PullPlanner::RANGED_PULL_NEIGHBOR_RADIUS);
                    o.expected = std::move(rangedExpected);
                    o.aggroAdds = std::move(rangedAggro);
                }
            }
            o.elites = CountElites(map, o.expected);
            return o;
        }

        // Best pull among `candidates` (Better), leaving out `excluded`. Stealthed members are
        // skipped (the tank cannot target them and would walk for 20 seconds) and counted.
        Option ChooseOption(WorldModel const& world, Map* map, PackFacts const* pack,
                            std::vector<ObjectGuid> const& candidates, std::vector<ObjectGuid> const& excluded,
                            Player* tank, uint32 rangedSpell, float rangedRange, bool forceRanged, uint32& stealthed)
        {
            Option best;
            for (ObjectGuid guid : candidates)
            {
                if (Contains(excluded, guid))
                    continue;
                Creature* c = map->GetCreature(guid);
                if (!c || !c->IsAlive() || c->IsInCombat() || !tank->IsValidAttackTarget(c))
                    continue;
                PackCreature const* facts = FindMemberFacts(pack, guid);
                if (facts && facts->stealthed)
                {
                    ++stealthed;
                    continue;
                }
                Option o = Evaluate(world, map, c, tank, rangedSpell, rangedRange, forceRanged);
                if (Better(o, best))
                    best = std::move(o);
            }
            return best;
        }

        // How long the fight with the target and `expected` should last: their health over the
        // party's damage (Value::DealtDps, measured or estimated), see PullPlanner.h.
        float ExpectedFightSec(WorldModel const& world, Map* map, Creature* target, std::vector<ObjectGuid> const& expected)
        {
            float health = float(target->GetHealth());
            for (ObjectGuid g : expected)
                if (g != target->GetObjectGuid())
                    if (Creature const* c = map->GetCreature(g))
                        if (c->IsAlive())
                            health += float(c->GetHealth());
            float const sec = health / std::max(1.0f, Value::DealtDps(world)) * PullPlanner::FIGHT_TIME_MARGIN +
                              PullPlanner::PULL_LEAD_SEC;
            return std::min(PullPlanner::PATROL_HORIZON_MAX_SEC, std::max(PullPlanner::PATROL_HORIZON_SEC, sec));
        }

        // A patrol that will walk next to the pulled creatures or the fight spot while the fight lasts.
        struct PatrolThreat
        {
            Creature* patrol = nullptr;
            bool atPack = false;        // next to a pulled creature, else at the fight spot
            float enterSec = 0.0f;      // first time within its clearance (0 = there now)
            // Start the pull this many seconds from now and it stays away for the whole fight;
            // < 0: no such start within PATROL_WAIT_MAX_MS (its loop is shorter than the fight).
            float waitSec = 0.0f;
            float fightSec = 0.0f;      // expected fight length the check used
            float clearance = 0.0f;
            PatrolTracker::TrackSource source = PatrolTracker::TrackSource::Standing;

            bool WaitHelps() const { return waitSec >= 0.0f; }
        };

        // Patrols: WorldModel::patrols (out-of-combat path walkers within 100 yd of the leader).
        // The target's own walking group is not a threat to itself.
        // The whole pull counts, not only the target (strat-20261102-235355: Patchwork Horror
        // was 20 yd from the target, outside its clearance, but walked through the rest of the
        // pack and was called in for help).
        bool FindPatrolThreat(WorldModel const& world, Map* map, Creature* target, Unit* tank, Unit* leader,
                              Position3 const& spot, std::vector<ObjectGuid> const& expected, PatrolThreat& out)
        {
            std::vector<Position3> pullAt;
            pullAt.push_back(PositionOf(target));
            for (ObjectGuid g : expected)
                if (g != target->GetObjectGuid())
                    if (Creature const* m = map->GetCreature(g))
                        if (m->IsAlive())
                            pullAt.push_back(PositionOf(m));
            PackCreature const* targetFacts = FindPackCreature(world, target->GetObjectGuid());
            bool const targetPatrols = targetFacts && targetFacts->patrol;
            CreatureGroup const* targetGroup = target->GetCreatureGroup();
            float const fightSec = ExpectedFightSec(world, map, target, expected);
            float const waitMaxSec = PullPlanner::PATROL_WAIT_MAX_MS / 1000.0f;

            bool found = false;
            for (PackCreature const& c : world.patrols)
            {
                if (!c.aggressive || c.noAggro || c.tappedByOther || c.guid == target->GetObjectGuid())
                    continue;
                // Already counted as a joiner by the prediction.
                if (Contains(expected, c.guid))
                    continue;
                Creature* patrol = map->GetCreature(c.guid);
                if (!patrol || !patrol->IsAlive() || patrol->IsInCombat())
                    continue;
                if (targetGroup && patrol->GetCreatureGroup() == targetGroup)
                    continue;
                // A patrolling target walks with its neighbours (distance-linked, no group record).
                if (targetPatrols && patrol->GetDistance(target) <= PackResolver::LINK_DISTANCE)
                    continue;

                // Far enough ahead to find a start within the wait limit that is clear for the
                // whole fight.
                PatrolTracker::Track const track = PatrolTracker::Predict(patrol, fightSec + waitMaxSec);
                float const toTank = std::max(PullPlanner::PATROL_MIN_CLEARANCE, patrol->GetAttackDistance(tank));
                float const toParty = std::max(PullPlanner::PATROL_MIN_CLEARANCE, patrol->GetAttackDistance(leader ? leader : tank));
                // Pack: the earliest entry next to any pulled creature.
                PatrolTracker::Approach a;
                bool packAlwaysWithin = false;
                std::vector<PatrolTracker::Zone> zones;
                for (Position3 const& at : pullAt)
                {
                    PatrolTracker::Approach const m = PatrolTracker::ClosestApproach(track, at, toTank);
                    packAlwaysWithin = packAlwaysWithin || m.alwaysWithin;
                    if (m.comesWithin && (!a.comesWithin || m.enterSec < a.enterSec))
                        a = m;
                    zones.push_back({ at, toTank });
                }
                PatrolTracker::Approach const b = PatrolTracker::ClosestApproach(track, spot, toParty);
                if (!a.comesWithin && !b.comesWithin)
                    continue;
                // Inside the whole time (a short loop around the pack, or waiting at a node):
                // waiting does not help. If it is close enough to join, the prediction counts it
                // like any pack member.
                if (packAlwaysWithin || b.alwaysWithin)
                    continue;
                zones.push_back({ spot, toParty });
                float start = PatrolTracker::FirstClearWindow(track, zones, fightSec);
                if (start == 0.0f)
                    continue;       // comes by only after the fight
                if (start > waitMaxSec)
                    start = -1.0f;

                PatrolThreat t;
                t.patrol = patrol;
                t.source = track.source;
                t.atPack = a.comesWithin && (!b.comesWithin || a.enterSec <= b.enterSec);
                t.enterSec = t.atPack ? a.enterSec : b.enterSec;
                t.waitSec = start;
                t.fightSec = fightSec;
                t.clearance = t.atPack ? toTank : toParty;
                // Report the worst: one waiting cannot avoid, else the one that waits longest.
                bool const worse = !found ||
                    (!t.WaitHelps() && out.WaitHelps()) ||
                    (t.WaitHelps() == out.WaitHelps() && (t.WaitHelps() ? t.waitSec > out.waitSec : t.enterSec < out.enterSec));
                if (worse)
                    out = t;
                found = true;
            }
            return found;
        }

        // "is within 19 yd of the target (path)" / "comes within 15 yd of the fight spot in about 6 s (moving)"
        std::string DescribePatrol(PatrolThreat const& t)
        {
            char const* where = t.atPack ? "the pack" : "the fight spot";
            std::string text = t.enterSec <= 0.0f
                ? Format("is within %.0f yd of %s", t.clearance, where)
                : Format("comes within %.0f yd of %s in about %.0f s", t.clearance, where, t.enterSec);
            return text + Format(" (%s; fight about %.0f s)", PatrolTracker::SourceName(t.source), t.fightSec);
        }

        std::string ClearTime(PatrolThreat const& t)
        {
            return Format("about %.0f s", std::ceil(t.waitSec));
        }

        bool IsStealthedPackCreature(WorldModel const& world, ObjectGuid guid)
        {
            PackCreature const* c = FindPackCreature(world, guid);
            return c && c->stealthed;
        }

        // Line of sight pull: pulled enemies standing back casting at the tank (map thread).
        // "Standing back": melee off (a CF_MAIN_RANGED_SPELL cast turns it off, scripts may too),
        // or a main ranged spell in its profile and not moving (between two casts).
        std::vector<Creature*> CastersStandingBack(WorldModel const& world, PullPlan const& pull, Map* map,
                                                   Position3 const& tankAt)
        {
            std::vector<Creature*> casters;
            for (EnemyFacts const& e : world.enemies)
            {
                if (e.onAllyOnly || !e.acting || e.crowdControlled || e.victim != pull.tank || IsUnexpected(e, pull))
                    continue;
                if (PackResolver::Distance(e.pos, tankAt) <= PullPlanner::LOS_CASTER_MIN_DISTANCE)
                    continue;
                Creature* c = map->GetCreature(e.guid);
                if (!c || !c->IsAlive())
                    continue;
                if (e.meleeEnabled && !(IsRangedCaster(c) && !c->IsMoving()))
                    continue;
                casters.push_back(c);
                if (casters.size() >= PullPlanner::LOS_MAX_CASTERS)
                    break;
            }
            return casters;
        }

        // Moving there takes the tank towards an idle hostile creature that is not in the pull.
        bool TowardsOtherCreature(WorldModel const& world, PullPlan const& pull, Position3 const& point,
                                  Position3 const& center)
        {
            for (PackFacts const& p : world.packs)
            {
                for (PackCreature const& c : p.members)
                {
                    if (!c.aggressive || c.noAggro || Contains(pull.expected, c.guid))
                        continue;
                    float const d = PackResolver::Distance(c.pos, point);
                    if (d < PullPlanner::LOS_OTHER_CREATURE_CLEARANCE && d < PackResolver::Distance(c.pos, center))
                        return true;
                }
            }
            return false;
        }

        // Pure (PullAssigner). The break ends when every caster came within LOS_CASTER_ARRIVED of
        // the tank or cannot come (dead, out of the fight, crowd controlled), the fight is over,
        // or after LOS_BREAK_TIMEOUT_MS. `timedOut` says which.
        // A caster turning melee back on is not the end yet: it has only started to walk, and
        // the tank leaving the point would put it back in its sight (it stops again).
        bool LosBreakOver(WorldModel const& world, PullPlan const& pull, Position3 const& tankAt, uint32 nowMs,
                          bool& timedOut)
        {
            uint32 const timeoutMs = PullAssigner::LOS_BREAK_TIMEOUT_MS;
            float const arrived = PullAssigner::LOS_CASTER_ARRIVED;
            timedOut = false;
            if (!world.HasPartyEnemy())
                return true;
            if (WorldTimer::getMSTimeDiff(pull.losSinceMs, nowMs) > timeoutMs)
            {
                timedOut = true;
                return true;
            }
            for (ObjectGuid g : pull.losCasters)
            {
                EnemyFacts const* e = world.FindEnemy(g);
                if (e && e->acting && !e->crowdControlled && PackResolver::Distance(e->pos, tankAt) > arrived)
                    return false;
            }
            return true;
        }
    }

    uint32 PullPlanner::FindRangedPullSpell(Player* tank, float& range)
    {
        range = 0.0f;
        uint32 spellId = 0;
        PartyBotAI const* ai = tank->AI() ? dynamic_cast<PartyBotAI const*>(tank->AI()) : nullptr;

        auto hasAmmo = [tank]() { return CombatBotBaseAI::HasAmmoEquipped(tank); };

        switch (tank->GetClass())
        {
            case CLASS_WARRIOR:
            {
                Item* weapon = tank->GetWeaponForAttack(RANGED_ATTACK, true, true);
                if (!weapon || !weapon->GetProto())
                    break;
                switch (weapon->GetProto()->SubClass)
                {
                    case ITEM_SUBCLASS_WEAPON_BOW:      spellId = hasAmmo() ? SPELL_SHOOT_BOW : 0; break;
                    case ITEM_SUBCLASS_WEAPON_GUN:      spellId = hasAmmo() ? SPELL_SHOOT_GUN : 0; break;
                    case ITEM_SUBCLASS_WEAPON_CROSSBOW: spellId = hasAmmo() ? SPELL_SHOOT_CROSSBOW : 0; break;
                    case ITEM_SUBCLASS_WEAPON_THROWN:   spellId = SPELL_THROW; break;
                    default: break;
                }
                break;
            }
            case CLASS_DRUID:
                if (ai && ai->m_spells.druid.pFaerieFireFeral)
                    spellId = ai->m_spells.druid.pFaerieFireFeral->Id;
                break;
            case CLASS_HUNTER:
            {
                Item* weapon = tank->GetWeaponForAttack(RANGED_ATTACK, true, true);
                if (weapon && hasAmmo())
                    spellId = SPELL_AUTO_SHOT;
                break;
            }
            default:
                break;  // paladin: no ranged opener (see header)
        }
        if (!spellId || !tank->HasSpell(spellId))
            return 0;

        SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
        if (!spell)
            return 0;
        SpellRangeEntry const* srange = sSpellRangeStore.LookupEntry(spell->rangeIndex);
        float const maxRange = srange ? srange->maxRange : 0.0f;
        if (maxRange < 10.0f)
            return 0;
        range = std::min(PULL_RANGE, maxRange);
        return spellId;
    }

    std::string PullPlanner::MissingAmmoNote(Player* tank)
    {
        uint32 const ammoType = CombatBotBaseAI::GetRequiredAmmoSubClass(tank);
        if (!ammoType || CombatBotBaseAI::HasAmmoEquipped(tank))
            return std::string();
        if (tank->GetClass() != CLASS_WARRIOR && tank->GetClass() != CLASS_HUNTER)
            return std::string();
        Item const* weapon = tank->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
        return Format("No ranged pull: %s has %s but no %s. Give it some (it equips them out of combat).",
            tank->GetName(), weapon->GetProto()->Name1, ammoType == ITEM_SUBCLASS_BULLET ? "bullets" : "arrows");
    }

    std::vector<PullPlanner::Issue> PullPlanner::CheckReady(WorldModel const& world, float healerManaPct)
    {
        std::vector<Issue> issues;
        PartyMemberFacts const* leader = world.FindMember(world.leader);
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet)
                continue;
            if (!m.alive)
            {
                issues.push_back({ m.guid, "dead", 0.0f });
                continue;
            }
            if (m.inCombat)
                issues.push_back({ m.guid, "combat", 0.0f });
            if (m.healthPct < READY_HEALTH_PCT)
                issues.push_back({ m.guid, "hp", m.healthPct });
            // Healers may need more than the others before a hard pack (ClearPlanner raises it).
            float const manaNeeded = (m.isBot && m.role == ROLE_HEALER) ? healerManaPct : READY_MANA_PCT;
            // Healers: the mana pool, not manaPct (a shapeshifted healer still heals with it).
            // Others: only while mana is their active power. A feral druid bot stays in bear /
            // cat form and PartyBotAI::DrinkAndEat only drinks with mana active, so counting its
            // pool would hold every pull until the ready timeout.
            // Players can drink in any form themselves: their pool counts too.
            bool const usePool = (m.isBot && m.role == ROLE_HEALER) || !m.isBot;
            if (usePool ? m.UsesMana() : m.manaPct >= 0.0f)
            {
                float const manaPct = float(m.mana) * 100.0f / float(std::max(1u, m.maxMana));
                if (manaPct < manaNeeded)
                    issues.push_back({ m.guid, "mana", manaPct });
            }
            if (leader && m.guid != leader->guid)
            {
                float const d = PackResolver::Distance(m.pos, leader->pos);
                if (d > READY_MAX_DISTANCE)
                    issues.push_back({ m.guid, "far", d });
            }
        }
        return issues;
    }

    bool PullPlanner::Plan(Player* leader, PartyCommander& commander, Creature* selected, bool skipReady,
                           std::vector<std::string>& report)
    {
        WorldModel const& world = commander.GetWorld();
        Map* map = leader->GetMap();

        Player* tank = FindTank(world, map);
        if (!tank)
        {
            report.push_back("No living tank bot in your party on this map (.partybot setrole <name> tank).");
            return false;
        }

        // Candidates: the selected creature's pack, or the nearest pack with a hostile member.
        PackFacts const* pack = nullptr;
        std::vector<ObjectGuid> candidates;
        std::vector<std::string> notes;     // reported after the summary line
        if (selected)
        {
            if (!selected->IsAlive() || selected->IsInCombat() || !tank->IsValidAttackTarget(selected))
            {
                report.push_back("Selected creature cannot be pulled (dead, in combat or not attackable).");
                return false;
            }
            pack = FindPackOf(world, selected->GetObjectGuid());
            if (pack)
            {
                // Somebody else has loot rights: their fight, not ours.
                if (AnyMember(*pack, [](PackCreature const& c) { return c.tappedByOther; }))
                {
                    report.push_back(Format("Someone else is fighting %s's pack (they have loot rights).", selected->GetName()));
                    return false;
                }
                // Bosses are left to the player, but selecting one is an explicit choice: allowed.
                if (AnyMember(*pack, [](PackCreature const& c) { return c.boss; }))
                    notes.push_back("Note: this pack has a boss. Pulling it because you selected it.");
                for (PackCreature const& m : pack->members)
                    candidates.push_back(m.guid);
            }
            else
            {
                std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(selected->GetEntry());
                if (profile && profile->boss)
                    notes.push_back("Note: this is a boss. Pulling it because you selected it.");
                candidates.push_back(selected->GetObjectGuid());
            }
        }
        else
        {
            uint32 tapped = 0;
            uint32 bosses = 0;
            for (PackFacts const& p : world.packs)
            {
                // No-aggro creatures never start a fight, so they do not make a pack worth pulling.
                bool const hostile = AnyMember(p, [](PackCreature const& c) { return c.aggressive && !c.noAggro; });
                if (!hostile)
                    continue;
                if (AnyMember(p, [](PackCreature const& c) { return c.boss; }))
                {
                    ++bosses;       // only when the player selects it
                    continue;
                }
                if (AnyMember(p, [](PackCreature const& c) { return c.tappedByOther; }))
                {
                    ++tapped;
                    continue;
                }
                pack = &p;
                break;  // packs are sorted nearest first
            }
            if (bosses)
                notes.push_back(Format("Passed over %u pack(s) with a boss; select the boss to pull it.", bosses));
            if (tapped)
                notes.push_back(Format("Passed over %u pack(s) someone else is fighting.", tapped));
            if (!pack)
            {
                report.push_back("No hostile pack within 50 yards.");
                report.insert(report.end(), notes.begin(), notes.end());
                return false;
            }
            for (PackCreature const& m : pack->members)
                candidates.push_back(m.guid);
        }

        // The leader's pull method (PullMethod.h): melee never looks for a ranged opener.
        PullMethod const method = sCommanderMgr.GetPullMethod(leader->GetObjectGuid());
        bool const forceRanged = method == PullMethod::Ranged;
        float rangedRange = 0.0f;
        uint32 const rangedSpell = method == PullMethod::Melee ? 0 : FindRangedPullSpell(tank, rangedRange);
        if (!rangedSpell && method != PullMethod::Melee)
        {
            std::string const ammoNote = MissingAmmoNote(tank);
            if (!ammoNote.empty())
                notes.push_back(ammoNote);
            else if (forceRanged)
                notes.push_back(Format("You chose ranged pulls, but %s has no ranged pull: walking in.", tank->GetName()));
        }

        // Each candidate is judged with its best approach (walk in, or open from a shot spot),
        // then ranked (Better).
        uint32 stealthed = 0;
        Option best = ChooseOption(world, map, pack, candidates, std::vector<ObjectGuid>(), tank, rangedSpell, rangedRange,
                                   forceRanged, stealthed);
        if (!best.pullTarget)
        {
            report.push_back(stealthed ? "Only stealthed creatures left in that pack; the tank cannot target them."
                                       : "No pullable creature in that pack.");
            return false;
        }

        // Stealthed pack members still join the fight: count them.
        uint32 stealthedAdded = 0;
        if (pack)
        {
            for (PackCreature const& c : pack->members)
            {
                if (c.stealthed && !Contains(best.expected, c.guid))
                {
                    best.expected.push_back(c.guid);
                    if (c.elite)
                        ++best.elites;
                    ++stealthedAdded;
                }
            }
        }

        // Fight where they stand, or bring them back (PullOdds.h): is the party strong enough
        // for a creature or two nearby joining? Bringing them back only helps when the spot has
        // less add risk than the pack. Casters that stand back come along too once the tank
        // is out of their sight (SearchLosBreak); otherwise the party deals with them.
        Position3 const spot = PositionOf(leader);
        bool const distant = leader->GetDistance(best.pullTarget) > RETURN_MIN_DISTANCE;   // `far` is a Windows macro
        PullOdds::Edge const edge = PullOdds::PartyEdge(world, map, best.expected);
        PullOdds::AddRisk const packRisk = PullOdds::NearbyAdds(world, PositionOf(best.pullTarget), best.expected);
        PullOdds::AddRisk const spotRisk = PullOdds::NearbyAdds(world, spot, best.expected);
        bool const fightThere = PullOdds::FightThere(edge, packRisk);
        bool const returnToSpot = distant && !fightThere && spotRisk.total < packRisk.total;

        // Bringing them back: open from range when the tank can (and that brings no more).
        ShotSpot::Spot returnSpot;
        ShotSpot::Failure returnSpotWhy = ShotSpot::Failure::None;
        if (returnToSpot && rangedSpell && !best.ranged && !best.noSpot &&
            FindShotSpot(map, best.pullTarget, tank, rangedSpell, rangedRange, std::vector<Position3>(), returnSpot,
                         returnSpotWhy))
        {
            Position3 const rangedAt = returnSpot.at;
            std::vector<AssistPredictor::Joiner> const joiners = AssistPredictor::PredictDetailed(best.pullTarget, tank, &rangedAt);
            std::vector<ObjectGuid> const expected = AssistPredictor::Guids(joiners);
            if (expected.size() <= best.expected.size())
            {
                best.spot = returnSpot;
                // Stealthed pack members counted above stay counted.
                std::vector<ObjectGuid> merged = expected;
                for (ObjectGuid g : best.expected)
                    if (IsStealthedPackCreature(world, g) && !Contains(merged, g))
                        merged.push_back(g);
                best.ranged = true;
                best.rangedWhy = "bringing them back";
                best.expected = std::move(merged);
                best.aggroAdds = AggroRangeGuids(joiners);
                best.elites = CountElites(map, best.expected);
            }
        }

        TeamPlan& plan = commander.GetPlanMutable();
        PullPlan& pull = plan.pull;
        pull = PullPlan();
        plan.lastPullOutcome = PullOutcome::None;
        pull.target = best.pullTarget->GetObjectGuid();
        pull.tank = tank->GetObjectGuid();
        pull.expected = best.expected;
        pull.aggroRangeAdds = best.aggroAdds;
        pull.spot = spot;
        pull.returnToSpot = returnToSpot;
        pull.phaseSinceMs = WorldTimer::getMSTime();
        pull.phase = skipReady ? PullPhase::Approach : PullPhase::WaitReady;
        pull.healerManaPct = READY_MANA_PCT;
        // `pull now` means now: no second prediction, no waiting for patrols.
        pull.recheckOnApproach = !skipReady;
        if (best.ranged)
        {
            pull.rangedPullSpell = rangedSpell;
            pull.rangedPullRange = rangedRange;
            pull.rangedSpot = best.spot.at;
        }
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet)
                continue;
            if (Unit* u = map->GetUnit(m.guid))
                pull.names.push_back({ m.guid, u->GetName() });
        }

        report.push_back(Format("%s pulls %s: expect %u (%u elite): %s",
            tank->GetName(), best.pullTarget->GetName(), uint32(pull.expected.size()), best.elites,
            NamesOf(map, pull.expected, &pull.aggroRangeAdds, " (aggro range)").c_str()));
        if (candidates.size() > 1)
            report.push_back(Format("Chosen from %u pack members (fewest joiners, a spot to shoot from, fewest elites, "
                "melee before casters, nearest).", uint32(candidates.size())));
        if (stealthedAdded)
            report.push_back(Format("Counted %u stealthed creature(s) of the pack.", stealthedAdded));
        if (best.ranged)
        {
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(rangedSpell);
            std::string const clearance = best.spot.clearance < ShotSpot::NO_CREATURE_CLEARANCE
                ? Format(", %.0f yd outside the nearest aggro range", best.spot.clearance) : std::string();
            report.push_back(Format("Ranged pull: %s opens with %s from %.0f yd%s (%s).",
                tank->GetName(), spell ? spell->SpellName[0].c_str() : "?", best.spot.range, clearance.c_str(),
                best.rangedWhy.c_str()));
        }
        else if (best.noSpot)
            report.push_back(Format("Walking in: no spot to shoot %s from (%s).", best.pullTarget->GetName(),
                ShotSpot::FailureName(best.noSpotWhy)));
        else if (method == PullMethod::Melee)
            report.push_back(best.meleeAggroAddsNoRanged
                ? Format("Walking in (you chose melee pulls); that brings %u by aggro range.", best.meleeAggroAddsNoRanged)
                : std::string("Walking in (you chose melee pulls)."));
        else if (best.meleeAggroAddsNoRanged)
            report.push_back(Format("Walking in brings %u by aggro range; %s has no ranged pull (warrior: bow/gun/crossbow "
                "with ammo or a thrown weapon; druid: Faerie Fire (Feral)).", best.meleeAggroAddsNoRanged, tank->GetName()));
        std::string const odds = PullOdds::Describe(map, edge, packRisk);
        if (pull.returnToSpot)
            report.push_back(Format("Tank brings them back to where you stand, walking backwards (%s; at your spot %.1f).",
                odds.c_str(), spotRisk.total));
        else if (!distant)
            report.push_back("Fight where they stand (they are next to you).");
        else if (fightThere)
            report.push_back(Format("Fight where they stand: the party can take an add (%s).", odds.c_str()));
        else
            report.push_back(Format("Fight where they stand: your spot is no safer (%s; at your spot %.1f).",
                odds.c_str(), spotRisk.total));
        report.insert(report.end(), notes.begin(), notes.end());

        // Fight spot too close to a creature that is not part of the pull.
        for (PackFacts const& p : world.packs)
        {
            for (PackCreature const& c : p.members)
            {
                if (!c.aggressive || Contains(pull.expected, c.guid))
                    continue;
                Creature* other = map->GetCreature(c.guid);
                if (!other)
                    continue;
                float const d = PackResolver::Distance(pull.spot, c.pos);
                if (d < other->GetAttackDistance(leader) + 3.0f)
                    report.push_back(Format("Warning: %s is %.0f yd from the fight spot (aggro range %.0f).",
                        other->GetName(), d, other->GetAttackDistance(leader)));
            }
        }

        // Patrols: reported now, waited for when the tank is about to go (RecheckBeforeApproach).
        PatrolThreat patrol;
        if (FindPatrolThreat(world, map, best.pullTarget, tank, leader, pull.spot, pull.expected, patrol))
        {
            std::string const line = std::string("Patrol ") + patrol.patrol->GetName() + " " + DescribePatrol(patrol);
            if (skipReady)
                report.push_back(line + ". 'pull now' does not wait for it.");
            else if (!patrol.WaitHelps())
                report.push_back(line + ". Its loop is shorter than the fight, waiting does not help.");
            else
                report.push_back(line + Format(". The pull waits until it has passed (%s, at most %u s).",
                    ClearTime(patrol).c_str(), PATROL_WAIT_MAX_MS / 1000));
        }

        if (!skipReady)
            report.push_back("Waiting until everyone is ready (90% hp, 80% mana, within 40 yd). '.partybot ai pull now' skips this.");
        return true;
    }

    void PullPlanner::SearchLosBreak(TeamPlan& plan, WorldModel const& world, Map* map, uint32 nowMs)
    {
        PullPlan& pull = plan.pull;
        if (pull.phase != PullPhase::Settle || pull.losBreak != PullLosBreak::None)
            return;
        Unit* tank = map->GetUnit(pull.tank);
        if (!tank || !tank->IsAlive())
            return;
        Position3 const tankAt = PositionOf(tank);

        // No caster standing back yet: looked at again next refresh while Settle lasts. Facts
        // only, no line of sight checks until there is one.
        std::vector<Creature*> const casters = CastersStandingBack(world, pull, map, tankAt);
        if (casters.empty())
            return;

        // Around the fight spot when the pack was brought back, else where the tank fights.
        Position3 const center = pull.returnToSpot ? pull.spot : tankAt;

        // Shared search (LosBreak.h). Healer bots must still see the tank at the point (they do
        // not move with it).
        LosBreak::Search search;
        search.center = center;
        search.casters.assign(casters.begin(), casters.end());
        search.mover = tank;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet || !m.isBot || !m.alive || m.role != ROLE_HEALER || m.guid == pull.tank)
                continue;
            if (Unit* h = map->GetUnit(m.guid))
                search.watchers.push_back(h);
            if (search.watchers.size() >= LOS_MAX_HEALERS)
                break;
        }
        search.rejects = [&world, &pull, &center](Position3 const& p)
        {
            return TowardsOtherCreature(world, pull, p, center);
        };

        Creature const* first = casters.front();
        Position3 point;
        bool const found = LosBreak::FindPoint(map, search, point);
        if (!found)
        {
            pull.losBreak = PullLosBreak::NoPoint;
            plan.notes.push_back(Format("%s stands back and casts; no spot out of its sight nearby, fighting here.",
                first->GetName()));
            return;
        }
        pull.losBreak = PullLosBreak::Active;
        pull.losPoint = point;
        pull.losSinceMs = nowMs;
        pull.losCasters.clear();
        for (Creature const* c : casters)
            pull.losCasters.push_back(c->GetObjectGuid());
        plan.notes.push_back(Format("%s stands back and casts: %s steps out of its sight (%.0f yd) so it comes closer.",
            first->GetName(), tank->GetName(), PackResolver::Distance(tankAt, point)));
    }

    namespace
    {
        // Stealthed pack members counted by the planner stay counted.
        void KeepStealthed(WorldModel const& world, std::vector<ObjectGuid> const& before, std::vector<ObjectGuid>& expected)
        {
            for (ObjectGuid g : before)
                if (!Contains(expected, g) && IsStealthedPackCreature(world, g))
                    expected.push_back(g);
        }

        // No shot spot works for the pull's target. Brain, map thread. Another pack member that
        // has a spot becomes the target (ChooseOption without the given-up ones, at most
        // MAX_TARGET_SWITCHES); none: the tank walks in on the current target. Either way a new
        // Approach (the shot steps start over) and a full recheck (prediction, limit, patrols).
        void ReplaceUnshootableTarget(TeamPlan& plan, WorldModel const& world, Map* map, Player* tank,
                                      Creature* target, char const* why, uint32 nowMs)
        {
            PullPlan& pull = plan.pull;
            pull.excludedTargets.push_back(pull.target);
            pull.failedSpots.clear();
            pull.spotFailures = 0;
            pull.phaseSinceMs = nowMs;
            pull.rechecked = false;

            Option best;
            PackFacts const* pack = FindPackOf(world, pull.target);
            if (pack && pull.excludedTargets.size() <= PullPlanner::MAX_TARGET_SWITCHES)
            {
                std::vector<ObjectGuid> candidates;
                for (PackCreature const& m : pack->members)
                    candidates.push_back(m.guid);
                uint32 stealthed = 0;
                // Already a ranged pull: only a member that can be shot is an alternative.
                best = ChooseOption(world, map, pack, candidates, pull.excludedTargets, tank, pull.rangedPullSpell,
                                    pull.rangedPullRange, true, stealthed);
            }
            if (best.pullTarget && best.ranged)
            {
                KeepStealthed(world, pull.expected, best.expected);
                plan.notes.push_back(Format("%s cannot shoot %s (%s): pulling %s instead, from %.0f yd. Expect %u: %s.",
                    tank->GetName(), target->GetName(), why, best.pullTarget->GetName(), best.spot.range,
                    uint32(best.expected.size()), NamesOf(map, best.expected, &best.aggroAdds, " (aggro range)").c_str()));
                pull.target = best.pullTarget->GetObjectGuid();
                pull.expected = std::move(best.expected);
                pull.aggroRangeAdds = std::move(best.aggroAdds);
                pull.rangedSpot = best.spot.at;
                return;
            }

            Option melee = Evaluate(world, map, target, tank, 0, 0.0f, false);
            KeepStealthed(world, pull.expected, melee.expected);
            plan.notes.push_back(Format("%s cannot shoot %s (%s) and no other pack member can be shot: walking in. Expect %u: %s.",
                tank->GetName(), target->GetName(), why, uint32(melee.expected.size()),
                NamesOf(map, melee.expected, &melee.aggroAdds, " (aggro range)").c_str()));
            pull.rangedPullSpell = 0;
            pull.expected = std::move(melee.expected);
            pull.aggroRangeAdds = std::move(melee.aggroAdds);
        }

        // The tank reported that it cannot shoot from its spot (PullShotState::spotFailed).
        // Brain, map thread. Another spot for the same target (not a failed one, at most
        // MAX_SPOT_FAILURES), else another target (ReplaceUnshootableTarget).
        void HandleSpotFailure(TeamPlan& plan, WorldModel const& world, Map* map, uint32 nowMs)
        {
            PullPlan& pull = plan.pull;
            PullShotState const& shot = pull.rangedShot;
            Creature* target = map->GetCreature(pull.target);
            Player* tank = map->GetPlayer(pull.tank);
            if (!target || !tank || !target->IsAlive())
                return;         // the assigner deals with it
            pull.failedSpots.push_back(shot.standAt);
            ++pull.spotFailures;

            ShotSpot::Spot spot;
            ShotSpot::Failure spotWhy = ShotSpot::Failure::None;
            if (pull.spotFailures < PullPlanner::MAX_SPOT_FAILURES &&
                FindShotSpot(map, target, tank, pull.rangedPullSpell, pull.rangedPullRange, pull.failedSpots, spot, spotWhy))
            {
                plan.notes.push_back(Format("%s cannot shoot %s from there (%s): trying a spot %.0f yd away.",
                    tank->GetName(), target->GetName(), shot.spotFailWhy,
                    PackResolver::Distance(PositionOf(tank), spot.at)));
                pull.rangedSpot = spot.at;
                pull.phaseSinceMs = nowMs;
                return;
            }
            ReplaceUnshootableTarget(plan, world, map, tank, target, shot.spotFailWhy, nowMs);
        }
    }

    PullPlanner::RecheckResult PullPlanner::RecheckBeforeApproach(TeamPlan& plan, WorldModel const& world, Map* map,
                                                                  uint32 nowMs, std::string& why)
    {
        PullPlan& pull = plan.pull;
        ClearPlan const& clear = plan.clear;
        // ClearPlanner::Step runs this on every refresh while a pull is going (single pull and
        // clear alike): the map-thread step of the pull. Settle: line of sight pull search.
        if (pull.phase == PullPhase::Settle)
        {
            SearchLosBreak(plan, world, map, nowMs);
            return RecheckResult::NotDue;
        }
        // The tank cannot shoot from its spot (PullBehavior): another spot or another target.
        // Also for `pull now`, which skips the recheck below.
        if (pull.phase == PullPhase::Approach && pull.rangedPullSpell && pull.rangedShot.spotFailed &&
            pull.rangedShot.approachSinceMs == pull.phaseSinceMs)
        {
            HandleSpotFailure(plan, world, map, nowMs);
            return RecheckResult::NotDue;
        }
        if (pull.phase != PullPhase::Approach || !pull.recheckOnApproach || pull.rechecked)
            return RecheckResult::NotDue;

        Creature* target = map->GetCreature(pull.target);
        Player* tank = map->GetPlayer(pull.tank);
        if (!target || !tank || !target->IsAlive() || target->IsInCombat())
        {
            pull.rechecked = true;      // the assigner deals with it (fight started / timeout)
            return RecheckResult::Go;
        }

        auto holdBack = [&pull, nowMs]()
        {
            pull.phase = PullPhase::WaitReady;
            pull.phaseSinceMs = nowMs;
        };

        // Held before (patrol or over the limit) and predicted a moment ago: still held. The
        // first recheck of a pull (nothing held yet) always runs.
        bool const patrolWait = pull.patrolWaitSinceMs != 0;
        if (patrolWait || pull.recheckWaitSinceMs)
        {
            uint32 const interval = patrolWait ? RECHECK_PATROL_WAIT_MS : RECHECK_HOLD_MS;
            // Set by the recheck that started the wait (a new pull starts at 0 with the plan).
            if (pull.lastRecheckMs && WorldTimer::getMSTimeDiff(pull.lastRecheckMs, nowMs) < interval)
            {
                holdBack();
                return RecheckResult::Hold;
            }
        }
        pull.lastRecheckMs = nowMs;

        // 1. Predict again with the planned approach: creatures may have wandered or patrolled
        //    next to the target while the party got ready (combat scenario 13, 3:10).
        //    A ranged pull looks for its shot spot again first (from where the tank stands now);
        //    none: another target, then the whole recheck again for it.
        Position3 standAt = MeleeStandPoint(target, tank);
        if (pull.rangedPullSpell)
        {
            ShotSpot::Spot spot;
            ShotSpot::Failure spotWhy = ShotSpot::Failure::None;
            if (!FindShotSpot(map, target, tank, pull.rangedPullSpell, pull.rangedPullRange, pull.failedSpots, spot, spotWhy))
            {
                ReplaceUnshootableTarget(plan, world, map, tank, target, ShotSpot::FailureName(spotWhy), nowMs);
                holdBack();
                return RecheckResult::Hold;
            }
            pull.rangedSpot = spot.at;
            standAt = spot.at;
        }
        std::vector<AssistPredictor::Joiner> const joiners = AssistPredictor::PredictDetailed(target, tank, &standAt);
        std::vector<ObjectGuid> expected = AssistPredictor::Guids(joiners);
        std::vector<ObjectGuid> aggroAdds = AggroRangeGuids(joiners);
        // A caster opened from range does not come: the tank walks to it afterwards, so the
        // walk-in aggro range counts as well (same as the planner, Evaluate).
        if (pull.rangedPullSpell && IsRangedCaster(target))
        {
            Position3 const meleeAt = MeleeStandPoint(target, tank);
            std::vector<ObjectGuid> const walkIn = AggroRangeGuids(AssistPredictor::PredictDetailed(target, tank, &meleeAt));
            AddUnique(expected, walkIn);
            AddUnique(aggroAdds, walkIn);
        }
        // Stealthed pack members were added by the planner, keep counting them.
        for (ObjectGuid g : pull.expected)
            if (!Contains(expected, g) && IsStealthedPackCreature(world, g))
                expected.push_back(g);

        uint32 const maxMobs = clear.active ? clear.maxMobs : 0;
        if (maxMobs && expected.size() > maxMobs)
        {
            if (!pull.recheckWaitSinceMs)
            {
                pull.recheckWaitSinceMs = nowMs;
                plan.notes.push_back(Format("Holding the pull: %s would now bring %u (limit %u). Waiting for that to change.",
                    target->GetName(), uint32(expected.size()), maxMobs));
            }
            else if (WorldTimer::getMSTimeDiff(pull.recheckWaitSinceMs, nowMs) > ClearPlanner::REPREDICT_WAIT_MS)
            {
                why = Format("still %u after %u seconds", uint32(expected.size()), ClearPlanner::REPREDICT_WAIT_MS / 1000);
                AddUnique(pull.expected, expected);
                return RecheckResult::GiveUp;
            }
            holdBack();
            return RecheckResult::Hold;
        }

        // 2. Patrols about to walk next to the target or the fight spot (scenarios 4, 11, 13).
        Unit* leader = map->GetUnit(world.leader);
        PatrolThreat patrol;
        bool const patrolNear = FindPatrolThreat(world, map, target, tank, leader, pull.spot, expected, patrol);
        // Its loop is shorter than the fight: waiting only loses time. Go now, the fight-time
        // patrol response deals with it when it comes.
        if (patrolNear && !patrol.WaitHelps())
        {
            if (pull.patrolWaitingFor != patrol.patrol->GetObjectGuid())
                plan.notes.push_back(std::string("Patrol ") + patrol.patrol->GetName() + " " + DescribePatrol(patrol) +
                    ". Its loop is shorter than the fight, pulling without waiting.");
            pull.patrolWaitingFor = patrol.patrol->GetObjectGuid();
        }
        else if (patrolNear)
        {
            if (!pull.patrolWaitSinceMs)
                pull.patrolWaitSinceMs = nowMs;
            if (WorldTimer::getMSTimeDiff(pull.patrolWaitSinceMs, nowMs) <= PATROL_WAIT_MAX_MS)
            {
                if (pull.patrolWaitingFor != patrol.patrol->GetObjectGuid())
                {
                    pull.patrolWaitingFor = patrol.patrol->GetObjectGuid();
                    plan.notes.push_back(Format("Waiting for patrol %s to move away (%s): it ", patrol.patrol->GetName(),
                        ClearTime(patrol).c_str()) + DescribePatrol(patrol) + ".");
                }
                holdBack();
                return RecheckResult::Hold;
            }
            if (clear.active)
            {
                why = Format("patrol %s still near after %u seconds", patrol.patrol->GetName(), PATROL_WAIT_MAX_MS / 1000);
                AddUnique(pull.expected, expected);
                return RecheckResult::GiveUp;
            }
            plan.notes.push_back(Format("Patrol %s is still near after %u seconds. Pulling anyway.",
                patrol.patrol->GetName(), PATROL_WAIT_MAX_MS / 1000));
        }
        else if (pull.patrolWaitSinceMs)
            plan.notes.push_back("The patrol has moved away, pulling.");

        if (pull.recheckWaitSinceMs)
            plan.notes.push_back(Format("%s would bring %u now, pulling.", target->GetName(), uint32(expected.size())));
        else if (expected.size() != pull.expected.size())
            plan.notes.push_back(Format("Predicted again: %s now brings %u (was %u): %s.", target->GetName(),
                uint32(expected.size()), uint32(pull.expected.size()), NamesOf(map, expected, &aggroAdds, " (aggro range)").c_str()));
        pull.expected = std::move(expected);
        pull.aggroRangeAdds = std::move(aggroAdds);
        pull.rechecked = true;
        return RecheckResult::Go;
    }

    void PullAssigner::Clear(TeamPlan& plan) const
    {
        plan.pull = PullPlan();
        plan.clear = ClearPlan();
        plan.lastPullOutcome = PullOutcome::None;
    }

    void PullAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        PullPlan& pull = plan.pull;
        if (pull.phase == PullPhase::None)
            return;

        uint32 const now = world.refreshedAtMs;
        uint32 const elapsed = WorldTimer::getMSTimeDiff(pull.phaseSinceMs, now);
        auto enter = [&](PullPhase phase)
        {
            pull.phase = phase;
            pull.phaseSinceMs = now;
        };
        // `pull.expected` is kept: ClearPlanner reads it after an interrupted pull.
        auto finish = [&](PullOutcome outcome, std::string const& text)
        {
            plan.notes.push_back(text);
            pull.phase = PullPhase::None;
            plan.lastPullOutcome = outcome;
        };
        auto cancel = [&](std::string const& why) { finish(PullOutcome::Cancelled, "Pull cancelled: " + why); };
        auto interrupt = [&](std::string const& why)
        {
            finish(PullOutcome::Interrupted, "Pull interrupted: " + why +
                (plan.clear.active ? " Clear goes on after this fight." : ""));
        };

        PartyMemberFacts const* tank = world.FindMember(pull.tank);
        if (!tank || !tank->alive)
        {
            cancel("tank is gone or dead.");
            return;
        }

        bool const fightStarted = std::any_of(pull.expected.begin(), pull.expected.end(),
            [&world](ObjectGuid g) { return world.FindEnemy(g) != nullptr; });

        switch (pull.phase)
        {
            case PullPhase::WaitReady:
            {
                // The player or a patrol started a fight: not a failure of the plan.
                if (fightStarted || world.HasPartyEnemy())
                {
                    interrupt("a fight started.");
                    return;
                }
                std::vector<PullPlanner::Issue> const issues = PullPlanner::CheckReady(world, pull.healerManaPct);
                if (issues.empty())
                {
                    // Announce once: RecheckBeforeApproach may send the pull back here (new
                    // prediction over the limit, patrol coming).
                    if (!pull.readyAnnounced)
                        plan.notes.push_back("Everyone ready, pulling.");
                    pull.readyAnnounced = true;
                    enter(PullPhase::Approach);
                    return;
                }

                // Only bot problems count toward the timeout; players do not eat or drink on
                // their own command, so their state is reported instead.
                bool const botIssue = std::any_of(issues.begin(), issues.end(), [&world](PullPlanner::Issue const& i)
                {
                    PartyMemberFacts const* m = world.FindMember(i.who);
                    return !m || m->isBot;
                });
                if (botIssue && pull.lastReadyCheckMs)
                    pull.botWaitMs += WorldTimer::getMSTimeDiff(pull.lastReadyCheckMs, now);
                pull.lastReadyCheckMs = now;

                if (!pull.readyNoteSent && elapsed > WAIT_READY_NOTE_MS)
                {
                    pull.readyNoteSent = true;
                    plan.notes.push_back("Still waiting for: " + DescribeIssues(issues, world, pull) +
                        (botIssue ? "." : ". (Your own state does not time out.)"));
                }
                if (pull.botWaitMs > WAIT_READY_TIMEOUT_MS)
                    cancel("bots not ready after 90 seconds (.partybot ai ready shows why).");
                return;
            }
            case PullPhase::Approach:
                if (fightStarted)
                {
                    enter(pull.returnToSpot ? PullPhase::Return : PullPhase::Settle);
                    // Opened from range, fought here: wait where the tank shot.
                    if (!pull.returnToSpot && pull.rangedPullSpell)
                    {
                        pull.awaitArrival = true;
                        pull.arrivalPoint = tank->pos;
                        pull.arrivalTimeoutMs = ArrivalTimeoutMs(world, pull, tank->pos);
                    }
                    if (pull.returnToSpot)
                    {
                        // (copies: std::min takes references, the constants have no definition)
                        uint32 const maxMs = RETURN_TIMEOUT_MAX_MS;
                        uint32 const extraMs = RETURN_TIMEOUT_MS;
                        float const walkMs = PackResolver::Distance(tank->pos, pull.spot) / RETURN_BACK_SPEED * 1000.0f;
                        pull.returnTimeoutMs = std::min(maxMs, uint32(walkMs * RETURN_TIME_FACTOR) + extraMs);
                    }
                }
                // Someone else started another fight: the tank must not go on to pull a second pack.
                else if (std::any_of(world.enemies.begin(), world.enemies.end(),
                         // (an ally's fight nearby, e.g. a guard, is not ours)
                         [&pull](EnemyFacts const& e) { return !e.onAllyOnly && IsUnexpected(e, pull); }))
                    interrupt("a fight with other enemies started.");
                else if (elapsed > APPROACH_TIMEOUT_MS)
                    cancel("tank did not reach the target in 20 seconds.");
                else if (pull.rangedPullSpell && elapsed > RANGED_PULL_FALLBACK_MS)
                {
                    pull.rangedPullSpell = 0;
                    plan.notes.push_back(NameIn(pull, world, pull.tank) +
                        " could not open from range (no ammo, line of sight or form?). Walking in instead.");
                }
                return;
            case PullPhase::Return:
                if (PackResolver::Distance(tank->pos, pull.spot) <= RETURN_ARRIVED ||
                    elapsed > std::max<uint32>(pull.returnTimeoutMs, uint32(RETURN_TIMEOUT_MS)))
                    enter(PullPhase::Settle);
                return;
            case PullPhase::Settle:
                // Ranged pull fought in place: the tank stays where it shot until the pack came.
                // Settle's own time starts then (the others hold until SETTLE_MS after it).
                if (pull.awaitArrival)
                {
                    bool const timedOut = elapsed > pull.arrivalTimeoutMs;
                    if (!timedOut && pull.losBreak != PullLosBreak::Active && world.HasPartyEnemy() &&
                        !PackArrived(world, pull, tank->pos))
                        return;
                    pull.awaitArrival = false;
                    pull.phaseSinceMs = now;
                    if (timedOut)
                        plan.notes.push_back(Format("The pack did not reach %s within %.0f seconds, fighting anyway.",
                            NameIn(pull, world, pull.tank).c_str(), pull.arrivalTimeoutMs / 1000.0f));
                    return;
                }
                // Line of sight pull (point found by PullPlanner::SearchLosBreak on the map
                // thread): Settle lasts until the casters came or the timeout.
                if (pull.losBreak == PullLosBreak::Active)
                {
                    bool timedOut = false;
                    if (!LosBreakOver(world, pull, tank->pos, now, timedOut))
                        return;
                    pull.losBreak = PullLosBreak::Ended;
                    if (timedOut)
                        plan.notes.push_back(Format("The caster did not come out within %u seconds, fighting here.",
                            LOS_BREAK_TIMEOUT_MS / 1000));
                }
                if (elapsed >= SETTLE_MS || !world.HasPartyEnemy())
                {
                    pull.phase = PullPhase::None;
                    plan.lastPullOutcome = PullOutcome::Done;
                }
                return;
            case PullPhase::None:
                return;
        }
    }
}
