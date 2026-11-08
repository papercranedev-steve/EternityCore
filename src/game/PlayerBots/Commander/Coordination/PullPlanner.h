/*
 * PartyBot Commander layer 2: pull planning (step 5).
 * Design: .kiro/docs/2026-11-01-pull-planning.md
 *
 * PullPlanner::Plan  - started by `.partybot ai pull`. Reads live objects (assist prediction
 *                      uses the server's own checks), so it runs on the world thread while
 *                      maps are idle. Picks the pack member that brings the fewest extra
 *                      creatures (assistance, aggro-together groups, and creatures whose aggro
 *                      range the tank walks into) and fills TeamPlan::pull. Decides whether the
 *                      tank opens from range (ranged pull: only from a shot spot,
 *                      Perception/ShotSpot.h) and reports patrols that will pass.
 *                      Ranking: fewest joiners, a shot spot when a ranged pull is wanted,
 *                      fewest elites, melee before casters, nearest.
 * PullPlanner::RecheckBeforeApproach
 *                    - map thread (ClearPlanner::Step), for single pulls and the clear alike:
 *                      when the tank is about to go, look for the shot spot again (none:
 *                      another target), predict again and hold the pull in WaitReady while a
 *                      patrol walking next to the pull makes the fight too hard (FightLoad)
 *                      and waiting for it to walk on helps (see "Patrols" below).
 *                      In Approach it answers the tank's "cannot shoot from this spot"
 *                      (PullShotState::spotFailed) with another spot or target.
 *                      In Settle it also looks for a line of sight pull point (SearchLosBreak).
 * PullAssigner       - pure; advances the pull phases on every perception refresh.
 *                      A fight with other enemies before the tank's opener went out ends it
 *                      as Interrupted (after it: they join, OPENED_GRACE_MS); real failures
 *                      end it as Cancelled. Settle waits for an
 *                      active line of sight pull (casters walked in, turned to someone else,
 *                      or the timeout), after a ranged pull fought in place for the pack to
 *                      reach the tank, and for the opening holds (PullOpening.h: damage
 *                      dealers until the tank has threat on what came, healers also until the
 *                      casters came). Settle lasts at least SETTLE_MS.
 *
 * Files (split by step, todo item 14):
 *   PullPlanner.cpp      Plan, CheckReady, RecheckBeforeApproach, FindRangedPullSpell
 *   PullChoice.h/.cpp    target and opening choice (walk in / shot / face pull), new spot or
 *                        target when the tank cannot shoot
 *   PullPatrolCheck.h/.cpp  patrols next to the pull and the start they allow
 *   PullLosSearch.h/.cpp SearchLosBreak (line of sight pull, corner) and when the break ends
 *   PullAssigner.cpp     PullAssigner (the phases)
 *   PullCommon.h/.cpp    helpers shared by the files above
 *   PullControl, PullOpening  opening control, who may start after the pull
 */

#ifndef MANGOS_COMMANDER_PULL_PLANNER_H
#define MANGOS_COMMANDER_PULL_PLANNER_H

#include "Commander/Coordination/IAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include <string>
#include <vector>

class Player;
class Creature;
class Map;

namespace Commander
{
    class PartyCommander;

    namespace PullPlanner
    {
        // Leader is the player who gave the command; `selected` may be null (nearest pack).
        // Returns false with the reason in `report` when no pull can be planned.
        bool Plan(Player* leader, PartyCommander& commander, Creature* selected, bool skipReady,
                  std::vector<std::string>& report);

        enum class RecheckResult : uint8
        {
            NotDue,     // not the refresh the tank leaves on, or already rechecked
            Go,         // the tank goes (expected list updated)
            Hold,       // back to WaitReady: too many now (clear limit) or a patrol is coming
            // Clear only: still over the limit after ClearPlanner::REPREDICT_WAIT_MS, or a patrol
            // still near after PATROL_WAIT_MAX_MS. `pull.expected` holds every creature seen in
            // the predictions (to skip); `why` says why. The caller resets the pull.
            GiveUp,
        };
        // Runs when PullAssigner has just moved the pull to Approach. Map thread.
        // ClearPlanner::Step calls it on every refresh while a pull is going, so it is also the
        // pull's map-thread step: in Settle it runs SearchLosBreak (and returns NotDue).
        RecheckResult RecheckBeforeApproach(TeamPlan& plan, WorldModel const& world, Map* map, uint32 nowMs,
                                            std::string& why);

