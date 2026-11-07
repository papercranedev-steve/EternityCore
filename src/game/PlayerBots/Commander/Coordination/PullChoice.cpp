// Pull target and opening choice (PullChoice.h).
#include "Commander/Coordination/PullPlanner.h"
#include "Commander/Coordination/PullCommon.h"
#include "Commander/Coordination/PullChoice.h"
#include "Commander/Coordination/PullPatrolCheck.h"
#include "Commander/Coordination/PullLosSearch.h"
#include "Commander/Coordination/ClearPlanner.h"
#include "Commander/Coordination/LosBreak.h"
#include "Commander/Coordination/LosHealerSpot.h"
#include "Commander/Coordination/PatrolAssigner.h"
#include "Commander/Coordination/PullMethod.h"
#include "Commander/Coordination/PullOpening.h"
#include "Commander/Coordination/PullControl.h"
#include "Commander/Coordination/PullPlace.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/AssistPredictor.h"
#include "Commander/Perception/FightLoad.h"
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
    using namespace PullDetail;

    namespace
    {
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

        // One way to open from a distance (a shot or a face pull) and what it brings.
        struct FarOpening
        {
            bool found = false;
            bool face = false;
            ShotSpot::Spot spot;
            std::vector<ObjectGuid> expected;
            std::vector<ObjectGuid> aggroAdds;
            uint32 elites = 0;
            ShotSpot::Failure why = ShotSpot::Failure::None;
        };

        FarOpening JudgeFar(WorldModel const& world, Map* map, Creature* c, Player* tank, Openers const& op, bool face,
                            Option const& melee, std::vector<Position3> const& avoid)
        {
            FarOpening f;
            f.face = face;
            bool const found = face ? FindFaceSpot(world, map, c, tank, avoid, f.spot, f.why)
                                    : FindShotSpot(map, c, tank, op.shotSpell, op.shotRange, avoid, f.spot, f.why);
            if (!found)
                return f;
            Position3 const at = f.spot.at;
            std::vector<AssistPredictor::Joiner> const joiners = AssistPredictor::PredictDetailed(c, tank, &at);
            f.expected = AssistPredictor::Guids(joiners);
            f.aggroAdds = AggroRangeGuids(joiners);
            if (melee.caster)
            {
                // It does not come to the tank: the original AI walks to it after the opener,
                // so walking-in aggro range still applies later.
                AddUnique(f.expected, melee.aggroAdds);
                AddUnique(f.aggroAdds, melee.aggroAdds);
            }
            f.elites = CountElites(map, f.expected);
            f.found = true;
            return f;
        }

        // Stealthed pack members counted by the planner stay counted.
        void KeepStealthed(WorldModel const& world, std::vector<ObjectGuid> const& before, std::vector<ObjectGuid>& expected)
        {
            for (ObjectGuid g : before)
                if (!Contains(expected, g) && IsStealthedPackCreature(world, g))
                    expected.push_back(g);
        }
    }

    namespace PullDetail
    {
        // Opening from a distance is used when the player chose it (Openers::forced) or for a
        // reason: the target stands back and casts, walking in would bring more by aggro range,
        // or a creature the prediction does not bring stands next to it. A shot or a face pull,
        // whichever brings fewer, then fewer elites; a shot on a tie (more room to the aggro
        // ranges). It needs a spot (`avoid`: spots that failed live); none: walks in (`noSpot`,
        // ranked after the candidates that have one).
        Option Evaluate(WorldModel const& world, Map* map, Creature* c, Player* tank, Openers const& op,
                        std::vector<Position3> const& avoid)
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
            if (!op.shotSpell)
                o.meleeAggroAddsNoRanged = uint32(o.aggroAdds.size());
            if ((op.shotSpell || op.face) && (op.forced || caster || walkInAdds || neighbour))
            {
                FarOpening shot, face;
                if (op.shotSpell)
                    shot = JudgeFar(world, map, c, tank, op, false, o, avoid);
                if (op.face)
                    face = JudgeFar(world, map, c, tank, op, true, o, avoid);
                FarOpening const* pick = nullptr;
                if (shot.found && face.found)
                    pick = face.expected.size() < shot.expected.size() ||
                           (face.expected.size() == shot.expected.size() && face.elites < shot.elites) ? &face : &shot;
                else if (shot.found)
                    pick = &shot;
                else if (face.found)
                    pick = &face;
                if (!pick)
                {
                    o.noSpot = true;
                    o.noSpotWhy = op.shotSpell ? shot.why : face.why;
                    o.elites = CountElites(map, o.expected);
                    return o;
                }
                if (op.forced || pick->expected.size() <= o.expected.size())
                {
                    o.ranged = !pick->face;
                    o.face = pick->face;
                    o.spot = pick->spot;
                    if (op.forced)
                        o.rangedWhy = pick->face ? "you chose face pulls" : "you chose ranged pulls";
                    else if (caster)
                        o.rangedWhy = Format("%s stands back and casts", c->GetName());
                    else if (walkInAdds)
                        o.rangedWhy = Format("walking in would bring %u more by aggro range", uint32(o.aggroAdds.size()));
                    else
                        o.rangedWhy = Format("%s is within %.0f yd of the target", neighbour->GetName(),
                            PullPlanner::RANGED_PULL_NEIGHBOR_RADIUS);
                    if (shot.found && face.found)
                        o.rangedWhy += pick->face ? Format("; a face pull brings %u, a shot %u", uint32(face.expected.size()),
                                                           uint32(shot.expected.size()))
                                                  : std::string("; a shot brings no more than a face pull");
                    o.expected = pick->expected;
                    o.aggroAdds = pick->aggroAdds;
                }
            }
            o.elites = CountElites(map, o.expected);
            return o;
        }

        // Best pull among `candidates` (Better), leaving out `excluded`. Stealthed members are
        // skipped (the tank cannot target them and would walk for 20 seconds) and counted.
        Option ChooseOption(WorldModel const& world, Map* map, PackFacts const* pack,
                            std::vector<ObjectGuid> const& candidates, std::vector<ObjectGuid> const& excluded,
                            Player* tank, Openers const& op, uint32& stealthed)
        {
            // A pack with a hostile member is pulled through a hostile one. strat-20261103-005941:
            // a neutral Ghostly Citizen wandering in the Ravaged Cadaver pack brought only
            // itself (fewest joiners), had no shot spot, and the tank walking to it took the
            // whole pack by aggro range. Neutral and no-aggro creatures never come on their own.
            bool const anyHostile = pack && AnyMember(*pack, [](PackCreature const& m) { return m.aggressive && !m.noAggro; });
            Option best;
            for (ObjectGuid guid : candidates)
            {
                if (Contains(excluded, guid))
                    continue;
                Creature* c = map->GetCreature(guid);
                if (!c || !c->IsAlive() || c->IsInCombat() || !tank->IsValidAttackTarget(c))
                    continue;
                PackCreature const* facts = FindMemberFacts(pack, guid);
                if (anyHostile && facts && (!facts->aggressive || facts->noAggro))
                    continue;
                if (facts && facts->stealthed)
                {
                    ++stealthed;
                    continue;
                }
                Option o = Evaluate(world, map, c, tank, op);
                if (Better(o, best))
                    best = std::move(o);
            }
            return best;
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
                Openers shotOnly;
                shotOnly.shotSpell = pull.rangedPullSpell;
                shotOnly.shotRange = pull.rangedPullRange;
                shotOnly.forced = true;
                best = ChooseOption(world, map, pack, candidates, pull.excludedTargets, tank, shotOnly, stealthed);
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

            Option melee = Evaluate(world, map, target, tank, Openers());
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
}
