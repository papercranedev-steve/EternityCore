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
 *                      Perception/Spatial/ShotSpot.h) and reports patrols that will pass.
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
 *                      reach the tank, while the pulled enemies are still on their way to the
 *                      tank (PullPlan::packComing), and for the healers' hold (PullOpening.h:
 *                      until the casters came). Settle lasts at least SETTLE_MS.
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
#include "Commander/Perception/Forecast/PullTimeline.h"
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
            Hold,       // back to WaitReady: too hard or too many now (clear) or a patrol is coming
            // Clear only: still over the limit after ClearPlanner::REPREDICT_WAIT_MS, or a patrol
            // still near after PATROL_WAIT_MAX_MS. `pull.expected` holds every creature seen in
            // the predictions (to skip); `why` says why. The caller resets the pull.
            GiveUp,
            // Clear only, like GiveUp, but the pull itself works: the fight estimate says the
            // party cannot carry it (after REPREDICT_WAIT_MS, or a patrol still joining after
            // PATROL_WAIT_MAX_MS). A clear that asks puts it to the player instead of skipping.
            TooHard,
            // Clear only: a patrol walking next to the pull can be ambushed alone now (user
            // 2026-11-24, "单拉路过的胖子"): `patrolFirst` says which. The caller plans that
            // patrol's pull instead; the pack is planned again after it (FindPatrolFirst).
            PatrolFirst,
        };
        // Runs when PullAssigner has just moved the pull to Approach. Map thread.
        // ClearPlanner::Step calls it on every refresh while a pull is going, so it is also the
        // pull's map-thread step: in Settle it runs SearchLosBreak (and returns NotDue).
        RecheckResult RecheckBeforeApproach(TeamPlan& plan, WorldModel const& world, Map* map, uint32 nowMs,
                                            std::string& why, ObjectGuid* patrolFirst = nullptr);

        // Patrol first: a patrol that would join the planned pull (walks next to it while the
        // fight lasts, or is predicted to come now) and that can be ambushed (AmbushSpot, the
        // usual AMBUSH_MAX_WAIT_SEC / AMBUSH_MAX_FROM_PARTY_YD) bringing no creature of the
        // planned pull, in a fight the estimate says the party carries. Not a patrol in
        // ClearPlan::patrolFirstTried. Empty: none.
        ObjectGuid FindPatrolFirst(TeamPlan const& plan, WorldModel const& world, Map* map, Player* tank,
                                   Creature* target, std::vector<ObjectGuid> const& expected);

        // Line of sight pull, map thread, Settle only, once per pull (PullPlan::losBreak).
        // The doctrine chose to lure (OutlierTactic::Lure: the enemies stamped STAMP_LURE this
        // refresh, FightPlace::LureTargets): look for a point (LosBreak::FindPoint) around the fight
        // spot (around the tank when the pack was not brought back) that the casters cannot
        // see and the tank reaches by a short safe walk (around a corner is fine), not where an
        // idle patrol comes before the fight ends (PatrolZone.h). Healer bots get a
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
        // fight estimate (Perception/Forecast/FightLoad.h) has them spend x the margin, at least the
        // minimum (a healer still out of combat keeps drinking into the fight while the drink
        // pays: Behaviors/Support/RestBehavior.h). "Full" for a fight not bearable even at full mana.
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

        // Patrol targets (WorldModel::patrols) are ambushed on their way, never walked after
        // (PullChoice EvaluateAmbush, Perception/Spatial/AmbushSpot.h): a spot within
        // AMBUSH_MAX_FROM_PARTY_YD of the leader where the patrol comes by within
        // AMBUSH_MAX_WAIT_SEC. None: the patrol is passed over and, without a selection, the
        // next pack is pulled (it flew off: no point waiting; the patrol check of that pull
        // still counts it). Ranked like any candidate, the sooner it comes the better on a tie.
        // The recheck before the tank goes looks for the spot again; none for
        // PATROL_WAIT_MAX_MS: the clear skips it, a single pull is cancelled.
        float const AMBUSH_MAX_WAIT_SEC = 15.0f;
        float const AMBUSH_MAX_FROM_PARTY_YD = 35.0f;

        // Patrols (PatrolTracker, WorldModel::patrols), also those the prediction counts as
        // joiners because they walk past the pack right now (strat-20261103-013312): a patrol
        // joins when its walk comes within max(PATROL_MIN_CLEARANCE, its aggro range) of any
        // pulled creature (a patrol next to any of them is called in for help) or of the fight
        // spot. The fight is estimated (Perception/Forecast/FightLoad.h: the creatures' damage on the
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
        // from a shot spot (Perception/Spatial/ShotSpot.h) found within it.
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

        // Line of sight pull (SearchLosBreak). Who to hide from and the fight place Auto rules:
        // the doctrine's (Coordination/Doctrine/FightPlace.h). Candidate rays, ground, walk and
        // check budget: LosBreak.h (shared with the patrol guard).
        // A candidate this close to an idle hostile creature not in the pull, and closer to it
        // than the spot is, is skipped.
        float const LOS_OTHER_CREATURE_CLEARANCE = 20.0f;

        // Pure: who is not ready. Players count too (the leader's own health matters).
        // healerManaPct applies to healer bots only; everyone else with mana needs READY_MANA_PCT.
        // `planned`: the pull's planned fight spot; within READY_MAX_DISTANCE of it is not far.
        struct Issue
        {
            ObjectGuid who;
            char const* what;   // "dead", "hp", "mana", "far", "combat"
            float value;
        };
        // The mana share (0-1) CheckReady waits for from bot `m` while `pull` waits in
        // WaitReady, or went early and is not over (healers PullPlan::healerManaPct, others
        // READY_MANA_PCT); 0 otherwise.
        // A bot drinks up to it (RestValue::DrinkBelowShare) so the pull does not wait for
        // mana it would not drink.
        float ReadyManaShare(PullPlan const& pull, PartyMemberFacts const& m);
        std::vector<Issue> CheckReady(WorldModel const& world, float healerManaPct = READY_MANA_PCT,
                                      Position3 const* planned = nullptr);
        // An autonomous hunt (Hunt/HuntAssigner.h) has no pull waiting for the party, so bots
        // drink only below their own drink start share (RestValue::DrinkBelowShare with no pull
        // share): bots need that share, not READY_MANA_PCT. With READY_MANA_PCT a bot between
        // the two neither drank nor was ready, and the hunt waited for its spirit regeneration.
        // Everything else as CheckReady.
        std::vector<Issue> CheckReadyToHunt(WorldModel const& world);

        // `pull`'s moments if its tank went now (Perception/Forecast/PullTimeline.h). False:
        // no pull planned or its tank is not in the party.
        bool Timeline(WorldModel const& world, PullPlan const& pull, PullTimeline::Result& out);

        // Going early (user 2026-11-24): the tank need not wait for a bot's mana or health it
        // gets by resting on into the pull (it keeps drinking while the tank walks, opens and
        // builds threat). Each bot needs them by the moment it acts, x EARLY_GO_TIME_SHARE
        // (the timeline is a guess): a bot healing the team at contact, everyone else at
        // damageSec. No credit (needed at the go) for:
        //  - the pull's tank (walking, it cannot rest);
        //  - a bot not resting yet (drink / food not on);
        //  - a healer bot below healerManaPct / READY_HEALER_MANA_MARGIN: the fight's own
        //    mana without the margin, for a pull that goes wrong and needs heals at once;
        //  - the opening control's bot and the patrol hold's bot: they act at the opener;
        //  - anyone (EarlyGoAllowed false) in an ambush (the patrol sets the time), a fight not
        //    bearable even at full mana (healerManaPct READY_FULL_MANA_PCT), and a pull held
        //    back once (PullPlan::earlyHeldBack: no going and coming back again);
        //  - players and every other issue (dead, combat, far): goInSec unknown, the pull waits.
        // Before the opener the assigner checks again on every refresh; once the go lies more
        // than EARLY_GO_HOLD_SLACK_SEC away (a drinker stood up, the control went to a
        // drinker) the pull goes back to WaitReady. While the pull went early, ReadyManaShare
        // holds until it is over: a drink that ran out starts again.
        float const EARLY_GO_TIME_SHARE = 0.7f;
        float const EARLY_GO_HOLD_SLACK_SEC = 2.0f;
        uint32 const CHARGE_LEVEL = 4;      // warriors learn Charge (PullTimeline::Input::charges)
        bool EarlyGoAllowed(PullPlan const& pull);
        struct ReadyWait
        {
            ObjectGuid who;
            char const* what = "";      // an Issue's
            float readySec = -1.0f;     // resting gets it there in this long; < 0 never or not a rest issue
            float neededAtSec = 0.0f;   // with EARLY_GO_TIME_SHARE; 0 = no credit (above)
        };
        struct ReadyForecast
        {
            PullTimeline::Result timeline;
            std::vector<ReadyWait> waits;   // one per issue
            // The go could come this long from now: the latest readySec - neededAtSec, at
            // least 0 (0: go now). < 0: unknown (a player's issue, someone dead, in combat,
            // far, or never getting there by resting).
            float goInSec = -1.0f;
        };
        // `issues`: CheckReady's for this pull. False as Timeline.
        bool ForecastReady(WorldModel const& world, PullPlan const& pull, std::vector<Issue> const& issues,
                           ReadyForecast& out);
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
        // Walk guard of a walk-in pull, before the fight starts (230431: the tank followed a
        // patrolling gargoyle 70 yd into an idle pack). The pull is called off
        // (PullOutcome::CalledOff) when
        //  - the tank's walk to the target (WorldModel::routes, MeleeRoutes) comes into the
        //    aggro range of an idle creature the prediction does not count, on the hard line
        //    (RouteFact::safeTight false, a blocker not in PullPlan::expected), or
        //  - the target is more than TARGET_MOVED_YD from where it was planned (packAt): the
        //    prediction was made for another place.
        // A shot or face pull has its own checked spot and walk (ShotSpot, AmbushSpot).
        static constexpr float TARGET_MOVED_YD = 15.0f;
        // Ambush (PullPlan::ambush): the patrol has this long after its predicted arrival.
        static constexpr uint32 AMBUSH_EXTRA_MS = 8 * 1000;
        // The tank drops the target it still has after a pull ended without a fight (PullPlan::
        // standDownTarget) for this long.
        static constexpr uint32 STAND_DOWN_MS = 3 * 1000;
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
