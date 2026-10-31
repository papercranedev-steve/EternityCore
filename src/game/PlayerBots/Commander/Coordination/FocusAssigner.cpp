#include "Commander/Coordination/FocusAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/CastValue.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/CreatureProfile.h"
#include "Commander/Perception/SpellProfile.h"
#include "Commander/Perception/SpellTags.h"
#include "Creature.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "SharedDefines.h"
#include "Timer.h"
#include <algorithm>
#include <cstdio>
#include <set>

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

        bool HasHealSpell(CreatureProfile const* profile)
        {
            if (!profile)
                return false;
            for (CreatureSpellRef const& ref : profile->spells)
                if (std::shared_ptr<SpellProfile const> sp = sProfileStore.GetSpell(ref.spellId))
                    if (sp->category == CastCategory::Heal)
                        return true;
            return false;
        }

        bool IsTotem(uint32 entry)
        {
            CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);
            return info && info->type == CREATURE_TYPE_TOTEM;
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
        };

        // Judged on facts only (simple version, see design doc "limits"):
        //  - melee off and a self-destruct spell in its profile: walks up and explodes (scenario 8).
        //  - a summon whose victim is a bot that is not a tank: an add running at the healer or
        //    a damage dealer. Players' roles are unknown and pets take a few hits: not counted.
        bool NeedsIntercept(WorldModel const& world, EnemyFacts const& e, InterceptNeed& out)
        {
            if (!e.acting || e.crowdControlled || !e.health)
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
                    return true;
                }
            }
            if (e.summoned && victim && victim->isBot && !victim->isPet && victim->role != ROLE_TANK)
            {
                out.reason = "summon on non-tank";
                float const horizon = std::min(FocusAssigner::ONE_OFF_HORIZON_SEC, Value::EnemyTimeToDie(world, e));
                out.points = Value::HitLoss(world, e, *victim, horizon);
                return true;
            }
            return false;
        }

        std::vector<InterceptDuty> BuildIntercepts(WorldModel const& world, std::vector<InterceptDuty> const& previous)
        {
            std::vector<InterceptNeed> needs;
            for (EnemyFacts const& e : world.enemies)
            {
                InterceptNeed n;
                if (NeedsIntercept(world, e, n))
                    needs.push_back(n);
            }
            std::stable_sort(needs.begin(), needs.end(), [](InterceptNeed const& a, InterceptNeed const& b)
            {
                return a.distanceToVictim < b.distanceToVictim;
            });

            std::vector<InterceptDuty> duties;
            std::set<ObjectGuid> busy;      // one intercept per bot
            for (InterceptNeed const& n : needs)
            {
                ObjectGuid previousBot;
                for (InterceptDuty const& d : previous)
                    if (d.enemy == n.enemy->guid)
                        previousBot = d.bot;

                // Ranged damage dealers first; melee only if no ranged one can take it.
                PartyMemberFacts const* chosen = nullptr;
                for (uint8 role : { uint8(ROLE_RANGE_DPS), uint8(ROLE_MELEE_DPS) })
                {
                    float const range = role == ROLE_RANGE_DPS ? FocusAssigner::INTERCEPT_RANGE
                                                               : FocusAssigner::MELEE_INTERCEPT_RANGE;
                    float bestDistance = 0.0f;
                    for (PartyMemberFacts const& m : world.party)
                    {
                        if (!m.isBot || m.isPet || !m.CanAct() || m.role != role || busy.count(m.guid))
                            continue;
                        float const distance = PackResolver::Distance(m.pos, n.enemy->pos);
                        if (distance > range)
                            continue;
                        float const ranked = distance - (m.guid == previousBot ? FocusAssigner::INTERCEPT_STICKY_YARDS : 0.0f);
                        if (!chosen || ranked < bestDistance)
                        {
                            chosen = &m;
                            bestDistance = ranked;
                        }
                    }
                    if (chosen)
                        break;
                }
                if (!chosen)
                    continue;

                InterceptDuty d;
                d.enemy = n.enemy->guid;
                d.bot = chosen->guid;
                d.points = n.points;
                d.reason = n.reason;
                busy.insert(chosen->guid);
                duties.push_back(d);
            }
            return duties;
        }
    }

    bool FocusAssigner::IsEligible(WorldModel const& world, EnemyFacts const& e)
    {
        if (!e.acting || e.crowdControlled || !e.health)
            return false;
        // A fresh enemy (summoned add, patrol walking in) is the tank's first: hitting it now
        // pulls it (ThreatBehavior settle window, scenario 6).
        if (e.firstSeenMs &&
            WorldTimer::getMSTimeDiff(e.firstSeenMs, world.refreshedAtMs) < uint32(NEW_ENEMY_SETTLE_SEC * 1000.0f))
            return false;
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
        if (profile && profile->fleeAtHealthPct > 0.0f && e.healthPct > profile->fleeAtHealthPct &&
            e.healthPct <= profile->fleeAtHealthPct + FLEE_WINDOW_PCT)
        {
            oneOff += FLEE_POINTS;
            AddReason(s.reason, "flees at %.0f%%", profile->fleeAtHealthPct);
        }
        if (e.summoned && (IsTotem(e.entry) || HasHealSpell(profile.get())))
        {
            oneOff += SUPPORT_SUMMON_POINTS;
            AddReason(s.reason, "support summon %.0f", SUPPORT_SUMMON_POINTS);
        }
        if (e.summoned && float(e.health) <= dealt * QUICK_KILL_SEC)
        {
            oneOff += QUICK_KILL_POINTS;
            AddReason(s.reason, "dies in %.1fs", float(e.health) / dealt);
        }
        harm += oneOff / ONE_OFF_HORIZON_SEC;

        s.harmRate = harm;
        s.score = harm / s.killSec;
        return s;
    }

    void FocusAssigner::Clear(TeamPlan& plan) const
    {
        plan.focus = FocusTarget();
        plan.intercepts.clear();
    }

    void FocusAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        plan.intercepts = BuildIntercepts(world, plan.intercepts);

        // Intercepted enemies are one bot's job; the party focus stays where it is (scenario 8:
        // a skull on the bomb would pull the tank and the melee over too).
        auto intercepted = [&plan](ObjectGuid guid)
        {
            return std::any_of(plan.intercepts.begin(), plan.intercepts.end(),
                [guid](InterceptDuty const& d) { return d.enemy == guid; });
        };

        EnemyFacts const* best = nullptr;
        FocusScore bestScore;
        for (EnemyFacts const& e : world.enemies)
        {
            if (!IsEligible(world, e) || intercepted(e.guid))
                continue;
            FocusScore s = Evaluate(world, e);
            if (s.score <= 0.0f)
                continue;
            if (!best || s.score > bestScore.score)
            {
                best = &e;
                bestScore = std::move(s);
            }
        }

        if (!best)
        {
            plan.focus = FocusTarget();
            return;
        }

        // Sticky: switching costs every damage dealer a target change, and scores move with
        // health and victims every refresh.
        if (!plan.focus.enemy.IsEmpty() && plan.focus.enemy != best->guid)
        {
            EnemyFacts const* previous = world.FindEnemy(plan.focus.enemy);
            if (previous && IsEligible(world, *previous) && !intercepted(previous->guid))
            {
                FocusScore const kept = Evaluate(world, *previous);
                if (kept.score >= STICKY_SHARE * bestScore.score)
                {
                    plan.focus.score = kept.score;
                    plan.focus.harmRate = kept.harmRate;
                    plan.focus.killSec = kept.killSec;
                    plan.focus.reason = kept.reason;
                    return;
                }
            }
        }

        if (plan.focus.enemy != best->guid)
            plan.focus.sinceMs = world.refreshedAtMs;
        plan.focus.enemy = best->guid;
        plan.focus.score = bestScore.score;
        plan.focus.harmRate = bestScore.harmRate;
        plan.focus.killSec = bestScore.killSec;
        plan.focus.reason = std::move(bestScore.reason);
    }
}
