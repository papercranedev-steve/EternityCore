#include "Commander/Coordination/Duties/FocusAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Values/CastValue.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Profiles/CreatureProfile.h"
#include "Commander/Perception/Profiles/SpellProfile.h"
#include "Commander/Perception/Profiles/SpellTags.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/SwitchCost.h"
#include "Creature.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "SharedDefines.h"
#include "Timer.h"
#include <algorithm>
#include <cstdio>
#include <limits>

namespace Commander
{
    namespace
    {
        // Seconds between two casts of a profile spell (creature_spells repeat delay), or the
        // CastValue default when the profile has none (EventAI, scripted creatures).
        float RepeatSec(CreatureSpellRef const* ref)
        {
            if (ref && ref->repeatMaxMs)
                return std::max(1.0f, (ref->repeatMinMs + ref->repeatMaxMs) / 2000.0f);
            return CastValues::CAST_INTERVAL_SEC;
        }

        // Same availability rule as CastValues::PlannedCast: on-hit procs are not casts, and
        // EventAI health events count once health is near their threshold.
        bool IsAvailable(CreatureSpellRef const& ref, EnemyFacts const& enemy)
        {
            if (ref.source == SpellSource::OnHit)
                return false;
            if (ref.OncePerFight() && enemy.castingSpellId != ref.spellId && enemy.LastCastStartMs(ref.spellId))
                return false;
            return ref.healthPct <= 0.0f || enemy.healthPct <= ref.healthPct + CastValues::HP_EVENT_WINDOW_PCT;
        }

        // Spell kills its own caster (Walking Bomb Effect: damage around it + instakill self).
        bool IsSelfDestruct(uint32 spellId)
        {
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
            if (!spell)
                return false;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (spell->Effect[i] == SPELL_EFFECT_INSTAKILL && spell->EffectImplicitTargetA[i] == TARGET_UNIT_CASTER)
                    return true;
            return false;
        }

        uint32 SelfDestructSpell(CreatureProfile const* profile)
        {
            if (!profile)
                return 0;
            for (CreatureSpellRef const& ref : profile->spells)
                if (ref.source != SpellSource::OnHit && IsSelfDestruct(ref.spellId))
                    return ref.spellId;
            return 0;
        }

        // The next summon of an EventAI combat timer the party can still prevent by killing `e`
        // within killSec (earliest side of the timer). Points of the summoned creatures and
        // seconds left until it fires; points 0 when nothing can be prevented.
        struct PreventableSummon
        {
            float points = 0.0f;
            float leftSec = 0.0f;
        };

        PreventableSummon NextPreventableSummon(WorldModel const& world, EnemyFacts const& e,
                                                CreatureProfile const* profile, float killSec)
        {
            PreventableSummon best;
            if (!profile || profile->summonTimers.empty() || !e.firstSeenMs)
                return best;
            // EventAI combat timers start when it enters combat, about when it joins our lists.
            uint32 const sinceMs = WorldTimer::getMSTimeDiff(e.firstSeenMs, world.refreshedAtMs);
            for (SummonTimer const& t : profile->summonTimers)
            {
                uint32 nextMs = t.initialMinMs;
                if (sinceMs >= nextMs)
                {
                    if (!t.repeatMinMs)
                        continue;
                    nextMs += ((sinceMs - nextMs) / t.repeatMinMs + 1) * t.repeatMinMs;
                }
                float const leftSec = (nextMs - sinceMs) / 1000.0f;
                if (leftSec <= killSec)
                    continue;       // fires before we can kill it: nothing to win
                float const points = CastValues::SummonCreaturePoints(world, e.level, t.creatureEntry,
                                                                     float(t.count), t.lifeMs);
                float const value = points >= 0.0f ? points : CastValues::SUMMON_POINTS * t.count;
                if (!best.points || leftSec < best.leftSec)
                {
                    best.points = value;
                    best.leftSec = leftSec;
                }
            }
            return best;
        }