        // Line of sight pull, map thread, Settle only, once per pull (PullPlan::losBreak).
        // A pulled caster standing back casting at the tank more than LOS_CASTER_MIN_DISTANCE
        // away: look for a point (LosBreak::FindPoint) around the fight
        // spot (around the tank when the pack was not brought back) that the casters cannot
        // see and the tank reaches by a short safe walk (around a corner is fine), not where an
        // idle patrol walks before the fight ends (PatrolAssigner::InPath). Healer bots get a
        // hiding point behind the tank (PullPlan::losHiders, LosHealerSpot.h); one without a
        // point stays and moves itself if it loses the tank (HealerStanceBehavior).
        // Found: losBreak = Active, the tank goes
        // there (PullBehavior) and PullAssigner keeps Settle going until the casters came.
        // Not found: NoPoint, fight where we are (as before).
        // Why it works: CreatureAI::DoSpellsListCasts tries the spell list every 1.2 s. A
        // CF_MAIN_RANGED_SPELL cast that succeeds turns combat movement and melee off (it
        // stands); any failure, SPELL_FAILED_LINE_OF_SIGHT from Spell::CheckCast included,
        // turns both back on, so the chase brings it to its victim. It notices at its next
        // try once the spell is off cooldown; a cast already under way is finished first.
        // Casters kept back by a script (melee off without a main ranged spell) may not come:
        // the timeout covers them.
        void SearchLosBreak(TeamPlan& plan, WorldModel const& world, Map* map, uint32 nowMs);

        // Spell the tank can open a pull with from range, 0 = none. `range` = the distance
        // to use (spell range, at most PULL_RANGE). Per class:
        //  Warrior  Shoot Bow / Shoot Gun / Shoot Crossbow (needs that ranged weapon and ammo)
        //           or Throw (thrown weapon equipped), if the spell is known.
        //  Druid    Faerie Fire (Feral) if learned (talent; castable in bear form, no damage).
        //  Hunter   Auto Shot with a ranged weapon and ammo (hunter tanks are rare).
        //  Paladin  none: Judgement needs a seal and 10 yd, Exorcism only hits undead and demons.
        //  Others   none (not tank classes).
        uint32 FindRangedPullSpell(Player* tank, float& range);
        // "No ranged pull: ..." when a warrior or hunter tank has a bow / gun / crossbow but no
        // ammo equipped, else empty. Plan reports it; a clear says it once (ClearPlan).
        std::string MissingAmmoNote(Player* tank);

        float const READY_HEALTH_PCT = 90.0f;
        // Non-healers; healers too when there is no fight estimate.
        float const READY_MANA_PCT = 80.0f;
        // Healer bots (user, 2026-11-07: by how hard the fight is, not a fixed 80%): what the
        // fight estimate (Perception/FightLoad.h) has them spend x the margin, at least the
        // minimum (a healer still out of combat keeps drinking into the fight while the drink
        // pays: Behaviors/RestBehavior.h). "Full" for a fight not bearable even at full mana.
        // Drinking stops at 100%; 99 leaves room for rounding. Margin and minimum untested.
        float const READY_HEALER_MANA_MARGIN = 1.5f;
        float const READY_HEALER_MIN_MANA_PCT = 50.0f;
        float const READY_FULL_MANA_PCT = 99.0f;
        float const READY_MAX_DISTANCE = 40.0f;   // from the leader
        // Next to the leader, a pack further than this may be brought back to the fight spot:
        // when PullOdds::FightThere says the party should not fight it where it stands, and
        // the spot has less add risk than the pack. With a ranged pull spell the tank then
        // opens from range (charging in and walking back gains nothing), and it walks back
        // backwards facing the pack with charges vetoed (PullBehavior).
        float const RETURN_MIN_DISTANCE = 15.0f;

