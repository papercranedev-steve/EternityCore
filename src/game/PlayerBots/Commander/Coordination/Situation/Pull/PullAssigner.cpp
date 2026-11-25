// Pull phases (PullAssigner in PullPlanner.h).
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

        // PullPlan::wentEarly no longer holds (PullPlanner::ForecastReady, EARLY_GO_HOLD_SLACK_SEC):
        // `issues` says who. The tank's own issues do not count (walking away from the leader
        // makes it "far"; it went with none of the rest kind).
        bool EarlyGoBroken(WorldModel const& world, TeamPlan const& plan, std::vector<PullPlanner::Issue>& issues)
        {
            PullPlan const& pull = plan.pull;
            issues = PullPlanner::CheckPullReady(world, plan);
            issues.erase(std::remove_if(issues.begin(), issues.end(),
                [&pull](PullPlanner::Issue const& i) { return i.who == pull.tank; }), issues.end());
            if (issues.empty())
                return false;
            PullPlanner::ReadyForecast forecast;
            if (!PullPlanner::ForecastReady(world, pull, issues, forecast))
                return false;
            return forecast.goInSec < 0.0f || forecast.goInSec > PullPlanner::EARLY_GO_HOLD_SLACK_SEC;
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

        // Walk guard of a walk-in pull (PullAssigner::TARGET_MOVED_YD): why the plan no longer
        // fits, empty when it still does.
        std::string WalkGuard(WorldModel const& world, PullPlan const& pull)
        {
            if (pull.OpensFromAfar())
                return std::string();
            EnemyFacts const* target = world.FindTargeted(pull.target);
            Position3 at;
            if (target)
                at = target->pos;
            else if (PackCreature const* c = FindPackCreature(world, pull.target))
                at = c->pos;
            else
                return std::string();
            float const moved = PackResolver::Distance(at, pull.packAt);
            if (moved > PullAssigner::TARGET_MOVED_YD)
                return Format("the target walked %.0f yd from where the pull was planned.", moved);
            RouteFact const* route = world.FindRoute(pull.tank, pull.target);
            if (route && !route->safeTight && !route->blocker.IsEmpty() && !Contains(pull.expected, route->blocker))
                return "the walk to the target passes the aggro range of a creature the prediction does not count.";
            return std::string();
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
        // Opening control (PullControl.h): the controller reaching its spot restarts Approach.
        PullControl::Update(world, pull, now, plan.notes);
        uint32 const elapsed = WorldTimer::getMSTimeDiff(pull.phaseSinceMs, now);
        auto enter = [&](PullPhase phase)
        {
            pull.phase = phase;
            pull.phaseSinceMs = now;
            // Healers keep waiting until the first Settle refresh past the arrival wait
            // decides (PullOpening::Update).
            if (phase == PullPhase::Settle)
            {
                PullOpening::Release(pull);
                pull.holdHealing = true;
            }
        };
        // `pull.expected` is kept: ClearPlanner reads it after an interrupted pull.
        auto finish = [&](PullOutcome outcome, std::string const& text)
        {
            plan.notes.push_back(text);
            // Left Approach without a fight: the tank still has the target (PullBehavior drops it).
            if (pull.phase == PullPhase::Approach)
            {
                pull.standDownTarget = pull.target;
                pull.standDownUntilMs = now + STAND_DOWN_MS;
            }
            pull.phase = PullPhase::None;
            plan.lastPullOutcome = outcome;
        };
        auto cancel = [&](std::string const& why) { finish(PullOutcome::Cancelled, "Pull cancelled: " + why); };
        auto interrupt = [&](std::string const& why)
        {
            finish(PullOutcome::Interrupted, "Pull interrupted: " + why +
                (plan.clear.active ? " Clear goes on after this fight." : ""));
        };
        auto callOff = [&](std::string const& why)
        {
            finish(PullOutcome::CalledOff, "Pull called off: " + why +
                (plan.clear.active ? " Clear plans the next pull from here." : " Pull again to plan it from here."));
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
                    plan.readyWaiver = ReadyWaiver();   // (it was for the next fight)
                    interrupt("a fight started.");
                    return;
                }
                std::vector<PullPlanner::Issue> const issues = PullPlanner::CheckPullReady(world, plan);
                // Nothing to drink: the tank asks the player (PullQuestions, PullAskKind::NotReady).
                pull.noDrink.clear();
                for (PullPlanner::Issue const& i : issues)
                    if (PullPlanner::NoDrinkIssue(world, i))
                        pull.noDrink.push_back({ i.who, i.value });
                if (pull.noDrink.empty())
                    pull.noDrinkSinceMs = 0;
                else if (!pull.noDrinkSinceMs)
                    pull.noDrinkSinceMs = now;
                if (issues.empty())
                {
                    // Announce once: RecheckBeforeApproach may send the pull back here (new
                    // prediction over the limit, patrol coming).
                    if (!pull.readyAnnounced)
                        plan.notes.push_back("Everyone ready, pulling.");
                    pull.readyAnnounced = true;
                    pull.wentEarly = false;
                    enter(PullPhase::Approach);
                    return;
                }
                // Only bots resting on into the pull are not ready: go now (PullPlanner.h, EARLY_GO_*).
                PullPlanner::ReadyForecast forecast;
                if (PullPlanner::ForecastReady(world, pull, issues, forecast) &&
                    forecast.goInSec == 0.0f)
                {
                    if (!pull.readyAnnounced)
                        plan.notes.push_back("Pulling while " + DescribeIssues(issues, world, pull) +
                            " rest on (ready by the time they act).");
                    pull.readyAnnounced = true;
                    pull.wentEarly = true;
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
                // A bot with nothing to drink is the player's call when he is there (asked, his
                // no means wait for its regeneration), not a timeout.
                bool const timesOut = std::any_of(issues.begin(), issues.end(), [&world](PullPlanner::Issue const& i)
                {
                    PartyMemberFacts const* m = world.FindMember(i.who);
                    return !m || (m->isBot && !(world.humanInTeam && PullPlanner::NoDrinkIssue(world, i)));
                });
                if (timesOut && pull.lastReadyCheckMs)
                    pull.botWaitMs += WorldTimer::getMSTimeDiff(pull.lastReadyCheckMs, now);
                pull.lastReadyCheckMs = now;

                if (!pull.readyNoteSent && elapsed > WAIT_READY_NOTE_MS)
                {
                    pull.readyNoteSent = true;
                    plan.notes.push_back("Still waiting for: " + DescribeIssues(issues, world, pull) +
                        (botIssue ? "." : ". (Your own state does not time out.)"));
                }
                if (pull.botWaitMs > WAIT_READY_TIMEOUT_MS)
                {
                    VoiceEvent e;
                    e.key = "voice.pull.cancel";
                    e.speaker = pull.tank;
                    plan.voice.push_back(std::move(e));
                    cancel("bots not ready after 90 seconds (.partybot ai ready shows why).");
                }
                return;
            }
            case PullPhase::Approach:
            {
                // The opener went out this Approach (PullShotState::openedAtMs, the tank's tick).
                PullShotState const& shot = pull.rangedShot;
                uint32 const openedAtMs = shot.approachSinceMs == pull.phaseSinceMs ? shot.openedAtMs : 0;
                std::string guardWhy;
                std::vector<PullPlanner::Issue> earlyIssues;
                std::vector<ObjectGuid> unexpected;
                for (EnemyFacts const& e : world.enemies)
                    // (an ally's fight nearby, e.g. a guard, is not ours)
                    if (!e.onAllyOnly && IsUnexpected(e, pull))
                        unexpected.push_back(e.guid);
                if (fightStarted)
                {
                    plan.readyWaiver = ReadyWaiver();   // (it was for this fight)
                    // Enemies nobody expected are in this fight too: the pack-coming wait and
                    // the line of sight pull count them.
                    if (!unexpected.empty())
                    {
                        AddUnique(pull.extra, unexpected);
                        AddUnique(pull.expected, unexpected);
                        plan.notes.push_back(Format("%u enemy(s) not in the prediction joined the pull.",
                            uint32(unexpected.size())));
                    }
                    enter(pull.returnToSpot ? PullPhase::Return : PullPhase::Settle);
                    // Opened from a distance, fought here: wait where the tank opened.
                    if (!pull.returnToSpot && pull.OpensFromAfar())
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
                // Someone else started another fight before the opener: the tank must not go on
                // to pull a second pack. After it the pull cannot be called off: wait for the
                // pulled enemies a moment (a charge in flight, a shot on its way).
                else if (!unexpected.empty() &&
                         (!openedAtMs || WorldTimer::getMSTimeDiff(openedAtMs, now) > OPENED_GRACE_MS))
                    interrupt(openedAtMs ? "other enemies came and the pulled ones did not follow." :
                                           "a fight with other enemies started.");
                // Went early: until the opener the resting bots must still be ready in time (one
                // stood up, a drink ran out). Else back to waiting.
                else if (pull.wentEarly && !openedAtMs && EarlyGoBroken(world, plan, earlyIssues))
                {
                    plan.notes.push_back("Holding the pull: " + DescribeIssues(earlyIssues, world, pull) +
                        " not ready in time.");
                    pull.wentEarly = false;
                    pull.earlyHeldBack = true;
                    enter(PullPhase::WaitReady);
                }
                else if (pull.ambush)
                {
                    // Waits on the patrol's way: no walk-in fallback (it would follow the patrol).
                    if (elapsed > pull.ambushTimeoutMs)
                        cancel("the patrol did not come by the ambush spot in time.");
                }
                else if (elapsed > APPROACH_TIMEOUT_MS)
                    cancel("tank did not reach the target in 20 seconds.");
                else if (!openedAtMs && !(guardWhy = WalkGuard(world, pull)).empty())
                    callOff(guardWhy);
                else if (pull.rangedPullSpell && elapsed > RANGED_PULL_FALLBACK_MS)
                {
                    pull.rangedPullSpell = 0;
                    plan.notes.push_back(NameIn(pull, world, pull.tank) +
                        " could not open from range (no ammo, line of sight or form?). Walking in instead.");
                }
                else if (pull.facePull && elapsed > RANGED_PULL_FALLBACK_MS)
                {
                    pull.facePull = false;
                    plan.notes.push_back(NameIn(pull, world, pull.tank) + " could not face pull. Walking in instead.");
                }
                return;
            }
            case PullPhase::Return:
                if (PackResolver::Distance(tank->pos, pull.spot) <= RETURN_ARRIVED ||
                    elapsed > std::max<uint32>(pull.returnTimeoutMs, uint32(RETURN_TIMEOUT_MS)))
                    enter(PullPhase::Settle);
                return;
            case PullPhase::Settle:
                // Ranged pull fought in place: the tank stays where it shot until the pack came.
                // Settle's own time starts then (the waits below count from then too).
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
                    if (LosBreakOver(world, pull, tank->pos, now, timedOut))
                    {
                        pull.losBreak = PullLosBreak::Ended;
                        if (timedOut)
                            plan.notes.push_back(Format("The caster did not come out within %u seconds, fighting here.",
                                LOS_BREAK_TIMEOUT_MS / 1000));
                    }
                }
                // Healing once the casters of the break came (PullOpening.h). Settle also lasts
                // while pulled enemies are still on their way to the tank (PullPlan::packComing;
                // until 2026-11-13 the threat opening held the party for them).
                PullOpening::Update(world, pull, now);
                pull.packComing = elapsed <= PullOpening::HOLD_MAX_MS && world.HasPartyEnemy() &&
                                  !PackArrived(world, pull, tank->pos);
                if (pull.losBreak == PullLosBreak::Active || pull.holdHealing || pull.packComing)
                    return;
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
