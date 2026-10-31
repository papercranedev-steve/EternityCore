#include "Commander/Coordination/InterruptAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/SpellTags.h"
#include "SharedDefines.h"
#include <algorithm>
#include <set>

namespace Commander
{
    namespace
    {
        struct Candidate
        {
            ObjectGuid bot;
            uint32 spellId = 0;
            float score = -1e9f;
        };

        // Best ability of one bot against one enemy.
        Candidate Evaluate(PartyMemberFacts const& m, EnemyFacts const& e, InterruptDuty const* previous, bool needReadyNow)
        {
            Candidate best;
            if (!m.isBot || m.isPet || !m.alive || m.interrupts.empty())
                return best;
            if (needReadyNow && m.casting)
                return best;    // busy with its own cast, would react too late

            float const distance = PackResolver::Distance(m.pos, e.pos);
            for (InterruptAbility const& a : m.interrupts)
            {
                if (!a.ready && needReadyNow)
                    continue;

                float score = 0.0f;
                float const out = distance - (a.maxRange + InterruptAssigner::RANGE_SLACK);
                score += out <= 0.0f ? InterruptAssigner::SCORE_IN_RANGE : -out * InterruptAssigner::SCORE_PER_YARD_OUT;
                score -= (a.cooldownMs / 1000.0f) * InterruptAssigner::SCORE_PER_COOLDOWN_SEC;
                if (!a.ready)
                    score += InterruptAssigner::SCORE_NOT_READY;
                if (m.victim == e.guid)
                    score += InterruptAssigner::SCORE_ALREADY_ON_TARGET;
                if (m.role == ROLE_TANK)
                    score += InterruptAssigner::SCORE_TANK;
                if (previous && previous->primary == m.guid)
                    score += InterruptAssigner::SCORE_STICKY;

                if (score > best.score)
                {
                    best.bot = m.guid;
                    best.spellId = a.spellId;
                    best.score = score;
                }
            }
            return best;
        }
    }

    void InterruptAssigner::Clear(TeamPlan& plan) const
    {
        plan.interrupts.clear();
    }

    void InterruptAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        // Enemies worth an interrupter: casting an interruptible spell now, or seen casting this fight.
        std::vector<EnemyFacts const*> casters;
        for (EnemyFacts const& e : world.enemies)
        {
            if (e.crowdControlled)
                continue;
            if ((e.castingSpellId && e.castInterruptible) || e.lastCastSpellId)
                casters.push_back(&e);
        }

        // Live casts first, most important category first, shortest remaining time first.
        std::stable_sort(casters.begin(), casters.end(), [](EnemyFacts const* a, EnemyFacts const* b)
        {
            bool const la = a->castingSpellId && a->castInterruptible;
            bool const lb = b->castingSpellId && b->castInterruptible;
            if (la != lb)
                return la;
            if (a->castCategory != b->castCategory)
                return a->castCategory > b->castCategory;
            return a->castRemainingMs < b->castRemainingMs;
        });

        std::vector<InterruptDuty> duties;
        std::set<ObjectGuid> busyPrimary;   // a bot is primary for at most one enemy

        for (EnemyFacts const* e : casters)
        {
            bool const live = e->castingSpellId && e->castInterruptible;
            InterruptDuty const* previous = plan.FindInterruptDuty(e->guid);

            std::vector<Candidate> candidates;
            for (PartyMemberFacts const& m : world.party)
            {
                Candidate c = Evaluate(m, *e, previous, live);
                if (c.spellId)
                    candidates.push_back(c);
            }
            std::sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b)
            {
                return a.score > b.score;
            });

            InterruptDuty duty;
            duty.enemy = e->guid;
            duty.castingNow = live;
            duty.category = e->castCategory;
            for (Candidate const& c : candidates)
            {
                if (duty.primary.IsEmpty())
                {
                    if (busyPrimary.count(c.bot))
                        continue;
                    duty.primary = c.bot;
                    duty.primarySpellId = c.spellId;
                    busyPrimary.insert(c.bot);
                }
                else if (c.bot != duty.primary)
                {
                    duty.backup = c.bot;
                    duty.backupSpellId = c.spellId;
                    break;
                }
            }
            // Everyone already primary elsewhere: the best one doubles as backup here.
            if (duty.backup.IsEmpty())
            {
                for (Candidate const& c : candidates)
                {
                    if (c.bot != duty.primary)
                    {
                        duty.backup = c.bot;
                        duty.backupSpellId = c.spellId;
                        break;
                    }
                }
            }

            if (!duty.primary.IsEmpty() || !duty.backup.IsEmpty())
                duties.push_back(duty);
        }

        plan.interrupts = std::move(duties);
    }
}
