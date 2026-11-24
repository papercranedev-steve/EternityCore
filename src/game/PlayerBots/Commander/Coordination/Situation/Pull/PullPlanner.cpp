#include "Commander/Coordination/Situation/Pull/PullPlanner.h"
#include "Commander/Coordination/Situation/Pull/PullCommon.h"
#include "Commander/Coordination/Situation/Pull/PullChoice.h"
#include "Commander/Coordination/Situation/Pull/PullPatrolCheck.h"
#include "Commander/Coordination/Situation/Pull/PullLosSearch.h"
#include "Commander/Coordination/Situation/ClearPlanner.h"
#include "Commander/Coordination/Situation/Los/LosBreak.h"
#include "Commander/Coordination/Situation/Los/LosHealerSpot.h"
#include "Commander/Coordination/Situation/Patrol/PatrolAssigner.h"
#include "Commander/Coordination/Situation/Pull/PullMethod.h"
#include "Commander/Coordination/Situation/Pull/PullOpening.h"
#include "Commander/Coordination/Situation/Pull/PullControl.h"
#include "Commander/Coordination/Situation/Pull/PullPlace.h"
#include "Commander/Coordination/Doctrine/FightPlace.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/Forecast/AssistPredictor.h"
#include "Commander/Perception/Forecast/FightLoad.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/PatrolTracker.h"
#include "Commander/Perception/Forecast/PullOdds.h"
#include "Commander/Perception/Spatial/ShotSpot.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Values/RestValue.h"
#include "TeamBotAI.h"
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
    using namespace PullDetail;

    namespace
    {
        // The patrol check's hold (PatrolStart::Held) into the plan, for the doctrine (TacticCheck.h).
        void SetPatrolHold(PullPlan& pull, PatrolStart const& patrols)
        {
            PatrolArrival const* held = patrols.Held();
            pull.holdPatrol = held ? held->patrol->GetObjectGuid() : ObjectGuid();
            pull.holdBot = held ? patrols.holdBot : ObjectGuid();
            pull.holdSpellId = held ? patrols.holdSpellId : 0;
        }

        // Approach of an ambush ends by then (PullPlan::ambushTimeoutMs): the predicted arrival
        // plus PullAssigner::AMBUSH_EXTRA_MS.
        uint32 AmbushTimeoutMs(float waitSec)
        {
            return uint32(std::max(0.0f, waitSec) * 1000.0f) + PullAssigner::AMBUSH_EXTRA_MS;
        }

        // Warrior ranged weapon spells (learned with the weapon skill) and Auto Shot.
        uint32 const SPELL_SHOOT_BOW = 2480;
        uint32 const SPELL_SHOOT_GUN = 7918;
        uint32 const SPELL_SHOOT_CROSSBOW = 7919;
        uint32 const SPELL_THROW = 2764;
        uint32 const SPELL_AUTO_SHOT = 75;

        // The puller: the main tank among the bots (Perception/Tanks.h; players cannot be ordered).
        Player* FindTank(WorldModel const& world, Map* map)
        {
            PartyMemberFacts const* tank = Tanks::Main(world, true);
            Player* player = tank ? map->GetPlayer(tank->guid) : nullptr;
            return (player && player->IsAlive()) ? player : nullptr;
        }

        // Healer mana the planned fight needs before the tank goes (PullPlan::healerManaPct): the
        // fight simulated with every healer bot full and the tank at full health; what each one
        // spends (the total shared out) x READY_HEALER_MANA_MARGIN, as a share of the smallest
        // pool. At least READY_HEALER_MIN_MANA_PCT, at most READY_FULL_MANA_PCT (also when not
        // bearable even then). No estimate or no healer bot: READY_MANA_PCT as before.
        // The fight as it starts once everyone is ready: the tank at full health, every healer
        // bot at full mana.
        FightLoad::Input Rested(FightLoad::Input input)
        {
            for (FightLoad::Healer& h : input.healers)
                h.mana = h.maxMana;
            input.tankHealth = input.tankMaxHealth;
            return input;
        }

        // PullPlan::hard at planning: not bearable even rested. No estimate: not hard.
        bool HardEvenRested(FightLoad::Input const& input, bool haveLoad)
        {
            return haveLoad && !FightLoad::Bearable(FightLoad::Simulate(Rested(input), "too hard? (rested)"));
        }

        float HealerManaNeededPct(FightLoad::Input const& load, bool haveLoad)
        {
            if (!haveLoad || load.healers.empty())
                return PullPlanner::READY_MANA_PCT;
            FightLoad::Input const input = Rested(load);
            float smallestPool = 0.0f;
            for (FightLoad::Healer const& h : input.healers)
                if (h.maxMana > 0.0f && (smallestPool <= 0.0f || h.maxMana < smallestPool))
                    smallestPool = h.maxMana;
            if (smallestPool <= 0.0f)
                return PullPlanner::READY_MANA_PCT;
            FightLoad::Result const r = FightLoad::Simulate(input, "healer mana needed (full mana, full tank)");
            if (!FightLoad::Bearable(r))
                return PullPlanner::READY_FULL_MANA_PCT;
            float const spentEach = std::max(0.0f, r.manaStart - r.manaLeft) / float(input.healers.size());
            float const pct = spentEach * PullPlanner::READY_HEALER_MANA_MARGIN / smallestPool * 100.0f;
            return std::min(PullPlanner::READY_FULL_MANA_PCT, std::max(PullPlanner::READY_HEALER_MIN_MANA_PCT, pct));
        }
    }

    uint32 PullPlanner::FindRangedPullSpell(Player* tank, float& range)
    {
        range = 0.0f;
        uint32 spellId = 0;
        TeamBotAI const* ai = tank->AI() ? dynamic_cast<TeamBotAI const*>(tank->AI()) : nullptr;

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
        float const maxRange = CombatBotBaseAI::SpellMaxRange(tank, spell);  // Hawk Eye etc.
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

    namespace
    {
    // The mana percent a pull waits for from `m` (CheckReady). Healers: what the planned fight
    // needs (PullPlan::healerManaPct, HealerManaNeededPct).
    float PullManaPct(PartyMemberFacts const& m, float healerManaPct)
    {
        return (m.isBot && m.role == ROLE_HEALER) ? healerManaPct : PullPlanner::READY_MANA_PCT;
    }

    // CheckReady and CheckReadyToHunt: `manaNeededOf(m)` is the mana percent member m needs.
    template <typename ManaNeeded>
    std::vector<PullPlanner::Issue> FindIssues(WorldModel const& world, ManaNeeded manaNeededOf,
                                               Position3 const* planned)
    {
        using namespace PullPlanner;
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
            float const manaNeeded = manaNeededOf(m);
            // Healers: the mana pool, not manaPct (a shapeshifted healer still heals with it).
            // Others: only while mana is their active power. A feral druid bot stays in bear /
            // cat form and TeamBotAI::StartResting only drinks with mana active, so counting its
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
                // Near the planned fight spot is fine too: healers and ranged take their places
                // there while the party gets ready (stance modules).
                float const d = PackResolver::Distance(m.pos, leader->pos);
                if (d > READY_MAX_DISTANCE && !(planned && PackResolver::Distance(m.pos, *planned) <= READY_MAX_DISTANCE))
                    issues.push_back({ m.guid, "far", d });
            }
        }
        return issues;
    }
    }

    std::vector<PullPlanner::Issue> PullPlanner::CheckReady(WorldModel const& world, float healerManaPct,
                                                            Position3 const* planned)
    {
        return FindIssues(world, [healerManaPct](PartyMemberFacts const& m)
        {
            return PullManaPct(m, healerManaPct);
        }, planned);
    }

    std::vector<PullPlanner::Issue> PullPlanner::CheckReadyToHunt(WorldModel const& world)
    {
        // Bots: the share a new drink starts below (PartyMemberFacts::drinkStartShare), the
        // same line RestBehavior drinks by when no pull waits.
        return FindIssues(world, [](PartyMemberFacts const& m)
        {
            return (m.isBot && m.drinkStartShare > 0.0f) ? m.drinkStartShare * 100.0f : READY_MANA_PCT;
        }, nullptr);
    }

    bool PullPlanner::Timeline(WorldModel const& world, PullPlan const& pull, PullTimeline::Result& out)
    {
        if (pull.phase == PullPhase::None)
            return false;
        PartyMemberFacts const* tank = world.FindMember(pull.tank);
        if (!tank)
            return false;
        PullTimeline::Input in;
        in.tankAt = tank->pos;
        in.packAt = pull.packAt;
        in.returnToSpot = pull.returnToSpot;
        in.fightSpot = pull.spot;
        if (pull.ambush)
        {
            in.opener = PullTimeline::Opener::Ambush;
            in.openFrom = pull.faceSpot;
            in.ambushWaitSec = pull.ambushWaitSec;
        }
        else if (pull.rangedPullSpell)
        {
            in.opener = PullTimeline::Opener::Shot;
            in.openFrom = pull.rangedSpot;
        }
        else if (pull.facePull)
        {
            in.opener = PullTimeline::Opener::Face;
            in.openFrom = pull.faceSpot;
        }
        else
        {
            in.opener = PullTimeline::Opener::WalkIn;
            in.openFrom = pull.packAt;
            in.charges = !pull.returnToSpot && tank->classId == CLASS_WARRIOR && tank->level >= CHARGE_LEVEL;
            if (RouteFact const* route = world.FindRoute(pull.tank, pull.target))
                if (route->pathYd > 0.0f)
                    in.pathYd = route->pathYd;
        }
        out = PullTimeline::Of(in);
        return true;
    }

    bool PullPlanner::EarlyGoAllowed(PullPlan const& pull)
    {
        return (!pull.ambush || pull.ambushWaitSec >= 0.0f) && !pull.earlyHeldBack &&
            pull.healerManaPct < READY_FULL_MANA_PCT;
    }

    void PullPlanner::RefreshAmbush(TeamPlan& plan, WorldModel const& world, Map* map, uint32 nowMs)
    {
        PullPlan& pull = plan.pull;
        if (pull.phase != PullPhase::WaitReady || !pull.ambush)
            return;
        if (pull.ambushRefreshedMs && WorldTimer::getMSTimeDiff(pull.ambushRefreshedMs, nowMs) < AMBUSH_REFRESH_MS)
            return;
        pull.ambushRefreshedMs = nowMs;
        Creature* patrol = map->GetCreature(pull.target);
        Player* tank = map->GetPlayer(pull.tank);
        PatrolFacts const* facts = FindPatrol(world, pull.target);
        Option const o = patrol && tank && facts && patrol->IsAlive() && !patrol->IsInCombat()
            ? EvaluateAmbush(world, map, patrol, *facts, tank) : Option();
        if (!o.ambush)
        {
            pull.ambushWaitSec = -1.0f;
            return;
        }
        pull.faceSpot = o.spot.at;
        pull.packAt = o.meetAt;
        pull.ambushWaitSec = o.waitSec;
        pull.ambushTimeoutMs = AmbushTimeoutMs(o.waitSec);
    }

    float PullPlanner::ReadyManaShare(PullPlan const& pull, PartyMemberFacts const& m)
    {
        // Went early: until the pull is over, a drink that ran out starts again up to what
        // the go counted on (else only below the bot's own drink start share).
        bool const waits = pull.phase == PullPhase::WaitReady || (pull.wentEarly && pull.phase != PullPhase::None);
        if (!waits || !m.isBot)
            return 0.0f;
        return (m.role == ROLE_HEALER ? pull.healerManaPct : READY_MANA_PCT) / 100.0f;
    }

    bool PullPlanner::ForecastReady(WorldModel const& world, PullPlan const& pull, std::vector<Issue> const& issues,
                                    ReadyForecast& out)
    {
        out = ReadyForecast();
        if (!Timeline(world, pull, out.timeline))
            return false;
        bool known = true;
        float goIn = 0.0f;
        bool const credit = EarlyGoAllowed(pull);
        for (Issue const& issue : issues)
        {
            ReadyWait w;
            w.who = issue.who;
            w.what = issue.what;
            PartyMemberFacts const* m = world.FindMember(issue.who);
            bool const hp = !strcmp(issue.what, "hp");
            bool const mana = !strcmp(issue.what, "mana");
            if (m && m->isBot && (hp || mana))
            {
                w.readySec = hp ? RestValue::SecondsToHealth(*m, float(m->maxHealth) * READY_HEALTH_PCT / 100.0f)
                                : RestValue::SecondsToMana(*m, float(m->maxMana) * PullManaPct(*m, pull.healerManaPct) / 100.0f);
                // Credit only for a rest that is on (a bot still walking to its place may not
                // sit down in time), and never below a healer's floor.
                bool const resting = hp ? m->eating : m->drinking;
                bool const belowFloor = mana && m->role == ROLE_HEALER &&
                    issue.value < pull.healerManaPct / READY_HEALER_MANA_MARGIN;
                // The opening control and the patrol hold act at the opener (the controller
                // walks to its spot before it).
                bool const actsAtOpener = m->guid == pull.control.bot || m->guid == pull.holdBot;
                if (credit && m->guid != pull.tank && !actsAtOpener && resting && !belowFloor)
                    w.neededAtSec = EARLY_GO_TIME_SHARE *
                        (m->HealsTeam() ? out.timeline.contactSec : out.timeline.damageSec);
            }
            if (w.readySec < 0.0f)
                known = false;
            else
                goIn = std::max(goIn, w.readySec - w.neededAtSec);
            out.waits.push_back(w);
        }
        out.goInSec = known ? goIn : -1.0f;
        return true;
    }

    bool PullPlanner::Plan(Player* leader, PartyCommander& commander, Creature* selected, bool skipReady,
                           std::vector<std::string>& report)
    {
        // Everything the planning calculates, kept for the recorder until the next planning.
        Trace::Use trace(&commander.GetTrace(), WorldTimer::getMSTime());
        Trace::Scope scope("pull", "pull");
        scope.Label("plan%s", selected ? Trace::Format(" (selected %s)", selected->GetName()).c_str() : "");
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
            // A selected patrol is that patrol (ambushed on its way), not the pack it walks
            // through now (a clear's patrol first, FindPatrolFirst; the player selecting it).
            if (FindPatrol(world, selected->GetObjectGuid()))
                pack = nullptr;
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
        // The leader's pull method (PullMethod.h): melee never opens from a distance, ranged
        // only shoots, face only face pulls, auto may do either.
        PullMethod const method = sCommanderMgr.GetPullMethod(leader->GetObjectGuid());
        bool const forceRanged = method == PullMethod::Ranged;
        float rangedRange = 0.0f;
        bool const mayShoot = method == PullMethod::Auto || method == PullMethod::Ranged;
        uint32 const rangedSpell = mayShoot ? FindRangedPullSpell(tank, rangedRange) : 0;
        if (!rangedSpell && mayShoot)
        {
            std::string const ammoNote = MissingAmmoNote(tank);
            if (!ammoNote.empty())
                notes.push_back(ammoNote);
            else if (forceRanged)
                notes.push_back(Format("You chose ranged pulls, but %s has no ranged pull: walking in.", tank->GetName()));
        }
        Openers openers;
        openers.shotSpell = rangedSpell;
        openers.shotRange = rangedRange;
        openers.face = method == PullMethod::Auto || method == PullMethod::Face;
        openers.forced = forceRanged || method == PullMethod::Face;

        // Each candidate is judged with its best approach (walk in, open from a shot or face
        // pull spot, ambush a patrol on its way), then ranked (Better).
        uint32 stealthed = 0;
        std::vector<std::string> passed;    // patrols without an ambush spot
        Option best;
        if (selected)
            best = ChooseOption(world, map, pack, candidates, std::vector<ObjectGuid>(), tank, openers, stealthed, &passed);
        else
        {
            // The nearest pack that has a pull. A patrol passing far off has no ambush spot
            // now: the pack behind it is pulled instead (its patrol check still counts it).
            uint32 tapped = 0;
            uint32 bosses = 0;
            bool anyHostile = false;
            for (PackFacts const& p : world.packs)      // sorted nearest first
            {
                // No-aggro creatures never start a fight, so they do not make a pack worth pulling.
                bool const hostile = AnyMember(p, [](PackCreature const& c) { return c.aggressive && !c.noAggro; });
                if (!hostile)
                    continue;
                anyHostile = true;
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
                std::vector<ObjectGuid> members;
                for (PackCreature const& m : p.members)
                    members.push_back(m.guid);
                Option o = ChooseOption(world, map, &p, members, std::vector<ObjectGuid>(), tank, openers, stealthed, &passed);
                if (!o.pullTarget)
                    continue;
                pack = &p;
                candidates = std::move(members);
                best = std::move(o);
                break;
            }
            if (bosses)
                notes.push_back(Format("Passed over %u pack(s) with a boss; select the boss to pull it.", bosses));
            if (tapped)
                notes.push_back(Format("Passed over %u pack(s) someone else is fighting.", tapped));
            if (!anyHostile)
            {
                report.push_back("No hostile pack within 50 yards.");
                report.insert(report.end(), notes.begin(), notes.end());
                return false;
            }
        }
        notes.insert(notes.end(), passed.begin(), passed.end());
        if (!best.pullTarget)
        {
            report.push_back(stealthed ? "Only stealthed creatures left; the tank cannot target them."
                                       : selected ? "No pullable creature in that pack." : "No pullable pack nearby.");
            report.insert(report.end(), notes.begin(), notes.end());
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
        FightLoad::Input load;
        bool const haveLoad = FightLoad::Build(world, map, best.expected, load);
        FightLoad::Result const estimate = haveLoad ? FightLoad::Simulate(load, "estimate: hard fight?") : FightLoad::Result();
        // Where to fight is the doctrine's (Coordination/Doctrine/FightPlace.h); this carries it out.
        FightPlace::Input placeIn;
        placeIn.wanted = sCommanderMgr.GetPullPlace(leader->GetObjectGuid());
        placeIn.distant = distant;
        placeIn.fightThere = fightThere;
        placeIn.packRisk = packRisk.total;
        placeIn.spotRisk = spotRisk.total;
        placeIn.haveEstimate = haveLoad;
        placeIn.estimate = estimate;
        FightPlace::Choice const place = FightPlace::Choose(placeIn);
        // An ambushed patrol is fought where it meets the tank (the spot is near the party).
        bool const returnToSpot = !best.ambush && place.place == FightPlace::Kind::BringBack;
        bool const corner = !best.ambush && place.place == FightPlace::Kind::Corner;

        // Bringing them back: open from range when the tank can (and that brings no more).
        ShotSpot::Spot returnSpot;
        ShotSpot::Failure returnSpotWhy = ShotSpot::Failure::None;
        if (returnToSpot && rangedSpell && !best.ranged && !best.face && !best.noSpot &&
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
        pull.packAt = best.ambush ? best.meetAt : PositionOf(best.pullTarget);
        if (haveLoad && estimate.fightSec > 0.0f)
        {
            pull.expectedIncomingDps = estimate.damage / estimate.fightSec;
            pull.expectedFightSec = estimate.fightSec;
            pull.expectedPartyDps = load.partyDps;
            pull.expectedDpsSource = Value::CapacityDpsSource(world);
            pull.expectedManaStart = estimate.manaStart;
            pull.expectedManaLeft = estimate.manaLeft;
            pull.expectedLowestTankPct = estimate.lowestTankPct;
        }
        pull.phaseSinceMs = WorldTimer::getMSTime();
        pull.plannedAtMs = pull.phaseSinceMs;
        pull.phase = skipReady ? PullPhase::Approach : PullPhase::WaitReady;
        pull.healerManaPct = HealerManaNeededPct(load, haveLoad);
        pull.hard = HardEvenRested(load, haveLoad);
        // `pull now` means now: no second prediction, no waiting for patrols.
        pull.recheckOnApproach = !skipReady;
        if (best.ambush)
        {
            pull.facePull = true;
            pull.faceSpot = best.spot.at;
            pull.ambush = true;
            pull.ambushTimeoutMs = AmbushTimeoutMs(best.waitSec);
            pull.ambushWaitSec = best.waitSec;
        }
        else if (best.ranged)
        {
            pull.rangedPullSpell = rangedSpell;
            pull.rangedPullRange = rangedRange;
            pull.rangedSpot = best.spot.at;
        }
        else if (best.face)
        {
            pull.facePull = true;
            pull.faceSpot = best.spot.at;
        }
        pull.corner = corner;
        pull.placeWhy = place.why;
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
        if (best.ambush)
            report.push_back(Format("Ambush: %s patrols. %s waits on its way %.0f yd from you; it comes by in about %.0f s.",
                best.pullTarget->GetName(), tank->GetName(), PackResolver::Distance(best.spot.at, spot), best.waitSec));
        else if (best.ranged)
        {
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(rangedSpell);
            std::string const clearance = best.spot.clearance < ShotSpot::NO_CREATURE_CLEARANCE
                ? Format(", %.0f yd outside the nearest aggro range", best.spot.clearance) : std::string();
            report.push_back(Format("Ranged pull: %s opens with %s from %.0f yd%s (%s).",
                tank->GetName(), spell ? spell->SpellName[0].c_str() : "?", best.spot.range, clearance.c_str(),
                best.rangedWhy.c_str()));
        }
        else if (best.face)
        {
            std::string const clearance = best.spot.clearance < ShotSpot::NO_CREATURE_CLEARANCE
                ? Format(", %.0f yd outside the nearest other aggro range", best.spot.clearance) : std::string();
            report.push_back(Format("Face pull: %s stands %.0f yd from %s, inside its aggro range%s, and lets it come (%s).",
                tank->GetName(), best.spot.range, best.pullTarget->GetName(), clearance.c_str(), best.rangedWhy.c_str()));
        }
        else if (best.noSpot)
            report.push_back(Format("Walking in: no spot to %s %s from (%s).",
                rangedSpell && openers.face ? "shoot or face pull" : rangedSpell ? "shoot" : "face pull",
                best.pullTarget->GetName(), ShotSpot::FailureName(best.noSpotWhy)));
        else if (method == PullMethod::Melee)
            report.push_back(best.meleeAggroAddsNoRanged
                ? Format("Walking in (you chose melee pulls); that brings %u by aggro range.", best.meleeAggroAddsNoRanged)
                : std::string("Walking in (you chose melee pulls)."));
        else if (best.meleeAggroAddsNoRanged)
            report.push_back(Format("Walking in brings %u by aggro range; %s has no ranged pull (warrior: bow/gun/crossbow "
                "with ammo or a thrown weapon; druid: Faerie Fire (Feral)).", best.meleeAggroAddsNoRanged, tank->GetName()));
        std::string const odds = PullOdds::Describe(map, edge, packRisk);
        if (pull.ambush)
        {
            // (fought where it meets the tank: said above)
        }
        else if (pull.returnToSpot)
            report.push_back(Format("Tank brings them back to where you stand, walking backwards (%s; at your spot %.1f).",
                odds.c_str(), spotRisk.total));
        else if (!distant)
            report.push_back("Fight where they stand (they are next to you).");
        else if (fightThere)
            report.push_back(Format("Fight where they stand: the party can take an add (%s).", odds.c_str()));
        else
            report.push_back(Format("Fight where they stand: your spot is no safer (%s; at your spot %.1f).",
                odds.c_str(), spotRisk.total));
        if (pull.corner)
            report.push_back(Format("Corner: once they come, %s steps out of their sight so they walk round to it (%s).",
                tank->GetName(), place.why.c_str()));
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

        // How hard the fight is (FightLoad), then the patrols: reported now, waited for when the
        // tank is about to go (RecheckBeforeApproach).
        FightLoad::Input fightLoad;
        if (FightLoad::Build(world, map, pull.expected, fightLoad))
        {
            report.push_back("Fight estimate: " + FightLoad::Describe(FightLoad::Simulate(fightLoad, "estimate: report")) +
                Format(", party damage %.0f/s (%s).", fightLoad.partyDps, Value::CapacityDpsSource(world)));
            if (pull.hard)
                report.push_back("Too hard even when everyone is ready: " + FightLoad::Describe(FightLoad::Simulate(Rested(fightLoad))) + ".");
        }
        PatrolStart const patrols = ChooseStart(world, map, best.pullTarget, tank, leader, pull.spot, pull.expected,
                                                PATROL_WAIT_MAX_MS / 1000.0f);
        SetPatrolHold(pull, patrols);
        if (patrols.considered)
        {
            if (skipReady)
                report.push_back("Patrol " + DescribePatrol(patrols) + ": " + FightLoad::Describe(patrols.now) +
                    ". 'pull now' does not wait for it.");
            else
                report.push_back(PatrolVerdict(patrols, "The pull waits"));
        }

        if (!skipReady)
            report.push_back(Format("Waiting until everyone is ready (%.0f%% hp, healers %.0f%% mana for this fight, "
                "others %.0f%%, within %.0f yd). '.partybot ai pull now' skips this.",
                READY_HEALTH_PCT, pull.healerManaPct, READY_MANA_PCT, READY_MAX_DISTANCE));
        pull.report = report;
        return true;
    }

    ObjectGuid PullPlanner::FindPatrolFirst(TeamPlan const& plan, WorldModel const& world, Map* map, Player* tank,
                                            Creature* target, std::vector<ObjectGuid> const& expected)
    {
        Trace::Scope scope("pull", "patrolfirst");
        scope.Label("patrol first?");
        std::vector<Creature*> candidates;
        auto add = [&candidates](Creature* c)
        {
            if (c && c->IsAlive() && !c->IsInCombat() &&
                std::find(candidates.begin(), candidates.end(), c) == candidates.end())
                candidates.push_back(c);
        };
        // Predicted to come now (walking past the pack), or walking next to the pull later.
        for (ObjectGuid g : expected)
            if (FindPatrol(world, g))
                add(map->GetCreature(g));
        PatrolStart const joining = ChooseStart(world, map, target, tank, map->GetUnit(world.leader), plan.pull.spot,
                                                expected, 0.0f);
        for (PatrolArrival const& a : joining.patrols)
        {
            // (one coming by only after the fight is not part of it)
            float const arrive = a.ArriveAfter(0.0f);
            if (arrive >= 0.0f && arrive <= joining.fightSec)
                add(a.patrol);
        }

        for (Creature* patrol : candidates)
        {
            ObjectGuid const guid = patrol->GetObjectGuid();
            PatrolFacts const* facts = FindPatrol(world, guid);
            if (!facts || Contains(plan.clear.patrolFirstTried, guid) || !tank->IsValidAttackTarget(patrol))
                continue;
            Option const o = EvaluateAmbush(world, map, patrol, *facts, tank);
            if (!o.ambush)
                continue;
            // Alone: where it meets the tank it brings nobody of the planned pull.
            bool const alone = std::none_of(o.expected.begin(), o.expected.end(), [&](ObjectGuid g)
            {
                return g != guid && Contains(expected, g);
            });
            if (!alone)
                continue;
            // The fight starts when it comes by: healers drinking now have that long (counted
            // at the plain out-of-combat regeneration, below what a drink gives).
            FightLoad::Input load;
            if (!FightLoad::Build(world, map, o.expected, load) ||
                !FightLoad::Bearable(FightLoad::Simulate(FightLoad::AfterWait(load, o.waitSec), "patrol first: alone")))
                continue;
            return guid;
        }
        return ObjectGuid();
    }

    PullPlanner::RecheckResult PullPlanner::RecheckBeforeApproach(TeamPlan& plan, WorldModel const& world, Map* map,
                                                                  uint32 nowMs, std::string& why, ObjectGuid* patrolFirst)
    {
        Trace::Scope scope("pull", "pullcheck");
        scope.Label("recheck before the tank goes");
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
        // Opening control (PullControl.h), once the expected list is final for this Approach
        // (`pull now` skips the recheck). The tank holds its opener until it is decided.
        if (pull.phase == PullPhase::Approach && !pull.control.decided && (!pull.recheckOnApproach || pull.rechecked))
            PullControl::Choose(plan, world, map, nowMs);
        // An ambush waiting for the party: the patrol walks on meanwhile (early go counts from
        // its arrival).
        RefreshAmbush(plan, world, map, nowMs);
        if (pull.phase != PullPhase::Approach || !pull.recheckOnApproach || pull.rechecked)
            return RecheckResult::NotDue;

        Creature* target = map->GetCreature(pull.target);
        Player* tank = map->GetPlayer(pull.tank);
        if (!target || !tank || !target->IsAlive() || target->IsInCombat())
        {
            pull.rechecked = true;      // the assigner deals with it (fight started / timeout)
            pull.control.decided = true;    // no opening control for a fight already going
            return RecheckResult::Go;
        }

        auto holdBack = [&pull, nowMs]()
        {
            pull.phase = PullPhase::WaitReady;
            pull.phaseSinceMs = nowMs;
        };

        // Held before (patrol or over the limit) and predicted a moment ago: still held. The
        // first recheck of a pull (nothing held yet) always runs.
        bool const patrolWait = pull.patrolWaitSinceMs != 0 || pull.ambushWaitSinceMs != 0;
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
        std::vector<ObjectGuid> expected;
        std::vector<ObjectGuid> aggroAdds;
        Position3 standAt = MeleeStandPoint(target, tank);
        if (pull.ambush)
        {
            // An ambushed patrol walked on while the party got ready: its spot again.
            PatrolFacts const* facts = FindPatrol(world, pull.target);
            Option const o = facts ? EvaluateAmbush(world, map, target, *facts, tank) : Option();
            if (!o.ambush)
            {
                char const* noWhy = facts ? o.noAmbushWhy : "it does not patrol any more";
                if (!pull.ambushWaitSinceMs)
                {
                    pull.ambushWaitSinceMs = nowMs;
                    plan.notes.push_back(Format("No spot on %s's way now (%s): waiting for it to come round.",
                        target->GetName(), noWhy));
                }
                else if (WorldTimer::getMSTimeDiff(pull.ambushWaitSinceMs, nowMs) > PATROL_WAIT_MAX_MS)
                {
                    if (clear.active)
                    {
                        why = Format("no spot on patrol %s's way for %u seconds", target->GetName(),
                            PATROL_WAIT_MAX_MS / 1000);
                        AddUnique(pull.expected, std::vector<ObjectGuid>{ pull.target });
                        return RecheckResult::GiveUp;
                    }
                    plan.notes.push_back(Format("Pull cancelled: no spot on %s's way for %u seconds (%s).",
                        target->GetName(), PATROL_WAIT_MAX_MS / 1000, noWhy));
                    pull.phase = PullPhase::None;
                    plan.lastPullOutcome = PullOutcome::Cancelled;
                    return RecheckResult::NotDue;
                }
                holdBack();
                return RecheckResult::Hold;
            }
            if (pull.ambushWaitSinceMs)
                plan.notes.push_back(Format("%s comes by in about %.0f s: ambush.", target->GetName(), o.waitSec));
            pull.ambushWaitSinceMs = 0;
            pull.faceSpot = o.spot.at;
            pull.packAt = o.meetAt;
            pull.ambushTimeoutMs = AmbushTimeoutMs(o.waitSec);
            pull.ambushWaitSec = o.waitSec;
            expected = o.expected;
        }
        else
        {
            if (pull.rangedPullSpell)
            {
                ShotSpot::Spot spot;
                ShotSpot::Failure spotWhy = ShotSpot::Failure::None;
                if (!FindShotSpot(map, target, tank, pull.rangedPullSpell, pull.rangedPullRange, pull.failedSpots, spot,
                                  spotWhy))
                {
                    ReplaceUnshootableTarget(plan, world, map, tank, target, ShotSpot::FailureName(spotWhy), nowMs);
                    holdBack();
                    return RecheckResult::Hold;
                }
                pull.rangedSpot = spot.at;
                standAt = spot.at;
            }
            else if (pull.facePull)
            {
                ShotSpot::Spot spot;
                ShotSpot::Failure spotWhy = ShotSpot::Failure::None;
                if (FindFaceSpot(world, map, target, tank, std::vector<Position3>(), spot, spotWhy))
                {
                    pull.faceSpot = spot.at;
                    standAt = spot.at;
                }
                else
                {
                    pull.facePull = false;
                    plan.notes.push_back(Format("No spot to face pull %s from now (%s): walking in.", target->GetName(),
                        ShotSpot::FailureName(spotWhy)));
                }
            }
            std::vector<AssistPredictor::Joiner> const joiners = AssistPredictor::PredictDetailed(target, tank, &standAt);
            expected = AssistPredictor::Guids(joiners);
            aggroAdds = AggroRangeGuids(joiners);
            // A caster opened from a distance does not come: the tank walks to it afterwards, so
            // the walk-in aggro range counts as well (same as the planner, Evaluate).
            if (pull.OpensFromAfar() && IsRangedCaster(target))
            {
                Position3 const meleeAt = MeleeStandPoint(target, tank);
                std::vector<ObjectGuid> const walkIn = AggroRangeGuids(AssistPredictor::PredictDetailed(target, tank, &meleeAt));
                AddUnique(expected, walkIn);
                AddUnique(aggroAdds, walkIn);
            }
            // A walk-in is predicted for where the target stands now: the walk guard measures
            // from here (PullAssigner::TARGET_MOVED_YD).
            if (!pull.OpensFromAfar())
                pull.packAt = PositionOf(target);
        }
        // Stealthed pack members were added by the planner, keep counting them.
        for (ObjectGuid g : pull.expected)
            if (!Contains(expected, g) && IsStealthedPackCreature(world, g))
                expected.push_back(g);

        // A patrol that would join can be taken alone first: the clear pulls it, then this pack.
        if (patrolFirst && clear.active && !pull.ambush)
        {
            ObjectGuid const patrol = FindPatrolFirst(plan, world, map, tank, target, expected);
            if (!patrol.IsEmpty())
            {
                *patrolFirst = patrol;
                return RecheckResult::PatrolFirst;
            }
        }

        // The fight with the party as it is now (everyone ready), patrols left to step 2.
        FightLoad::Input nowLoad;
        bool const hardNow = FightLoad::Build(world, map, expected, nowLoad) &&
            !FightLoad::Bearable(FightLoad::Simulate(nowLoad, "too hard? (before the tank goes)"));

        // A clear holds a pull that is too hard (or over a limit the player gave) and gives it
        // up after REPREDICT_WAIT_MS. A pack the player said yes to (ClearPlan::approved) is
        // never held for it; a single pull is his order and goes (PullPlan::hard warns).
        bool const approved = std::any_of(pull.expected.begin(), pull.expected.end(),
            [&clear](ObjectGuid g) { return Contains(clear.approved, g); });
        bool const judged = clear.active && !approved;
        bool const overLimit = judged && clear.maxMobs && expected.size() > clear.maxMobs;
        if (overLimit || (judged && hardNow))
        {
            std::string const what = overLimit
                ? Format("would now bring %u (limit %u)", uint32(expected.size()), clear.maxMobs)
                : Format("is too hard now (%s)", FightLoad::Describe(FightLoad::Simulate(nowLoad)).c_str());
            if (!pull.recheckWaitSinceMs)
            {
                pull.recheckWaitSinceMs = nowMs;
                plan.notes.push_back(Format("Holding the pull: %s %s. Waiting for that to change.",
                    target->GetName(), what.c_str()));
            }
            else if (WorldTimer::getMSTimeDiff(pull.recheckWaitSinceMs, nowMs) > ClearPlanner::REPREDICT_WAIT_MS)
            {
                why = Format("%s after %u seconds", overLimit ? Format("still %u", uint32(expected.size())).c_str()
                                                              : "still too hard", ClearPlanner::REPREDICT_WAIT_MS / 1000);
                AddUnique(pull.expected, expected);
                return overLimit ? RecheckResult::GiveUp : RecheckResult::TooHard;
            }
            holdBack();
            return RecheckResult::Hold;
        }

        // 2. Patrols walking next to the pull while it lasts, joiners walking past included
        //    (scenarios 4, 11, 13; strat-20261103-013312): wait for the start the fight
        //    estimate likes (ChooseStart), within what is left of PATROL_WAIT_MAX_MS.
        Unit* leader = map->GetUnit(world.leader);
        uint32 const waitedMs = pull.patrolWaitSinceMs ? WorldTimer::getMSTimeDiff(pull.patrolWaitSinceMs, nowMs) : 0;
        float const budgetSec = waitedMs >= PATROL_WAIT_MAX_MS ? 0.0f : (PATROL_WAIT_MAX_MS - waitedMs) / 1000.0f;
        PatrolStart const patrols = ChooseStart(world, map, target, tank, leader, pull.spot, expected, budgetSec);
        ObjectGuid const mainPatrol = patrols.Main() ? patrols.Main()->patrol->GetObjectGuid() : ObjectGuid();
        SetPatrolHold(pull, patrols);
        if (patrols.Held())
        {
            // Pulled now, the patrol held when it comes (the doctrine's hold, TacticCheck.h).
            if (pull.patrolWaitingFor != mainPatrol || pull.patrolWaitSinceMs)
                plan.notes.push_back(PatrolVerdict(patrols, "Waiting"));
            pull.patrolWaitingFor = mainPatrol;
        }
        else if (patrols.considered && patrols.startSec > 0.0f)
        {
            if (!pull.patrolWaitSinceMs)
                pull.patrolWaitSinceMs = nowMs;
            if (pull.patrolWaitingFor != mainPatrol)
            {
                pull.patrolWaitingFor = mainPatrol;
                plan.notes.push_back(PatrolVerdict(patrols, "Waiting"));
            }
            holdBack();
            return RecheckResult::Hold;
        }
        else if (patrols.considered && !patrols.bearableNow)
        {
            // No start within the wait is bearable (waited the whole time, or waiting does not
            // help): too hard for the clear (it skips or asks).
            if (judged)
            {
                why = waitedMs >= PATROL_WAIT_MAX_MS
                    ? Format("patrol %s still near after %u seconds", patrols.Main()->patrol->GetName(), PATROL_WAIT_MAX_MS / 1000)
                    : Format("too hard with patrol %s joining", patrols.Main()->patrol->GetName());
                AddUnique(pull.expected, expected);
                return RecheckResult::TooHard;
            }
            if (pull.patrolWaitingFor != mainPatrol || pull.patrolWaitSinceMs)
                plan.notes.push_back(PatrolVerdict(patrols, "Waiting") + " Pulling.");
            pull.patrolWaitingFor = mainPatrol;
        }
        else if (patrols.considered)
        {
            if (pull.patrolWaitingFor != mainPatrol || pull.patrolWaitSinceMs)
                plan.notes.push_back(PatrolVerdict(patrols, "Waiting"));
            pull.patrolWaitingFor = mainPatrol;
        }
        else if (pull.patrolWaitSinceMs)
            plan.notes.push_back("The patrol has moved away, pulling.");

        // The fight the tank goes into (the patrols' verdict when one joins).
        pull.hard = !patrols.considered ? hardNow
                  : patrols.Held()      ? !patrols.bearableAtStart
                                        : !patrols.bearableNow;
        if (pull.recheckWaitSinceMs)
            plan.notes.push_back(Format("%s would bring %u now and the party can carry it, pulling.", target->GetName(),
                uint32(expected.size())));
        else if (expected.size() != pull.expected.size())
            plan.notes.push_back(Format("Predicted again: %s now brings %u (was %u): %s.", target->GetName(),
                uint32(expected.size()), uint32(pull.expected.size()), NamesOf(map, expected, &aggroAdds, " (aggro range)").c_str()));
        pull.expected = std::move(expected);
        pull.aggroRangeAdds = std::move(aggroAdds);
        pull.rechecked = true;
        PullControl::Choose(plan, world, map, nowMs);
        return RecheckResult::Go;
    }
}