        void AddReason(std::string& reason, char const* fmt, float value)
        {
            char buf[48];
            snprintf(buf, sizeof(buf), fmt, value);
            if (!reason.empty())
                reason += ", ";
            reason += buf;
        }

        // Must-kill enemy, before choosing who takes it.
        struct InterceptNeed
        {
            EnemyFacts const* enemy = nullptr;
            float points = 0.0f;
            float distanceToVictim = 0.0f;  // closest to its victim = most urgent
            char const* reason = "";
            bool rangedOnly = false;        // melee would walk to it
            bool loose = false;             // on a member who is not a tank (team rescue is the alternative)
            // Urgent tier (FocusAssigner::DETAIL_URGENT): a bomb, a patrol's caster, a summon whose
            // victim dies within the horizon. Else Assigned, against the other duties by points.
            bool urgent = false;
            // Killed to stop its cast (CastKill): a candidate must kill it alone within this
            // many seconds. 0 = no time limit.
            float killBeforeSec = 0.0f;
        };

        // A caster standing in a patrol's path that no lure brings out, or one that brings
        // more and stands there (PatrolAssigner, same refresh: it runs in the situation stage).
        // Worth what the patrol would cost.
        bool PatrolKill(WorldModel const& world, TeamPlan const& plan, EnemyFacts const& e, InterceptNeed& out)
        {
            if (!(plan.patrolGuard.active &&
                  std::find(plan.patrolGuard.kill.begin(), plan.patrolGuard.kill.end(), e.guid) != plan.patrolGuard.kill.end() &&
                  e.acting && !e.crowdControlled && e.health))
                return false;
            PartyMemberFacts const* victim = world.FindMember(e.victim);
            out.enemy = &e;
            out.distanceToVictim = victim ? PackResolver::Distance(e.pos, victim->pos) : 0.0f;
            out.reason = "in a patrol's path";
            out.points = plan.patrolGuard.points;
            out.rangedOnly = true;
            out.urgent = true;
            return true;
        }

        // Casting something worth stopping now, and one damage dealer may kill it before the
        // cast ends (the candidates' check: FocusAssigner::CastKillSec). For when no interrupter
        // reaches it (strat-20261126-224046: Eye of Naxxramas, 278 health, cast Summon Rockwing
        // Gargoyles for 10 s 37 yd off; only a Bash and a Hammer of Justice were offered).
        // Worth the cast x CAST_KILL_SHARE: an interrupter on it goes first (DutyKeyKind::
        // CastStopped). Ranged only while it stands in a patrol's path.
        bool CastKill(WorldModel const& world, TeamPlan const& plan, EnemyFacts const& e, InterceptNeed& out)
        {
            if (!e.acting || e.crowdControlled || !e.health || e.onAllyOnly ||
                !e.castingSpellId || !e.castInterruptible || !e.castRemainingMs)
                return false;
            if (plan.doctrine.Has(e.guid, STAMP_SWARM | STAMP_HOLD_FIRE))
                return false;
            CastValue const cast = CastValues::InterruptBenefit(world, e);
            if (!cast.spellId || cast.points <= 0.0f)
                return false;
            PartyMemberFacts const* victim = world.FindMember(e.victim);
            out.enemy = &e;
            out.distanceToVictim = victim ? PackResolver::Distance(e.pos, victim->pos) : 0.0f;
            out.reason = "kill before its cast ends";
            out.points = cast.points * FocusAssigner::CAST_KILL_SHARE;
            out.rangedOnly = plan.patrolGuard.Avoids(e.guid);
            out.urgent = cast.lethal || cast.manaCritical;
            out.killBeforeSec = e.castRemainingMs / 1000.0f;
            return true;
        }

