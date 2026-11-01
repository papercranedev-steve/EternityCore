/*
 * PartyBot Commander layer 2: pull planning (step 5).
 * Design: .kiro/docs/2026-11-01-pull-planning.md
 *
 * PullPlanner::Plan  - started by `.partybot ai pull`. Reads live objects (assist prediction
 *                      uses the server's own checks), so it runs on the world thread while
 *                      maps are idle. Picks the pack member that brings the fewest extra
 *                      creatures (assistance, aggro-together groups, and creatures whose aggro
 *                      range the tank walks into) and fills TeamPlan::pull. Decides whether the
 *                      tank opens from range (ranged pull) and reports patrols that will pass.
 * PullPlanner::RecheckBeforeApproach
 *                    - map thread (ClearPlanner::Step), for single pulls and the clear alike:
 *                      when the tank is about to go, predict again and hold the pull in
 *                      WaitReady while a patrol is about to walk next to the target or the
 *                      fight spot.
 *                      In Settle it also looks for a line of sight pull point (SearchLosBreak).
 * PullAssigner       - pure; advances the pull phases on every perception refresh.
 *                      A fight with other enemies before the pull starts ends it as
 *                      Interrupted; real failures end it as Cancelled. Settle waits for an
 *                      active line of sight pull (casters walked in, or the timeout).
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
        // away: look for a point on a ring (LOS_RING_INNER / LOS_RING_OUTER) around the fight
        // spot (around the tank when the pack was not brought back) that the casters cannot
        // see, the tank can see (short walk) and every healer bot can see (they do not move).
        // At most LOS_MAX_CHECKS line of sight checks. Found: losBreak = Active, the tank goes
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

        float const READY_HEALTH_PCT = 90.0f;
        float const READY_MANA_PCT = 80.0f;
        // "Full" for healers before a hard pack. Drinking stops at 100%; 99 leaves room for rounding.
        float const READY_FULL_MANA_PCT = 99.0f;
        float const READY_MAX_DISTANCE = 40.0f;   // from the leader
        // Next to the leader, a pack further than this is brought back to the fight spot.
        float const RETURN_MIN_DISTANCE = 15.0f;

        // Patrols (PatrolTracker, WorldModel::patrols): a patrol that comes within
        // max(PATROL_MIN_CLEARANCE, its aggro range) of the target or the fight spot while the
        // fight is expected to last holds the pull in WaitReady until the start that keeps it
        // away for the whole fight, at most PATROL_WAIT_MAX_MS (then a single pull goes anyway,
        // the clear skips the pack). No such start (a loop shorter than the fight, Patchwork
        // Horror in strat-20261101-225111): waiting does not help, the pull goes and says so.
        // Fight length = health of the expected creatures / party damage x FIGHT_TIME_MARGIN
        // + PULL_LEAD_SEC (the tank walking out and back), within PATROL_HORIZON_SEC ..
        // PATROL_HORIZON_MAX_SEC.
        float const PATROL_HORIZON_SEC = 20.0f;
        float const PATROL_HORIZON_MAX_SEC = 120.0f;
        float const FIGHT_TIME_MARGIN = 1.25f;
        float const PULL_LEAD_SEC = 5.0f;
        float const PATROL_MIN_CLEARANCE = 15.0f;
        uint32 const PATROL_WAIT_MAX_MS = 30 * 1000;
        // While a held pull comes back to Approach on every refresh, RecheckBeforeApproach
        // predicts again (assist prediction, every patrol's track) at most this often; in
        // between it holds the pull with the previous answer. Waiting for a patrol needs a
        // quicker answer (it may leave any moment) than waiting for a pack over the limit.
        uint32 const RECHECK_PATROL_WAIT_MS = 500;
        uint32 const RECHECK_HOLD_MS = 1000;

        // Ranged pull: the tank opens from at most this far (combat scenario 13, rows 10-12).
        float const PULL_RANGE = 25.0f;
        // Prediction stand point is this much inside the range (the tank stops once in range).
        float const RANGED_STAND_MARGIN = 3.0f;
        // Hostile creatures this close to the target that the prediction does not bring are
        // a risk of walking in (scenario 13, D next to G and P2): open from range.
        float const RANGED_PULL_NEIGHBOR_RADIUS = 10.0f;
        // Melee stand point: target combat reach plus this.
        float const MELEE_STAND_EXTRA = 1.5f;

        // Line of sight pull (SearchLosBreak). Casters closer than this to the tank are left alone.
        float const LOS_CASTER_MIN_DISTANCE = 10.0f;
        // Candidate ring around the fight spot: 6 directions x these two radii.
        float const LOS_RING_INNER = 6.0f;
        float const LOS_RING_OUTER = 11.0f;
        // Ground of a candidate within this of the spot's height (no ledges, no other floor).
        float const LOS_POINT_MAX_HEIGHT_DIFF = 3.0f;
        // A candidate this close to an idle hostile creature not in the pull, and closer to it
        // than the spot is, is skipped.
        float const LOS_OTHER_CREATURE_CLEARANCE = 20.0f;
        uint32 const LOS_MAX_CHECKS = 16;     // line of sight checks per pull
        uint32 const LOS_MAX_CASTERS = 2;     // the point must hide from each of them
        uint32 const LOS_MAX_HEALERS = 2;     // healer bots that must still see the point

        // Pure: who is not ready. Players count too (the leader's own health matters).
        // healerManaPct applies to healer bots only; everyone else with mana needs READY_MANA_PCT.
        struct Issue
        {
            ObjectGuid who;
            char const* what;   // "dead", "hp", "mana", "far", "combat"
            float value;
        };
        std::vector<Issue> CheckReady(WorldModel const& world, float healerManaPct = READY_MANA_PCT);
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
        static constexpr uint32 RETURN_TIMEOUT_MS = 8 * 1000;
        static constexpr uint32 SETTLE_MS = 2500;
        static constexpr float RETURN_ARRIVED = 4.0f;
        // Line of sight pull: Settle lasts until every caster is within LOS_CASTER_ARRIVED of the
        // tank (or dead, crowd controlled, out of the fight), at most this long after the point
        // was found. Long enough for one cast to finish plus a 1.2 s spell list try after it.
        static constexpr uint32 LOS_BREAK_TIMEOUT_MS = 8 * 1000;
        static constexpr float LOS_CASTER_ARRIVED = 8.0f;

        char const* GetId() const override { return "pull"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
    };
}

#endif
