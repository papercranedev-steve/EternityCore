#include "Commander/Coordination/InterruptAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/WorldModelBuilder.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/SpellTags.h"
#include "Commander/Perception/CastValue.h"
#include "Commander/Perception/ProfileStore.h"
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
            bool canBePrimary = true;   // false: cannot reach the live cast in time (backup only)
        };

        // An enemy worth an interrupter, with the cast its benefit is based on.
        struct Caster
        {
            EnemyFacts const* enemy = nullptr;
            bool live = false;
            CastValue cast;
            uint8 category = 0;
        };

        // Every usable ability of one bot against one enemy. Abilities that cost more than the
        // cast is worth (value-scoring doc 5.1) or that the enemy is immune to are skipped.
        void Evaluate(WorldModel const& world, PartyMemberFacts const& m, Caster const& target,
                      InterruptDuty const* previous, std::vector<Candidate>& out)
        {
            EnemyFacts const& e = *target.enemy;
            // Asleep, stunned, feared or mind controlled: cannot interrupt anything.
            if (!m.isBot || m.isPet || !m.CanAct() || m.interrupts.empty())
                return;
            if (target.live && m.casting)
                return;     // busy with its own cast, would react too late

            float const distance = PackResolver::Distance(m.pos, e.pos);
            for (InterruptAbility const& a : m.interrupts)
            {
                if (!a.ready && target.live)
                    continue;
                // Stun on a stun-immune boss: goes on cooldown, the cast completes.
                if (!CastValues::CanStop(a, e))
                    continue;
                float const benefit = target.cast.points + CastValues::LockoutBenefit(world, e, a, target.cast.spellId);
                if (benefit <= CastValues::InterruptCost(world, m, a, e, target.cast))
                    continue;

                Candidate c;
                c.bot = m.guid;
                c.spellId = a.spellId;
                float score = 0.0f;
                float const outOfRange = distance - (a.maxRange + InterruptAssigner::RANGE_SLACK);
                score += outOfRange <= 0.0f ? InterruptAssigner::SCORE_IN_RANGE : -outOfRange * InterruptAssigner::SCORE_PER_YARD_OUT;
                score -= (a.cooldownMs / 1000.0f) * InterruptAssigner::SCORE_PER_COOLDOWN_SEC;
                if (!a.ready)
                    score += InterruptAssigner::SCORE_NOT_READY;
                if (m.victim == e.guid)
                    score += InterruptAssigner::SCORE_ALREADY_ON_TARGET;
                if (m.role == ROLE_TANK)
                    score += InterruptAssigner::SCORE_TANK;
                if (previous && previous->primary == m.guid)
                    score += InterruptAssigner::SCORE_STICKY;
                // Ranged interrupt behind a pillar: the cast fails with "not in line of sight".
                // Backup at most (it may step out by then). Checked by the builder for live casts.
                bool const noSight = a.maxRange > WorldModelBuilder::MELEE_INTERRUPT_RANGE && !e.InSightOf(m.guid);
                if (noSight)
                {
                    score += InterruptAssigner::SCORE_NO_SIGHT;
                    c.canBePrimary = false;
                }
                // Gouge needs the caster to face the rogue; it faces its victim (usually the
                // tank), the rogue stands at its side or back. Backup at most unless it is on him.
                bool const notInFront = a.needsFront && e.victim != m.guid;
                if (notInFront)
                {
                    score += InterruptAssigner::SCORE_NO_SIGHT;
                    c.canBePrimary = false;
                }
                c.score = score;

                // A 3 s cast and a melee 20 yards away: it arrives after the cast (scenario 2, 3.5 item 2).
                if (target.live && outOfRange > 0.0f && !noSight && !notInFront)
                {
                    float const reachSec = outOfRange / CastValues::RUN_YARDS_PER_SEC + InterruptAssigner::REACTION_SEC;
                    c.canBePrimary = reachSec <= e.castRemainingMs / 1000.0f;
                }
                out.push_back(c);
            }
        }
    }

    void InterruptAssigner::Clear(TeamPlan& plan) const
    {
        plan.interrupts.clear();
    }

    void InterruptAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        // Enemies worth an interrupter: casting an interruptible spell now, or able to cast a
        // valuable interruptible spell (creature profile), also before the first cast.
        std::vector<Caster> casters;
        for (EnemyFacts const& e : world.enemies)
        {
            // Silenced (Serpent Form, our Silence), feigning death, totally immune: nothing to
            // stop now, do not hold an interrupter for it (scenario 3, 3.5).
            if (e.crowdControlled || e.silenced || !e.acting)
                continue;
            // Only fighting an ally: its casts land on the ally, not worth our interrupt
            // cooldowns (planned values would wrongly assume one of us is hit).
            if (e.onAllyOnly)
                continue;
            Caster c;
            c.enemy = &e;
            c.live = e.castingSpellId && e.castInterruptible;
            c.cast = CastValues::InterruptBenefit(world, e);
            if (!c.cast.spellId || c.cast.points <= 0.0f)
                continue;
            if (c.live)
                c.category = e.castCategory;
            else if (std::shared_ptr<SpellProfile const> p = sProfileStore.GetSpell(c.cast.spellId))
                c.category = uint8(p->category);
            casters.push_back(c);
        }

        // Live casts first, most valuable first, shortest remaining time first. By value, not by
        // category: one interrupter must kick the sleep on the healer before a small heal (scenario 3).
        std::stable_sort(casters.begin(), casters.end(), [](Caster const& a, Caster const& b)
        {
            if (a.live != b.live)
                return a.live;
            if (a.cast.points != b.cast.points)
                return a.cast.points > b.cast.points;
            return a.live && a.enemy->castRemainingMs < b.enemy->castRemainingMs;
        });

        std::vector<InterruptDuty> duties;
        std::set<ObjectGuid> busyPrimary;   // a bot is primary for at most one enemy

        for (Caster const& target : casters)
        {
            EnemyFacts const* e = target.enemy;
            InterruptDuty const* previous = plan.FindInterruptDuty(e->guid);

            std::vector<Candidate> candidates;
            for (PartyMemberFacts const& m : world.party)
                Evaluate(world, m, target, previous, candidates);
            std::stable_sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b)
            {
                return a.score > b.score;
            });

            InterruptDuty duty;
            duty.enemy = e->guid;
            duty.castingNow = target.live;
            duty.category = target.category;
            for (Candidate const& c : candidates)
            {
                if (c.canBePrimary && !busyPrimary.count(c.bot))
                {
                    duty.primary = c.bot;
                    duty.primarySpellId = c.spellId;
                    busyPrimary.insert(c.bot);
                    break;
                }
            }
            // Backup: best other bot that is not primary elsewhere; if there is none, the best
            // one already primary elsewhere doubles as backup here.
            for (bool allowBusy : { false, true })
            {
                for (Candidate const& c : candidates)
                {
                    if (c.bot == duty.primary || (!allowBusy && busyPrimary.count(c.bot)))
                        continue;
                    duty.backup = c.bot;
                    duty.backupSpellId = c.spellId;
                    break;
                }
                if (!duty.backup.IsEmpty())
                    break;
            }

            if (!duty.primary.IsEmpty() || !duty.backup.IsEmpty())
                duties.push_back(duty);
        }

        plan.interrupts = std::move(duties);
    }
}
