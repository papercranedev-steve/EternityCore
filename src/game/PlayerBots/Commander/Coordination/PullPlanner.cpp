#include "Commander/Coordination/PullPlanner.h"
#include "Commander/Coordination/ClearPlanner.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/AssistPredictor.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/PatrolTracker.h"
#include "Commander/Perception/ProfileStore.h"
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
#include <mutex>

namespace Commander
{
    namespace
    {
        // Last full RecheckBeforeApproach per tank (PullPlan has no field for it). Several maps
        // update at once, hence the lock. Entries of finished pulls are dropped after a minute.
        struct RecheckStamp
        {
            ObjectGuid tank;
            ObjectGuid target;
            uint32 atMs;
        };
        uint32 const RECHECK_STAMP_KEEP_MS = 60 * 1000;
        std::mutex g_recheckStampsLock;
        std::vector<RecheckStamp> g_recheckStamps;

        bool RecheckedWithin(ObjectGuid tank, ObjectGuid target, uint32 nowMs, uint32 intervalMs)
        {
            std::lock_guard<std::mutex> guard(g_recheckStampsLock);
            for (RecheckStamp const& s : g_recheckStamps)
                if (s.tank == tank)
                    return s.target == target && WorldTimer::getMSTimeDiff(s.atMs, nowMs) < intervalMs;
            return false;
        }

        void StampRecheck(ObjectGuid tank, ObjectGuid target, uint32 nowMs)
        {
            std::lock_guard<std::mutex> guard(g_recheckStampsLock);
            g_recheckStamps.erase(std::remove_if(g_recheckStamps.begin(), g_recheckStamps.end(),
                [tank, nowMs](RecheckStamp const& s)
                {
                    return s.tank == tank || WorldTimer::getMSTimeDiff(s.atMs, nowMs) > RECHECK_STAMP_KEEP_MS;
                }), g_recheckStamps.end());
            g_recheckStamps.push_back({ tank, target, nowMs });
        }

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

