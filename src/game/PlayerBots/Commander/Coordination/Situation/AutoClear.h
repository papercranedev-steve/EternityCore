/*
 * PartyBot Commander layer 2 (situation): the player's active pull stance (PullStance.h, the
 * talk window's 战斗策略 page). The party does not wait for the player to start every fight.
 * Design: .kiro/docs/2026-11-24-pull-stance.md (workspace root).
 *
 * Only with a real player leading (a team of bots hunts on its own, Hunt/HuntAssigner), the
 * pull module on and a tank bot alive (Tanks::Main, bots only): without one it is passive.
 *
 * Dungeon / raid: a plain clear (ClearPlanner, its defaults) around the player once he stood
 * still LEADER_STILL_MS and nothing else runs; after it ended: nothing left -> once he moved
 * MOVED_YD or RETRY_MS later; any other end (deaths, a failed pull, stopped) -> once he moved
 * MOVED_YD. A boss or a pack over the limit is asked about (ClearPlan::asks) instead of skipped.
 *
 * Open world: a standing clear (ClearPlan::standing, chosenOnly) that pulls only the packs
 * chosen here every refresh (Judge):
 *   Pull     it will start a fight anyway (a member within its aggro range + FIGHT_MARGIN_YD),
 *            the player has it selected (not an elite), a quest of a member still needs it
 *            (QuestNeeds; elites asked), or its camp is one he wants (he started a fight there
 *            himself, or answered "clear").
 *   Ask      the player wanders here (his trail of the last WANDER_MS within WANDER_YD; standing
 *            still too) and the pack gives experience and starts fights; elites he selected or
 *            a quest needs (also while he travels).
 *   Exclude  bosses, other players' (tapped, claimed), more than MAX_LEVEL_ABOVE over him,
 *            gray or neutral (unless a quest's), a camp he answered "leave", anything else
 *            while he travels; everything while he is dead, mounted, flying, resting, in a
 *            battleground, or the main tank is told to wait, a guide, a retreat, a push.
 * He walks away from the pack before the tank reached it: that pull is called off (ClearPlanner).
 *
 * Camps (TeamPlan CampMemory): creatures of the same kinds standing near one another. He
 * started a fight himself: an idle creature he selected that would not have come on its own
 * (WatchSelection; watching stops once it would) gets into a fight our pull did not start, and
 * he attacks it or it attacks him (HisFight); its camp is pulled STARTED_MEMORY_MS from his last
 * such fight. A creature that came to us is no sign: such packs stay as they are (asked while he
 * wanders). His answer is kept ANSWER_MEMORY_MS. A camp grows by the spots of its pulled packs
 * and is dropped once he is CAMP_FORGET_YD from all of them.
 *
 * Questions (TeamPlan::pullQuestion, one at a time, not during a fight): the main tank asks
 * (Companions/Fight/PullQuestion opens the popup; the answer comes back through
 * CommanderMgr::AnswerPullQuestion). Shown and unanswered within QUESTION_MAX_MS: no; never
 * shown (the tank had another question open): dropped, asked again later.
 *
 * Passive again: a clear AutoClear started stops (a pull already under way is finished) and
 * the open question is dropped.
 *
 * Runs on the map thread from PartyCommander::Update, after ClearPlanner::Step.
 */

#ifndef MANGOS_COMMANDER_AUTO_CLEAR_H
#define MANGOS_COMMANDER_AUTO_CLEAR_H

#include "Common.h"

class TeamBotAI;

namespace Commander
{
    class PartyCommander;
    struct TeamPlan;
    struct PullQuestion;

    namespace AutoClear
    {
        // Dungeon
        uint32 const LEADER_STILL_MS = 5000;        // the player stood still this long ...
        float const LEADER_STILL_RADIUS = 2.0f;     // ... within this distance
        float const MOVED_YD = 20.0f;               // moved this far from where the last clear ended
        uint32 const RETRY_MS = 30 * 1000;          // nothing was left: look again after this long

        // Open world
        float const WORLD_RADIUS = 30.0f;           // packs this far (walk) from the player
        float const FIGHT_MARGIN_YD = 3.0f;         // "will start a fight anyway": aggro range + this
        uint32 const MAX_LEVEL_ABOVE = 3;
        uint32 const TRAIL_STEP_MS = 1000;          // one trail point per second
        uint32 const WANDER_MS = 15 * 1000;         // trail looked at for "wanders here"
        float const WANDER_YD = 30.0f;              // ... all of it within this of where he is

        // Questions
        uint32 const QUESTION_MAX_MS = 60 * 1000;   // shown and unanswered this long: no; never shown: dropped

        // Camps (TeamPlan CampMemory)
        float const CAMP_YD = 50.0f;                // a pack of the camp's kinds this close to one of its spots
        float const CAMP_SPOT_STEP_YD = 15.0f;      // a pulled pack farther than this from every spot adds one
        uint32 const MAX_CAMP_SPOTS = 20;
        float const CAMP_FORGET_YD = 150.0f;        // dropped once he is this far from all its spots
        uint32 const ANSWER_MEMORY_MS = 10 * MINUTE * IN_MILLISECONDS;     // from his answer
        uint32 const STARTED_MEMORY_MS = 5 * MINUTE * IN_MILLISECONDS;     // from his last fight there

        void Step(TeamBotAI* bot, PartyCommander& commander, uint32 nowMs);

        // The question waiting for the player's answer (false: none).
        bool OpenQuestion(TeamPlan const& plan, PullQuestion& out);
        // The asker showed question `id` to the player (PullQuestion::shown).
        void MarkShown(TeamPlan& plan, uint32 id);
        // The player answered question `id` (an old id: ignored).
        void Answer(TeamPlan& plan, uint32 id, bool yes);
    }
}

#endif
