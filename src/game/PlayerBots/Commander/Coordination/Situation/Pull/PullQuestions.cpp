#include "Commander/Coordination/Situation/Pull/PullQuestions.h"
#include "Commander/Coordination/Situation/Pull/BossWaits.h"
#include "Commander/Coordination/Situation/Pull/Camps.h"
#include "Commander/Coordination/Situation/ClearPlanner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Tanks.h"
#include "Creature.h"
#include "Map.h"
#include "Timer.h"
#include <algorithm>
#include <string>

namespace Commander
{
namespace PullQuestions
{
    namespace
    {
        void Note(TeamPlan& plan, std::string text)
        {
            plan.notes.push_back(std::move(text));
        }

        void ApplyNotReady(TeamPlan& plan, WorldModel const& world, PullQuestion const& q, bool yes, uint32 nowMs)
        {
            PullPlan& pull = plan.pull;
            pull.noDrinkAnswered = true;
            if (!yes)
            {
                Note(plan, "You said wait: waiting for their mana to come back.");
                return;
            }
            ReadyWaiver& waiver = plan.readyWaiver;
            if (nowMs >= waiver.untilMs)
                waiver.bots.clear();
            for (NotReadyMember const& m : q.notReady)
                if (std::find(waiver.bots.begin(), waiver.bots.end(), m.guid) == waiver.bots.end())
                    waiver.bots.push_back(m.guid);
            waiver.untilMs = nowMs + READY_WAIVER_MS;
            // A clear judges the pull with the party as it is before the tank goes
            // (PullPlanner::RecheckBeforeApproach): his yes is to that fight too.
            if (plan.clear.active)
                for (ObjectGuid g : ClearPlanner::PackKey(world, pull.expected))
                    if (std::find(plan.clear.approved.begin(), plan.clear.approved.end(), g) == plan.clear.approved.end())
                        plan.clear.approved.push_back(g);
            Note(plan, "You said go: not waiting for their mana.");
        }

        void Apply(TeamPlan& plan, WorldModel const& world, PullQuestion const& q, uint32 nowMs)
        {
            bool const yes = q.answer == PullQuestion::Answer::Yes;
            if (q.kind == PullAskKind::NotReady)
            {
                ApplyNotReady(plan, world, q, yes, nowMs);
                return;
            }
            if (q.kind == PullAskKind::Area)
            {
                Camps::RememberAnswer(plan.autoClear.camps, q.entries, q.where, yes, nowMs);
                Note(plan, yes ? "You want that camp cleared." : "Leaving that camp alone.");
                return;
            }
            ClearPlan& clear = plan.clear;
            if (clear.ask.first == q.first)
                clear.ask = ClearAsk();
            if (q.kind == PullAskKind::Boss)
                BossWaits::Forget(plan.autoClear, q.pack);
            if (yes)
                clear.approved.insert(clear.approved.end(), q.pack.begin(), q.pack.end());
            else if (q.kind == PullAskKind::Boss)
            {
                // "Wait", not "never": asked again once he comes closer or selects it.
                float approachYd = 0.0f;
                for (PackFacts const& p : world.packs)
                    if (std::any_of(p.members.begin(), p.members.end(), [&q](PackCreature const& c) { return c.guid == q.first; }))
                        approachYd = p.ApproachYd();
                BossWaits::Remember(plan.autoClear, q.pack, approachYd);
                plan.autoClear.bossStartFor = ObjectGuid();
                Note(plan, "You said wait: asking again when you come " +
                    std::to_string(int(BossWaits::CLOSER_YD)) + " yd closer or select it.");
                return;
            }
            else
            {
                clear.skipped.insert(clear.skipped.end(), q.pack.begin(), q.pack.end());
                ++clear.skippedPacks;
            }
            Note(plan, yes ? "You said yes: pulling it." : "You said no: leaving it.");
        }