        Player* FindTank(Player* leader)
        {
            Group* group = leader->GetGroup();
            for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
            {
                Player* m = itr->getSource();
                if (!m || !m->IsInWorld() || m->GetMap() != leader->GetMap() || !m->IsAlive() || !m->AI())
                    continue;
                if (PartyBotAI const* ai = dynamic_cast<PartyBotAI const*>(m->AI()))
                    if (ai->m_role == ROLE_TANK)
                        return m;
            }
            return nullptr;
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
            std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(c->GetEntry());
            return profile && profile->mainRangedSpellId;
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

        // Stand point of the pull as planned: in range for a ranged pull, else in melee.
        Position3 PullStandPoint(Creature const* target, Unit const* tank, uint32 rangedSpell, float rangedRange)
        {
            float const distance = rangedSpell ? std::max(rangedRange - PullPlanner::RANGED_STAND_MARGIN, 5.0f)
                                               : MeleeStandDistance(target);
            return StandPoint(target, tank, distance);
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

        // One way to pull one candidate.
        struct Option
        {
            Creature* pullTarget = nullptr;
            std::vector<ObjectGuid> expected;
            std::vector<ObjectGuid> aggroAdds;      // subset of expected joining by aggro range
            uint32 elites = 0;
            float distance = 0.0f;
            bool ranged = false;
            std::string rangedWhy;                  // why it opens from range
            // Walking in would bring this many by aggro range and the tank has no ranged pull.
            uint32 meleeAggroAddsNoRanged = 0;
        };

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

        Option Evaluate(WorldModel const& world, Map* map, Creature* c, Player* tank, uint32 rangedSpell, float rangedRange)
        {
            Option o;
            o.pullTarget = c;
            o.distance = tank->GetDistance(c);

            Position3 const meleeAt = PullStandPoint(c, tank, 0, 0.0f);
            std::vector<AssistPredictor::Joiner> const melee = AssistPredictor::PredictDetailed(c, tank, &meleeAt);
            o.expected = AssistPredictor::Guids(melee);
            o.aggroAdds = AggroRangeGuids(melee);

            bool const caster = IsRangedCaster(c);
            Creature const* neighbour = UnpredictedNeighbour(world, map, c, o.expected);
            bool const walkInAdds = !o.aggroAdds.empty();
            if (!rangedSpell)
                o.meleeAggroAddsNoRanged = uint32(o.aggroAdds.size());
            if (rangedSpell && (caster || walkInAdds || neighbour))
            {
                Position3 const rangedAt = PullStandPoint(c, tank, rangedSpell, rangedRange);
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
                if (rangedExpected.size() <= o.expected.size())
                {
                    o.ranged = true;
                    if (caster)
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

        // A patrol that will walk next to the target or the fight spot soon.
        struct PatrolThreat
        {
            Creature* patrol = nullptr;
            bool atTarget = false;      // else at the fight spot
            float enterSec = 0.0f;      // first time within its clearance (0 = there now)
            float clearSec = 0.0f;      // gone for good from then on (= horizon: later or never)
            float clearance = 0.0f;
            PatrolTracker::TrackSource source = PatrolTracker::TrackSource::Standing;
        };

        // Patrols are taken from the pack list (out-of-combat creatures within 50 yd of the
        // leader). The target's own walking group is not a threat to itself.
        bool FindPatrolThreat(WorldModel const& world, Map* map, Creature* target, Unit* tank, Unit* leader,
                              Position3 const& spot, PatrolThreat& out)
        {
            Position3 const targetPos = PositionOf(target);
            PackCreature const* targetFacts = FindPackCreature(world, target->GetObjectGuid());
            bool const targetPatrols = targetFacts && targetFacts->patrol;
            CreatureGroup const* targetGroup = target->GetCreatureGroup();

            bool found = false;
            for (PackFacts const& p : world.packs)
            {
                for (PackCreature const& c : p.members)
                {
                    if (!c.patrol || !c.aggressive || c.noAggro || c.tappedByOther || c.guid == target->GetObjectGuid())
                        continue;
                    Creature* patrol = map->GetCreature(c.guid);
                    if (!patrol || !patrol->IsAlive() || patrol->IsInCombat())
                        continue;
                    if (targetGroup && patrol->GetCreatureGroup() == targetGroup)
                        continue;
                    // A patrolling target walks with its neighbours (distance-linked, no group record).
                    if (targetPatrols && patrol->GetDistance(target) <= PackResolver::LINK_DISTANCE)
                        continue;

                    PatrolTracker::Track const track = PatrolTracker::Predict(patrol, PullPlanner::PATROL_HORIZON_SEC);
                    float const toTank = std::max(PullPlanner::PATROL_MIN_CLEARANCE, patrol->GetAttackDistance(tank));
                    float const toParty = std::max(PullPlanner::PATROL_MIN_CLEARANCE, patrol->GetAttackDistance(leader ? leader : tank));
                    PatrolTracker::Approach const a = PatrolTracker::ClosestApproach(track, targetPos, toTank);
                    PatrolTracker::Approach const b = PatrolTracker::ClosestApproach(track, spot, toParty);
                    if (!a.comesWithin && !b.comesWithin)
                        continue;
                    // Inside for the whole horizon (a short loop around the pack, or waiting at a
                    // node): waiting does not help. If it is close enough to join, the prediction
                    // counts it like any pack member.
                    if (a.alwaysWithin || b.alwaysWithin)
                        continue;

                    PatrolThreat t;
                    t.patrol = patrol;
                    t.source = track.source;
                    t.atTarget = a.comesWithin && (!b.comesWithin || a.enterSec <= b.enterSec);
                    t.enterSec = t.atTarget ? a.enterSec : b.enterSec;
                    t.clearSec = std::max(a.comesWithin ? a.clearSec : 0.0f, b.comesWithin ? b.clearSec : 0.0f);
                    t.clearance = t.atTarget ? toTank : toParty;
                    // Report the one that keeps the pull waiting longest.
                    if (!found || t.clearSec > out.clearSec)
                        out = t;
                    found = true;
                }
            }
            return found;
        }

        // "is within 19 yd of the target (path)" / "comes within 15 yd of the fight spot in about 6 s (moving)"
        std::string DescribePatrol(PatrolThreat const& t)
        {
            char const* where = t.atTarget ? "the target" : "the fight spot";
            std::string text = t.enterSec <= 0.0f
                ? Format("is within %.0f yd of %s", t.clearance, where)
                : Format("comes within %.0f yd of %s in about %.0f s", t.clearance, where, t.enterSec);
            return text + Format(" (%s)", PatrolTracker::SourceName(t.source));
        }

        std::string ClearTime(PatrolThreat const& t)
        {
            if (t.clearSec >= PullPlanner::PATROL_HORIZON_SEC)
                return Format("more than %.0f s", PullPlanner::PATROL_HORIZON_SEC);
            return Format("about %.0f s", std::ceil(t.clearSec));
        }

        bool IsStealthedPackCreature(WorldModel const& world, ObjectGuid guid)
        {
            PackCreature const* c = FindPackCreature(world, guid);
            return c && c->stealthed;
        }
    }

    uint32 PullPlanner::FindRangedPullSpell(Player* tank, float& range)
    {
        range = 0.0f;
        uint32 spellId = 0;
        PartyBotAI const* ai = tank->AI() ? dynamic_cast<PartyBotAI const*>(tank->AI()) : nullptr;

        auto hasAmmo = [tank]()
        {
            uint32 const ammo = tank->GetUInt32Value(PLAYER_AMMO_ID);
            return ammo && tank->HasItemCount(ammo, 1);
        };

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
            bool const healer = m.isBot && m.role == ROLE_HEALER;
            if (healer ? m.UsesMana() : m.manaPct >= 0.0f)
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

        Player* tank = FindTank(leader);
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

        float rangedRange = 0.0f;
        uint32 const rangedSpell = FindRangedPullSpell(tank, rangedRange);

        // Pick the member that brings the fewest creatures, then fewest elites, then nearest to the tank.
        // Each candidate is judged with its best approach (walk in, or open from range).
        Option best;
        uint32 stealthed = 0;
        for (ObjectGuid guid : candidates)
        {
            Creature* c = map->GetCreature(guid);
            if (!c || !c->IsAlive() || c->IsInCombat() || !tank->IsValidAttackTarget(c))
                continue;
            // The tank cannot target a stealthed creature and would walk for 20 seconds.
            PackCreature const* facts = FindMemberFacts(pack, guid);
            if (facts && facts->stealthed)
            {
                ++stealthed;
                continue;
            }

            Option o = Evaluate(world, map, c, tank, rangedSpell, rangedRange);
            bool better = !best.pullTarget ||
                o.expected.size() < best.expected.size() ||
                (o.expected.size() == best.expected.size() && o.elites < best.elites) ||
                (o.expected.size() == best.expected.size() && o.elites == best.elites && o.distance < best.distance);
            if (better)
                best = std::move(o);
        }
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

        // Casters that stand back and cast do not follow the tank: bringing them back is useless.
        Creature const* rangedCaster = nullptr;
        for (ObjectGuid g : best.expected)
        {
            Creature* e = map->GetCreature(g);
            if (e && IsRangedCaster(e))
            {
                rangedCaster = e;
                break;
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
        pull.spot = PositionOf(leader);
        bool const distant = leader->GetDistance(best.pullTarget) > RETURN_MIN_DISTANCE;   // `far` is a Windows macro
        pull.returnToSpot = distant && !rangedCaster;
        pull.phaseSinceMs = WorldTimer::getMSTime();
        pull.phase = skipReady ? PullPhase::Approach : PullPhase::WaitReady;
        pull.healerManaPct = READY_MANA_PCT;
        // `pull now` means now: no second prediction, no waiting for patrols.
        pull.recheckOnApproach = !skipReady;
        if (best.ranged)
        {
            pull.rangedPullSpell = rangedSpell;
            pull.rangedPullRange = rangedRange;
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
            report.push_back(Format("Chosen from %u pack members (fewest joiners).", uint32(candidates.size())));
        if (stealthedAdded)
            report.push_back(Format("Counted %u stealthed creature(s) of the pack.", stealthedAdded));
        if (best.ranged)
        {
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(rangedSpell);
            report.push_back(Format("Ranged pull: %s opens with %s from up to %.0f yd (%s).", tank->GetName(),
                spell ? spell->SpellName[0].c_str() : "?", rangedRange, best.rangedWhy.c_str()));
        }
        else if (best.meleeAggroAddsNoRanged)
            report.push_back(Format("Walking in brings %u by aggro range; %s has no ranged pull (warrior: bow/gun/crossbow "
                "with ammo or a thrown weapon; druid: Faerie Fire (Feral)).", best.meleeAggroAddsNoRanged, tank->GetName()));
        if (pull.returnToSpot)
            report.push_back("Tank brings them back to where you stand.");
        else if (distant && rangedCaster)
            report.push_back(Format("Fight where they stand: %s casts from range and would not follow the tank.",
                rangedCaster->GetName()));
        else
            report.push_back("Fight where they stand.");
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
        if (FindPatrolThreat(world, map, best.pullTarget, tank, leader, pull.spot, patrol))
        {
            std::string const line = std::string("Patrol ") + patrol.patrol->GetName() + " " + DescribePatrol(patrol);
            if (skipReady)
                report.push_back(line + ". 'pull now' does not wait for it.");
            else
                report.push_back(line + Format(". The pull waits until it has moved away (%s, at most %u s).",
                    ClearTime(patrol).c_str(), PATROL_WAIT_MAX_MS / 1000));
        }

        if (!skipReady)
            report.push_back("Waiting until everyone is ready (90% hp, 80% mana, within 40 yd). '.partybot ai pull now' skips this.");
        return true;
    }

    PullPlanner::RecheckResult PullPlanner::RecheckBeforeApproach(TeamPlan& plan, WorldModel const& world, Map* map,
                                                                  uint32 nowMs, std::string& why)
    {
        PullPlan& pull = plan.pull;
        ClearPlan const& clear = plan.clear;
        if (pull.phase != PullPhase::Approach || !pull.recheckOnApproach || pull.rechecked)
            return RecheckResult::NotDue;

        Creature* target = map->GetCreature(pull.target);
        Unit* tank = map->GetUnit(pull.tank);
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
            if (RecheckedWithin(pull.tank, pull.target, nowMs, interval))
            {
                holdBack();
                return RecheckResult::Hold;
            }
        }
        StampRecheck(pull.tank, pull.target, nowMs);

        // 1. Predict again with the planned approach: creatures may have wandered or patrolled
        //    next to the target while the party got ready (combat scenario 13, 3:10).
        Position3 const standAt = PullStandPoint(target, tank, pull.rangedPullSpell, pull.rangedPullRange);
        std::vector<AssistPredictor::Joiner> const joiners = AssistPredictor::PredictDetailed(target, tank, &standAt);
        std::vector<ObjectGuid> expected = AssistPredictor::Guids(joiners);
        std::vector<ObjectGuid> aggroAdds = AggroRangeGuids(joiners);
        // A caster opened from range does not come: the tank walks to it afterwards, so the
        // walk-in aggro range counts as well (same as the planner, Evaluate).
        if (pull.rangedPullSpell && IsRangedCaster(target))
        {
            Position3 const meleeAt = PullStandPoint(target, tank, 0, 0.0f);
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
        if (FindPatrolThreat(world, map, target, tank, leader, pull.spot, patrol))
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
                    enter(pull.returnToSpot ? PullPhase::Return : PullPhase::Settle);
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
                if (PackResolver::Distance(tank->pos, pull.spot) <= RETURN_ARRIVED || elapsed > RETURN_TIMEOUT_MS)
                    enter(PullPhase::Settle);
                return;
            case PullPhase::Settle:
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
