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
                Position3 planned;
                bool const hasPlanned = pull.PlannedFightSpot(planned);
                std::vector<PullPlanner::Issue> const issues =
                    PullPlanner::CheckReady(world, pull.healerManaPct, hasPlanned ? &planned : nullptr);
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
            {
                // The opener went out this Approach (PullShotState::openedAtMs, the tank's tick).
                PullShotState const& shot = pull.rangedShot;
                uint32 const openedAtMs = shot.approachSinceMs == pull.phaseSinceMs ? shot.openedAtMs : 0;
                std::vector<ObjectGuid> unexpected;
                for (EnemyFacts const& e : world.enemies)
                    // (an ally's fight nearby, e.g. a guard, is not ours)
                    if (!e.onAllyOnly && IsUnexpected(e, pull))
                        unexpected.push_back(e.guid);
                if (fightStarted)
                {
                    // Enemies nobody expected are in this fight too: the opening holds and the
                    // line of sight pull count them.
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
                else if (elapsed > APPROACH_TIMEOUT_MS)
                    cancel("tank did not reach the target in 20 seconds.");
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
                // Settle's own time starts then (the opening holds below start then too).
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
                // while the threat opening holds the party (ThreatOpening.h: it runs after this
                // module, so its result of the last refresh; one refresh late at worst).
                PullOpening::Update(world, pull, now);
                if (pull.losBreak == PullLosBreak::Active || pull.holdHealing || plan.threat.active)
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