        // Judged on facts only (simple version, see design doc "limits"):
        //  - melee off and a self-destruct spell in its profile: walks up and explodes (scenario 8).
        //  - a summon whose victim is a member that is not a tank (Perception/Tanks.h; players
        //    too since their role is known): an add running at the healer or a damage dealer.
        //    Pets take a few hits: not counted.
        bool NeedsIntercept(WorldModel const& world, TeamPlan const& plan, EnemyFacts const& e, InterceptNeed& out)
        {
            if (!e.acting || e.crowdControlled || !e.health)
                return false;
            // A trap's swarm is the area spells' (RoleStamps.h Swarm; strat-20261112-204141: the
            // mage shot Frostbolts at crate insects worth 0.8 points instead of its Blizzard).
            if (plan.doctrine.Has(e.guid, STAMP_SWARM))
                return false;
            PartyMemberFacts const* victim = world.FindMember(e.victim);
            out.enemy = &e;
            out.distanceToVictim = victim ? PackResolver::Distance(e.pos, victim->pos) : 0.0f;

            std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(e.entry);
            if (!e.meleeEnabled)
            {
                if (uint32 const bomb = SelfDestructSpell(profile.get()))
                {
                    out.reason = "self-destructs";
                    out.points = CastValues::Evaluate(world, e, bomb, e.victim).points;
                    out.urgent = true;
                    return true;
                }
            }
            if (e.summoned && victim && !victim->isPet && !Tanks::IsTank(*victim))
            {
                out.reason = "summon on non-tank";
                out.loose = true;
                float const horizon = std::min(FocusAssigner::ONE_OFF_HORIZON_SEC, Value::EnemyTimeToDie(world, e));
                out.points = Value::HitLoss(world, e, *victim, horizon);
                out.urgent = Value::SurviveAllSec(world, e, *victim) < horizon;
                return true;
            }
            return false;
        }

        // Every enemy that needs an intercept, with the bots that may take it. Rank =
        // -(switch cost - stickiness): the cheapest switch first. Only candidates for whom the
        // intercept is worth more than their switch (points > SwitchCost) are offered.
        void OfferIntercepts(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out)
        {
            for (EnemyFacts const& e : world.enemies)
            {
                InterceptNeed n;
                bool found = NeedsIntercept(world, plan, e, n);
                if (!found)
                    found = PatrolKill(world, plan, e, n = InterceptNeed());
                if (!found)
                    found = CastKill(world, plan, e, n = InterceptNeed());
                if (!found)
                    continue;

                ObjectGuid previousBot;
                for (InterceptDuty const& d : plan.intercepts)
                    if (d.enemy == e.guid)
                        previousBot = d.bot;

                DutyNeed need;
                need.kind = DutyKind::Intercept;
                need.target = e.guid;
                need.now = true;
                need.points = n.points;
                // A summon on a non-tank is also a loose enemy (RescueAssigner): killing it or
                // taking it off its victim, one of them.
                if (n.loose)
                {
                    need.blockedBy.push_back({ DutyKeyKind::EnemyHandled, e.guid });
                    need.marks.push_back({ DutyKeyKind::EnemyHandled, e.guid });
                }
                // Killed to stop its cast: not while an interrupter is on that cast.
                if (n.killBeforeSec > 0.0f)
                {
                    need.blockedBy.push_back({ DutyKeyKind::CastStopped, e.guid });
                    need.marks.push_back({ DutyKeyKind::CastStopped, e.guid });
                }
                // Equal points: closest to its victim first (the most urgent).
                need.tieMs = uint32(n.distanceToVictim * 1000.0f);
                need.reason = n.reason;
                need.detail = n.urgent ? FocusAssigner::DETAIL_URGENT : uint8(0);

                for (PartyMemberFacts const& m : world.party)
                {
                    if (!m.isBot || m.isPet || !m.CanAct())
                        continue;
                    bool const ranged = m.role == ROLE_RANGE_DPS;
                    if (!ranged && (m.role != ROLE_MELEE_DPS || n.rangedOnly))
                        continue;
                    // In a patrol's path: within spell range, they do not walk closer. Melee only
                    // when the enemy is already close (no running across the room).
                    float const range = n.rangedOnly ? FocusAssigner::PATROL_INTERCEPT_RANGE :
                                        ranged ? FocusAssigner::INTERCEPT_RANGE : FocusAssigner::MELEE_INTERCEPT_RANGE;
                    float const distance = PackResolver::Distance(m.pos, e.pos);
                    if (distance > range)
                        continue;
                    float const reach = ranged ? FocusAssigner::RANGED_REACH : FocusAssigner::MELEE_REACH;
                    if (n.killBeforeSec > 0.0f &&
                        FocusAssigner::CastKillSec(m, e, distance - e.boundingRadius - reach) > n.killBeforeSec)
                        continue;
                    float const cost = SwitchCost::Of(world, m, e, reach);
                    if (n.points <= cost)
                        continue;
                    float sticky = 0.0f;
                    if (m.guid == previousBot)
                        sticky = Value::LostAction(world, m, FocusAssigner::INTERCEPT_STICKY_YARDS / CastValues::RUN_YARDS_PER_SEC);
                    DutyCandidate c;
                    c.bot = m.guid;
                    c.rank = sticky - cost;
                    c.value = n.points + c.rank;
                    c.valued = true;
                    need.candidates.push_back(c);
                }
                out.push_back(std::move(need));
            }
        }
    }
    float FocusAssigner::CastKillSec(PartyMemberFacts const& m, EnemyFacts const& e, float walkYd)
    {
        if (m.soloDps <= 0.0f)
            return std::numeric_limits<float>::infinity();
        return SwitchCost::SWITCH_SEC + std::max(0.0f, walkYd) / CastValues::RUN_YARDS_PER_SEC +
               float(e.health) / m.soloDps;
    }

