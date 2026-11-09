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
    using namespace PullDetail;

    namespace
    {
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

        // Auto fight place: a corner when the estimate says the tank drops low or the healers
        // run dry (PullPlanner::CORNER_*), or it is not bearable at all.
        bool IsHardFight(FightLoad::Result const& r)
        {
            if (!FightLoad::Bearable(r) || r.lowestTankPct < PullPlanner::CORNER_TANK_PCT)
                return true;
            return r.manaStart > 0.0f && r.manaLeft < r.manaStart * PullPlanner::CORNER_MANA_LEFT_SHARE;
        }

        // Healer mana the planned fight needs before the tank goes (PullPlan::healerManaPct): the
        // fight simulated with every healer bot full and the tank at full health; what each one
        // spends (the total shared out) x READY_HEALER_MANA_MARGIN, as a share of the smallest
        // pool. At least READY_HEALER_MIN_MANA_PCT, at most READY_FULL_MANA_PCT (also when not
        // bearable even then). No estimate or no healer bot: READY_MANA_PCT as before.
        float HealerManaNeededPct(FightLoad::Input input, bool haveLoad)
        {
            if (!haveLoad || input.healers.empty())
                return PullPlanner::READY_MANA_PCT;
            float smallestPool = 0.0f;
            for (FightLoad::Healer& h : input.healers)
            {
                h.mana = h.maxMana;
                if (h.maxMana > 0.0f && (smallestPool <= 0.0f || h.maxMana < smallestPool))
                    smallestPool = h.maxMana;
            }
            if (smallestPool <= 0.0f)
                return PullPlanner::READY_MANA_PCT;
            input.tankHealth = input.tankMaxHealth;
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

    std::vector<PullPlanner::Issue> PullPlanner::CheckReady(WorldModel const& world, float healerManaPct,
                                                            Position3 const* planned)
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
            // Healers: what the planned fight needs (PullPlan::healerManaPct, HealerManaNeededPct).
            float const manaNeeded = (m.isBot && m.role == ROLE_HEALER) ? healerManaPct : READY_MANA_PCT;
            // Healers: the mana pool, not manaPct (a shapeshifted healer still heals with it).
            // Others: only while mana is their active power. A feral druid bot stays in bear /
            // cat form and PartyBotAI::StartResting only drinks with mana active, so counting its
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

        // Each candidate is judged with its best approach (walk in, or open from a shot or face
        // pull spot), then ranked (Better).
        uint32 stealthed = 0;
        Option best = ChooseOption(world, map, pack, candidates, std::vector<ObjectGuid>(), tank, openers, stealthed);
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
        // Fight place (PullPlace.h): the player may want the corner whatever the odds; else a
        // corner when the fight estimate is hard and the pack is not brought back.
        PullPlace const place = sCommanderMgr.GetPullPlace(leader->GetObjectGuid());
        bool const returnToSpot = place != PullPlace::Corner && distant && !fightThere && spotRisk.total < packRisk.total;
        FightLoad::Input load;
        bool const haveLoad = FightLoad::Build(world, map, best.expected, load);
        FightLoad::Result const estimate = haveLoad ? FightLoad::Simulate(load, "estimate: hard fight?") : FightLoad::Result();
        bool const hard = haveLoad && IsHardFight(estimate);
        bool const corner = place == PullPlace::Corner || (hard && !returnToSpot);

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
        pull.packAt = PositionOf(best.pullTarget);
        if (haveLoad && estimate.fightSec > 0.0f)
        {
            pull.expectedIncomingDps = estimate.damage / estimate.fightSec;
            pull.expectedFightSec = estimate.fightSec;
            pull.expectedPartyDps = load.partyDps;
            pull.expectedDpsSource = Value::DealtDpsSource(world);
            pull.expectedManaStart = estimate.manaStart;
            pull.expectedManaLeft = estimate.manaLeft;
            pull.expectedLowestTankPct = estimate.lowestTankPct;
        }
        pull.phaseSinceMs = WorldTimer::getMSTime();
        pull.plannedAtMs = pull.phaseSinceMs;
        pull.phase = skipReady ? PullPhase::Approach : PullPhase::WaitReady;
        pull.healerManaPct = HealerManaNeededPct(load, haveLoad);
        // `pull now` means now: no second prediction, no waiting for patrols.
        pull.recheckOnApproach = !skipReady;
        if (best.ranged)
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
        if (pull.corner)
            report.push_back(Format("Corner: once they come, %s steps out of their sight so they walk round to it (%s).",
                tank->GetName(), place == PullPlace::Corner ? "you chose corner fights"
                    : ("the fight looks hard: " + FightLoad::Describe(estimate)).c_str()));
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
            report.push_back("Fight estimate: " + FightLoad::Describe(FightLoad::Simulate(fightLoad, "estimate: report")) +
                Format(", party damage %.0f/s (%s).", fightLoad.partyDps, Value::DealtDpsSource(world)));
        PatrolStart const patrols = ChooseStart(world, map, best.pullTarget, tank, leader, pull.spot, pull.expected,
                                                PATROL_WAIT_MAX_MS / 1000.0f);
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

    PullPlanner::RecheckResult PullPlanner::RecheckBeforeApproach(TeamPlan& plan, WorldModel const& world, Map* map,
                                                                  uint32 nowMs, std::string& why)
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
        std::vector<ObjectGuid> expected = AssistPredictor::Guids(joiners);
        std::vector<ObjectGuid> aggroAdds = AggroRangeGuids(joiners);
        // A caster opened from a distance does not come: the tank walks to it afterwards, so
        // the walk-in aggro range counts as well (same as the planner, Evaluate).
        if (pull.OpensFromAfar() && IsRangedCaster(target))
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

        // 2. Patrols walking next to the pull while it lasts, joiners walking past included
        //    (scenarios 4, 11, 13; strat-20261103-013312): wait for the start the fight
        //    estimate likes (ChooseStart), within what is left of PATROL_WAIT_MAX_MS.
        Unit* leader = map->GetUnit(world.leader);
        uint32 const waitedMs = pull.patrolWaitSinceMs ? WorldTimer::getMSTimeDiff(pull.patrolWaitSinceMs, nowMs) : 0;
        float const budgetSec = waitedMs >= PATROL_WAIT_MAX_MS ? 0.0f : (PATROL_WAIT_MAX_MS - waitedMs) / 1000.0f;
        PatrolStart const patrols = ChooseStart(world, map, target, tank, leader, pull.spot, expected, budgetSec);
        ObjectGuid const mainPatrol = patrols.Main() ? patrols.Main()->patrol->GetObjectGuid() : ObjectGuid();
        if (patrols.considered && patrols.startSec > 0.0f)
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
        if (patrols.considered && !patrols.bearableNow)
        {
            // Waited the whole time and it is still too much: the clear skips the pack.
            if (clear.active && waitedMs >= PATROL_WAIT_MAX_MS)
            {
                why = Format("patrol %s still near after %u seconds", patrols.Main()->patrol->GetName(),
                    PATROL_WAIT_MAX_MS / 1000);
                AddUnique(pull.expected, expected);
                return RecheckResult::GiveUp;
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

        if (pull.recheckWaitSinceMs)
            plan.notes.push_back(Format("%s would bring %u now, pulling.", target->GetName(), uint32(expected.size())));
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
