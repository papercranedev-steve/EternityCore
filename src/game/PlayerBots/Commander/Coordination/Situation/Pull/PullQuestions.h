/*
 * PartyBot Commander layer 2 (situation), the pull stance: the questions to the player before
 * a pull (TeamPlan::pullQuestion; design .kiro/docs/2026-11-24-pull-stance.md §4).
 *
 * One at a time, not during a fight, asked by the main tank bot (Tanks::Main). The tank's life
 * shows it (Companions/Fight/PullQuestion: popup and party chat; MarkShown), the answer comes
 * back through CommanderMgr (Answer). Then:
 *   Area     (the open world, PackJudge Ask) the answer is kept for the pack's camp (Camps.h)
 *   Boss / TooMany  (a clear put the pack up, ClearPlan::ask) yes: ClearPlan::approved; no: skipped
 * Shown and unanswered within QUESTION_MAX_MS: no. Never shown (the tank had another question
 * open): dropped, asked again later.
 */

#ifndef MANGOS_COMMANDER_PULL_QUESTIONS_H
#define MANGOS_COMMANDER_PULL_QUESTIONS_H

#include "Common.h"

class Map;

namespace Commander
{
    struct TeamPlan;
    struct PullQuestion;
    struct PackFacts;
    struct WorldModel;

    namespace PullQuestions
    {
        uint32 const QUESTION_MAX_MS = 60 * 1000;

        // Takes the answer in, or asks the next question: a pack a clear put up first, else
        // `areaPack` (may be null).
        void Step(TeamPlan& plan, WorldModel const& world, Map* map, PackFacts const* areaPack, uint32 nowMs);
        // The stance turned passive: the open question goes (its popup closes).
        void Drop(TeamPlan& plan);

        // The question waiting for the player's answer (false: none).
        bool Open(TeamPlan const& plan, PullQuestion& out);
        // The asker showed question `id` to the player (PullQuestion::shown).
        void MarkShown(TeamPlan& plan, uint32 id);
        // The player answered question `id` (an old id: ignored).
        void Answer(TeamPlan& plan, uint32 id, bool yes);
    }
}

#endif