    bool FocusAssigner::IsEligible(WorldModel const& world, EnemyFacts const& e, bool allowAllyOnly)
    {
        // A runner stays the focus (todo 13: the party was turning away from it, letting it
        // run off for help).
        if (!e.acting || (e.crowdControlled && !e.HitWhileControlled()) || !e.health)
            return false;
        // Only fighting an ally (a guard, an escort): not our fight unless a boss module says so
        // (Zul'Farrak: adds on Weegli are a focus tier).
        if (e.onAllyOnly && !allowAllyOnly)
            return false;
        // A fresh enemy (summoned add, patrol walking in) is the tank's first: hitting it now
        // pulls it (ThreatBehavior settle window, scenario 6).
        // Except one that summons on a combat timer we can still beat: the settle window would
        // eat most of Mobile Alert System's 6.8 s.
        if (e.firstSeenMs &&
            WorldTimer::getMSTimeDiff(e.firstSeenMs, world.refreshedAtMs) < uint32(NEW_ENEMY_SETTLE_SEC * 1000.0f))
        {
            std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(e.entry);
            float const killSec = std::max(MIN_KILL_SEC, float(e.health) / Value::DealtDps(world));
            if (NextPreventableSummon(world, e, profile.get(), killSec).points <= 0.0f)
                return false;
        }
        return true;
    }

