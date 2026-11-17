#include "Commander/Coordination/Duties/InterruptAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/WorldModelBuilder.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Profiles/SpellTags.h"
#include "Commander/Perception/Values/CastValue.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "SharedDefines.h"
#include <algorithm>
#include <string>

namespace Commander
{
    namespace
    {
        struct Candidate
        {
            ObjectGuid bot;
            uint32 spellId = 0;
            float score = -1e9f;
            float net = 0.0f;           // benefit - cost in points (+ STICKY_POINTS): DutyCandidate::value
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

        // The doctrine stamped `enemy` NoMez and `a` leaves a control that ends on the next hit
        // (Gouge): the same rule as the crowd control duty (CrowdControlAssigner). Null: allowed.
        // strat-20261113-235712: a Gouge on a caster in the area pile kept the Blizzard off the
        // pile until the tank, sent to wake it, broke it 1.9 s later.
        std::string DoctrineForbids(TeamPlan const& plan, InterruptAbility const& a, ObjectGuid enemy)
        {
            if (!a.breaksOnDamage || !plan.doctrine.Has(enemy, STAMP_NO_MEZ))
                return std::string();
            DoctrineEnemy const* d = plan.doctrine.Find(enemy);
            return "doctrine: no mez (" + (d ? d->noMezWhy : std::string("?")) + ")";
        }

        // Every usable ability of one bot against one enemy. Abilities that cost more than the
        // cast is worth (value-scoring doc 5.1) or that the enemy is immune to are skipped; the
        // ones the doctrine rules out go to `rejected` (the log only).
        void Evaluate(WorldModel const& world, TeamPlan const& plan, PartyMemberFacts const& m, Caster const& target,
                      InterruptDuty const* previous, std::vector<Candidate>& out, std::vector<DutyCandidate>& rejected)
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
                std::string why = DoctrineForbids(plan, a, e.guid);
                if (!why.empty())
                {
                    DutyCandidate r;
                    r.bot = m.guid;
                    r.spellId = a.spellId;
                    r.why = std::move(why);
                    rejected.push_back(std::move(r));
                    continue;
                }
                float const benefit = target.cast.points + CastValues::LockoutBenefit(world, e, a, target.cast.spellId);
                // The cooldown part is the team's (OfferReserves, DutyAllocator): another bot may
                // still cover what comes next.
                float const cost = CastValues::InterruptActionCost(world, m, a, e);
                if (benefit <= cost)
                    continue;

                Candidate c;
                c.bot = m.guid;
                c.spellId = a.spellId;
                c.net = benefit - cost + (previous && previous->primary == m.guid ? InterruptAssigner::STICKY_POINTS : 0.0f);
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
                // Kept trying it on this enemy and the cast never went out (range, sight, power,
                // form; fight-20261116-232401: Bash failed for 3 s on a Soul Tap channel while
                // the tank stood at another enemy). Backup at most until it works again.
                bool const failing = m.IsFailingCast(a.spellId, e.guid);
                if (failing)
                {
                    score += InterruptAssigner::SCORE_FAILING;
                    c.canBePrimary = false;
                }
                c.score = score;

                // A 3 s cast and a melee 20 yards away: it arrives after the cast (scenario 2, 3.5 item 2).
                if (target.live && outOfRange > 0.0f && !noSight && !notInFront && !failing)
                {
                    // Only a melee interrupt of a bot that is not the tank walks to the caster
                    // (InterruptBehavior "go to caster"); the others wait where they are.
                    bool const walks = a.maxRange <= WorldModelBuilder::MELEE_INTERRUPT_RANGE && m.role != ROLE_TANK;
                    float const reachSec = outOfRange / CastValues::RUN_YARDS_PER_SEC + InterruptAssigner::REACTION_SEC;
                    c.canBePrimary = walks && reachSec <= e.castRemainingMs / 1000.0f;
                }
                out.push_back(c);
            }
        }
    }

    void InterruptAssigner::Clear(TeamPlan& plan) const
    {
        plan.interrupts.clear();
    }

    void InterruptAssigner::Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const
    {
        // Enemies worth an interrupter: casting an interruptible spell now, or able to cast a
        // valuable interruptible spell (creature profile), also before the first cast.
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

            // Live casts first, most valuable first, shortest remaining time first (DutyAllocator
            // order). By value, not by category: one interrupter must kick the sleep on the healer
            // before a small heal (scenario 3).
            DutyNeed need;
            need.kind = DutyKind::Interrupt;
            need.target = e.guid;
            need.now = c.live;
            need.points = c.cast.points;
            need.tieMs = c.live ? e.castRemainingMs : 0;
            need.wantsBackup = true;
            need.detail = c.category;

            InterruptDuty const* previous = plan.FindInterruptDuty(e.guid);
            std::vector<Candidate> candidates;
            for (PartyMemberFacts const& m : world.party)
                Evaluate(world, plan, m, c, previous, candidates, need.rejected);
            for (Candidate const& cand : candidates)
            {
                DutyCandidate d;
                d.bot = cand.bot;
                d.spellId = cand.spellId;
                d.rank = cand.score;
                d.value = cand.net;
                d.valued = true;
                d.canBePrimary = cand.canBePrimary;
                need.candidates.push_back(d);
            }
            out.push_back(std::move(need));
        }
    }

    void InterruptAssigner::OfferReserves(WorldModel const& world, TeamPlan const& plan, std::vector<DutyReserve>& out) const
    {
        for (CastValues::ExpectedCast const& next : CastValues::ExpectedCasts(world))
        {
            EnemyFacts const* e = world.FindEnemy(next.enemy);
            if (!e)
                continue;
            DutyReserve r;
            r.group = next.enemy;
            r.spellId = next.spellId;
            r.points = next.points;
            bool safe = false;      // a cover that is back before it comes: spending it loses nothing
            for (PartyMemberFacts const& m : world.party)
            {
                if (!m.isBot || m.isPet || !m.CanAct())
                    continue;
                for (InterruptAbility const& a : m.interrupts)
                {
                    // On cooldown now (time left not known): neither spendable nor counted on.
                    if (!a.ready || !CastValues::CanStop(a, *e) || !DoctrineForbids(plan, a, e->guid).empty() ||
                        (a.needsFront && e->victim != m.guid))
                        continue;
                    DutyReserve::Cover c;
                    c.bot = m.guid;
                    c.spellId = a.spellId;
                    c.share = CastValues::ShareInCooldown(*e, next, a, false);
                    c.shareOnGroup = CastValues::ShareInCooldown(*e, next, a, true);
                    if (c.share <= 0.0f)
                        safe = true;
                    r.covers.push_back(c);
                }
            }
            if (!safe && !r.covers.empty())
                out.push_back(std::move(r));
        }
    }

    void InterruptAssigner::Apply(WorldModel const& /*world*/, std::vector<DutyResult> const& results, TeamPlan& plan) const
    {
        std::vector<InterruptDuty> duties;
        for (DutyResult const& r : results)
        {
            if (r.kind != DutyKind::Interrupt)
                continue;
            InterruptDuty duty;
            duty.enemy = r.target;
            duty.castingNow = r.now;
            duty.category = r.detail;
            duty.primary = r.primary;
            duty.primarySpellId = r.primarySpellId;
            duty.backup = r.backup;
            duty.backupSpellId = r.backupSpellId;
            duties.push_back(duty);
        }
        plan.interrupts = std::move(duties);
    }
}