        // Patrols (PatrolTracker, WorldModel::patrols), also those the prediction counts as
        // joiners because they walk past the pack right now (strat-20261103-013312): a patrol
        // joins when its walk comes within max(PATROL_MIN_CLEARANCE, its aggro range) of any
        // pulled creature (a patrol next to any of them is called in for help) or of the fight
        // spot. The fight is estimated (Perception/FightLoad.h: the creatures' damage on the
        // tank, the party's damage, the healer bots' real heals and mana) with every patrol
        // joining when its walk brings it, and the creatures it calls for help from where it
        // is then (AssistPredictor::ExpandAt, after the assistance delay; strat-20261108-230756:
        // Patchwork Horror met the tank beside a second pack and brought all of it):
        //  - bearable pulled now (a bat): pull now, the party takes it.
        //  - else the earliest start within PATROL_WAIT_MAX_MS that is bearable (Patchwork
        //    Horror walking on), held in WaitReady until then;
        //  - none: the start with the best estimate if it is PATROL_WAIT_MIN_GAIN better
        //    (tank health points) than now, else pull now (waiting does not help).
        // After PATROL_WAIT_MAX_MS of waiting a single pull goes, the clear skips the pack.
        // Fight length for the tracks = the estimate without the patrols + PULL_LEAD_SEC (the
        // tank walking out and back), within PATROL_HORIZON_SEC .. PATROL_HORIZON_MAX_SEC.
        float const PATROL_HORIZON_SEC = 20.0f;
        float const PATROL_HORIZON_MAX_SEC = 120.0f;
        float const PULL_LEAD_SEC = 5.0f;
        float const PATROL_MIN_CLEARANCE = 15.0f;
        uint32 const PATROL_WAIT_MAX_MS = 30 * 1000;
        float const PATROL_WAIT_MIN_GAIN = 10.0f;
        // While a held pull comes back to Approach on every refresh, RecheckBeforeApproach
        // predicts again (assist prediction, every patrol's track) at most this often; in
        // between it holds the pull with the previous answer. Waiting for a patrol needs a
        // quicker answer (it may leave any moment) than waiting for a pack over the limit.
        uint32 const RECHECK_PATROL_WAIT_MS = 500;
        uint32 const RECHECK_HOLD_MS = 1000;

        // Ranged pull: the tank opens from at most this far (combat scenario 13, rows 10-12),
        // from a shot spot (Perception/ShotSpot.h) found within it.
        float const PULL_RANGE = 25.0f;
        // The tank reports its spot failed (PullShotState::spotFailed): another spot for the
        // same target, up to this many failures of that target; then another pack member that
        // can be shot, up to MAX_TARGET_SWITCHES targets given up per pull; then it walks in.
        uint32 const MAX_SPOT_FAILURES = 2;
        uint32 const MAX_TARGET_SWITCHES = 2;
        // Hostile creatures this close to the target that the prediction does not bring are
        // a risk of walking in (scenario 13, D next to G and P2): open from range.
        float const RANGED_PULL_NEIGHBOR_RADIUS = 10.0f;
        // Melee stand point: target combat reach plus this.
        float const MELEE_STAND_EXTRA = 1.5f;

        // Line of sight pull (SearchLosBreak). Casters closer than this to the tank are left alone.
        float const LOS_CASTER_MIN_DISTANCE = 10.0f;
        // Candidate rays, ground, walk and check budget: LosBreak.h (shared with the patrol guard).
        // A candidate this close to an idle hostile creature not in the pull, and closer to it
        // than the spot is, is skipped.
        float const LOS_OTHER_CREATURE_CLEARANCE = 20.0f;
        uint32 const LOS_MAX_CASTERS = 2;     // the point must hide from each of them

        // Fight place Auto (PullPlace.h): a corner when the fight estimate (FightLoad) has the
        // tank below this, or the healer bots ending with less than this share of their mana,
        // or the fight is not bearable. Not when the pack is brought back.
        float const CORNER_TANK_PCT = 50.0f;
        float const CORNER_MANA_LEFT_SHARE = 0.25f;