    FocusScore FocusAssigner::Evaluate(WorldModel const& world, EnemyFacts const& e)
    {
        FocusScore s;
        float const dealt = Value::DealtDps(world);
        s.killSec = std::max(MIN_KILL_SEC, float(e.health) / dealt);

        // Base: what it deals to whoever it is hitting now (scenario 6: dps / remaining health).
        float dps = 0.0f;
        if (PartyMemberFacts const* victim = world.FindMember(e.victim))
            dps = Value::HitDps(e, *victim);
        else if (e.acting)
            dps = e.meleeDps + e.rangedDps;
        float harm = Value::Points(world, dps);
        AddReason(s.reason, "dps %.2f/s", harm);

        std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(e.entry);

        // Heals and mana drains: value of one cast (CastValues::Evaluate, the same points the
        // interrupt assigner uses) spread over its repeat delay = points per second while it
        // lives. Only the best spell of each kind: it casts one thing at a time. Not
        // PlannedCast: that one only looks at interruptible spells and mixes categories.
        float healRate = 0.0f;
        float drainRate = 0.0f;
        bool lastSeen = false;
        auto consider = [&](uint32 spellId, CreatureSpellRef const* ref)
        {
            std::shared_ptr<SpellProfile const> sp = sProfileStore.GetSpell(spellId);
            if (!sp || (sp->category != CastCategory::Heal && sp->category != CastCategory::ManaDrain))
                return;
            float const rate = CastValues::Evaluate(world, e, spellId, ObjectGuid()).points / RepeatSec(ref);
            float& slot = sp->category == CastCategory::Heal ? healRate : drainRate;
            slot = std::max(slot, rate);
        };
        if (profile)
        {
            for (CreatureSpellRef const& ref : profile->spells)
            {
                if (!IsAvailable(ref, e))
                    continue;
                lastSeen = lastSeen || ref.spellId == e.lastCastSpellId;
                consider(ref.spellId, &ref);
            }
        }
        // C++ scripted creatures have no profile spells: the last cast seen this fight.
        if (e.lastCastSpellId && !lastSeen)
            consider(e.lastCastSpellId, nullptr);
        if (healRate > 0.0f)
        {
            harm += healRate;
            AddReason(s.reason, "heals %.2f/s", healRate);
        }
        if (drainRate > 0.0f)
        {
            harm += drainRate;
            AddReason(s.reason, "mana burn %.2f/s", drainRate);
        }

        // One-off bonuses, as "lost within ONE_OFF_HORIZON_SEC if still alive".
        float oneOff = 0.0f;
        // About to run away, or running: what having the party on it saves (the whole run's
        // joiners minus those a focused kill still lets in, FleeFacts::FocusSaves; todo 13).
        if (e.flee.predicted && e.flee.FocusSaves() > 0.0f)
        {
            oneOff += e.flee.FocusSaves();
            AddReason(s.reason, "focus saves flee %.1f", e.flee.FocusSaves());
        }
        // Summoned totems and healers (Healing Ward, scenario 11) used to get a one-off bonus
        // here; since 2026-11-10 they are the doctrine's kill order (STAMP_KILL_ORDER).
        if (e.summoned && float(e.health) <= dealt * QUICK_KILL_SEC)
        {
            oneOff += QUICK_KILL_POINTS;
            AddReason(s.reason, "dies in %.1fs", float(e.health) / dealt);
        }
        harm += oneOff / ONE_OFF_HORIZON_SEC;

        // Summons on a combat timer (Mobile Alert System, Wandering Eye of Kilrogg): lost when
        // the timer fires, so spread over the seconds left instead of ONE_OFF_HORIZON_SEC.
        PreventableSummon const summon = NextPreventableSummon(world, e, profile.get(), s.killSec);
        if (summon.points > 0.0f)
        {
            harm += summon.points / std::max(1.0f, summon.leftSec);
            AddReason(s.reason, "summons in %.1fs", summon.leftSec);
        }

        s.harmRate = harm;
        s.score = harm / s.killSec;
        return s;
    }

    void FocusAssigner::Clear(TeamPlan& plan) const
    {
        plan.intercepts.clear();
    }

    void FocusAssigner::Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const
    {
        OfferIntercepts(world, plan, out);
    }

    void FocusAssigner::Apply(WorldModel const& /*world*/, std::vector<DutyResult> const& results, TeamPlan& plan) const
    {
        std::vector<InterceptDuty> intercepts;
        for (DutyResult const& r : results)
        {
            if (r.kind != DutyKind::Intercept || r.primary.IsEmpty())
                continue;
            InterceptDuty d;
            d.enemy = r.target;
            d.bot = r.primary;
            d.points = r.points;
            d.reason = r.reason;
            intercepts.push_back(d);
        }
        plan.intercepts = std::move(intercepts);
        // The party focus is chosen by TargetAssigner since 2026-11-07 (with every bot's target).
    }
}