        PullQuestion& Ask(TeamPlan& plan, ObjectGuid asker, PullAskKind kind, ObjectGuid first, uint32 nowMs)
        {
            PullQuestion& q = plan.pullQuestion;
            q = PullQuestion();
            q.id = plan.autoClear.nextQuestionId++;
            q.kind = kind;
            q.asker = asker;
            q.first = first;
            q.sinceMs = nowMs;
            return q;
        }
    }

    void Step(TeamPlan& plan, WorldModel const& world, Map* map, PackFacts const* areaPack, uint32 nowMs,
              bool stance)
    {
        PullQuestion& q = plan.pullQuestion;
        PullPlan const& pull = plan.pull;
        if (q.id)
        {
            // NotReady: gone with what it was about (the pull went or ended, they have
            // something to drink now or their mana came back).
            if (q.kind == PullAskKind::NotReady && q.answer == PullQuestion::Answer::Pending &&
                (pull.phase != PullPhase::WaitReady || pull.noDrink.empty()))
            {
                q = PullQuestion();
                return;
            }
            if (q.answer == PullQuestion::Answer::Pending &&
                WorldTimer::getMSTimeDiff(q.sinceMs, nowMs) < QUESTION_MAX_MS)
                return;
            // (shown and no answer in time: no; never shown: dropped, asked again later)
            if (q.answer != PullQuestion::Answer::Pending || q.shown)
                Apply(plan, world, q, nowMs);
            q = PullQuestion();
            return;
        }
        if (world.HasPartyEnemy())
            return;
        PartyMemberFacts const* tank = Tanks::Main(world, true);
        if (!tank)
            return;

        if (pull.phase == PullPhase::WaitReady && !pull.noDrinkAnswered && !pull.noDrink.empty() &&
            pull.noDrinkSinceMs && WorldTimer::getMSTimeDiff(pull.noDrinkSinceMs, nowMs) >= NOT_READY_ASK_MS)
        {
            PullQuestion& asked = Ask(plan, tank->guid, PullAskKind::NotReady, pull.target, nowMs);
            asked.pack = pull.expected;
            asked.notReady = pull.noDrink;
            return;
        }
        if (!stance)
            return;

        ClearAsk& ask = plan.clear.ask;
        if (!ask.pack.empty())
        {
            Creature const* first = plan.clear.active ? map->GetCreature(ask.first) : nullptr;
            if (!first || !first->IsAlive() || first->IsInCombat())
                ask = ClearAsk();
            else
            {
                PullQuestion& asked = Ask(plan, tank->guid, ask.kind, ask.first, nowMs);
                asked.count = ask.count;
                asked.pack = ask.pack;
                return;
            }
        }
        if (!areaPack || areaPack->members.empty())
            return;
        ObjectGuid first = areaPack->members.front().guid;
        for (PackCreature const& c : areaPack->members)
            if (c.aggressive && !c.noAggro)
            {
                first = c.guid;
                break;
            }
        PullQuestion& asked = Ask(plan, tank->guid, PullAskKind::Area, first, nowMs);
        for (PackCreature const& c : areaPack->members)
            asked.pack.push_back(c.guid);
        asked.entries = Camps::EntriesOf(*areaPack);
        asked.where = areaPack->center;
    }

    void Drop(TeamPlan& plan)
    {
        if (plan.pullQuestion.kind != PullAskKind::NotReady)
            plan.pullQuestion = PullQuestion();
    }

    bool Open(TeamPlan const& plan, PullQuestion& out)
    {
        if (!plan.pullQuestion.id || plan.pullQuestion.answer != PullQuestion::Answer::Pending)
            return false;
        out = plan.pullQuestion;
        return true;
    }

    void MarkShown(TeamPlan& plan, uint32 id)
    {
        if (plan.pullQuestion.id == id)
            plan.pullQuestion.shown = true;
    }

    void Answer(TeamPlan& plan, uint32 id, bool yes)
    {
        PullQuestion& q = plan.pullQuestion;
        if (q.id == id && q.answer == PullQuestion::Answer::Pending)
            q.answer = yes ? PullQuestion::Answer::Yes : PullQuestion::Answer::No;
    }
}
}