        // Pure: who is not ready. Players count too (the leader's own health matters).
        // healerManaPct applies to healer bots only; everyone else with mana needs READY_MANA_PCT.
        // `planned`: the pull's planned fight spot; within READY_MAX_DISTANCE of it is not far.
        struct Issue
        {
            ObjectGuid who;
            char const* what;   // "dead", "hp", "mana", "far", "combat"
            float value;
        };
        std::vector<Issue> CheckReady(WorldModel const& world, float healerManaPct = READY_MANA_PCT,
                                      Position3 const* planned = nullptr);
    }

    class PullAssigner : public IAssigner
    {
    public:
        // Only time a bot is not ready counts; the player's own state is reported, never times out.
        static constexpr uint32 WAIT_READY_TIMEOUT_MS = 90 * 1000;
        static constexpr uint32 WAIT_READY_NOTE_MS = 15 * 1000;     // then say once who is not ready
        static constexpr uint32 APPROACH_TIMEOUT_MS = 20 * 1000;
        // A ranged pull that has not started the fight by then (no ammo, wrong form, no line of
        // sight anywhere on the way) becomes a normal walk-in pull.
        static constexpr uint32 RANGED_PULL_FALLBACK_MS = 10 * 1000;
        // Once the opener went out (PullShotState::openedAtMs) an enemy nobody expected that
        // shows up first does not end the pull: the pulled ones get this long to come (charge
        // flight, shot travel, a face pulled target turning round). Then it is Interrupted.
        static constexpr uint32 OPENED_GRACE_MS = 3 * 1000;
        // Return lasts at most the walk at run-back speed (Backpedal) x RETURN_TIME_FACTOR plus
        // RETURN_TIMEOUT_MS, and no more than RETURN_TIMEOUT_MAX_MS.
        static constexpr uint32 RETURN_TIMEOUT_MS = 8 * 1000;
        static constexpr uint32 RETURN_TIMEOUT_MAX_MS = 30 * 1000;
        static constexpr float RETURN_BACK_SPEED = 4.5f;   // yd/s, base run-back speed
        static constexpr float RETURN_TIME_FACTOR = 1.5f;  // path detours, pauses for casts
        static constexpr uint32 SETTLE_MS = 2500;      // shortest Settle (holds: PullOpening.h)
        static constexpr float RETURN_ARRIVED = 4.0f;
        // Line of sight pull: Settle lasts until every caster is within LOS_CASTER_ARRIVED of the
        // tank (or dead, crowd controlled, out of the fight), at most this long after the point
        // was found. Long enough for one cast to finish plus a 1.2 s spell list try after it.
        static constexpr uint32 LOS_BREAK_TIMEOUT_MS = 8 * 1000;
        static constexpr float LOS_CASTER_ARRIVED = 8.0f;
        // Ranged pull fought where the pack stands (PullPlan::awaitArrival): the pack came when
        // every pulled enemy that goes for the tank with melee on is within ARRIVAL_DISTANCE of
        // it (from the enemy's edge). Casters standing back (melee off) are not waited for: the
        // line of sight pull or the party deals with them. The wait lasts at most the farthest
        // one's walk at ARRIVAL_SPEED x ARRIVAL_TIME_FACTOR plus ARRIVAL_EXTRA_MS, no more than
        // ARRIVAL_TIMEOUT_MAX_MS (pathing detours, a creature that does not come).
        static constexpr float ARRIVAL_DISTANCE = 5.0f;
        static constexpr float ARRIVAL_SPEED = 6.0f;        // yd/s, a chasing creature (run speed 7)
        static constexpr float ARRIVAL_TIME_FACTOR = 1.5f;
        static constexpr uint32 ARRIVAL_EXTRA_MS = 2 * 1000;
        static constexpr uint32 ARRIVAL_TIMEOUT_MAX_MS = 10 * 1000;

        char const* GetId() const override { return "pull"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
