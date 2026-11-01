#include "Commander/Perception/CastValue.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/WorldModelBuilder.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/PackResolver.h"
#include "Creature.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "SharedDefines.h"
#include "Timer.h"
#include <algorithm>
#include <atomic>
#include <functional>
#include <unordered_map>

namespace Commander
{
namespace CastValues
{
    namespace
    {
        // Castable now for planning: procs are not casts; EventAI health events only near
        // their threshold (the Scarlet Adept heal below 50% counts from 65% on).
        bool IsAvailable(CreatureSpellRef const& ref, EnemyFacts const& enemy)
        {
            if (ref.source == SpellSource::OnHit)
                return false;
            return ref.healthPct <= 0.0f || enemy.healthPct <= ref.healthPct + HP_EVENT_WINDOW_PCT;
        }

        float RepeatSec(CreatureSpellRef const* ref)
        {
            return ref && ref->repeatMaxMs ? (ref->repeatMinMs + ref->repeatMaxMs) / 2000.0f : 0.0f;
        }

        float CastSec(SpellProfile const& p)
        {
            return (p.channeled ? p.durationMs : p.castMs) / 1000.0f;
        }

        // How long the current cast of `enemy` has been running, if it is `spellId`. The
        // creature's repeat delay starts with the cast (CreatureAI.cpp ~152), interrupted or not.
        float CastElapsedSec(EnemyFacts const& enemy, uint32 spellId)
        {
            if (enemy.castingSpellId != spellId || enemy.castTotalMs < enemy.castRemainingMs)
                return 0.0f;
            return (enemy.castTotalMs - enemy.castRemainingMs) / 1000.0f;
        }

        // Seconds `spellId` still cannot be recast by `enemy` because of its repeat delay, which
        // runs from the start of its latest cast (EnemyFacts::castStarts, at world refresh time).
        // 0 when it was never seen cast or has no repeat delay. Not for the cast being
        // interrupted: that one restarts its delay (callers use CastElapsedSec for it).
        float RepeatWaitSec(WorldModel const& world, EnemyFacts const& enemy, uint32 spellId, CreatureSpellRef const* ref)
        {
            if (!ref || !ref->repeatMinMs)
                return 0.0f;
            uint32 const lastStartMs = enemy.LastCastStartMs(spellId);
            if (!lastStartMs)
                return 0.0f;
            float const sinceSec = WorldTimer::getMSTimeDiff(lastStartMs, world.refreshedAtMs) / 1000.0f;
            return std::max(0.0f, ref->repeatMinMs / 1000.0f - sinceSec);
        }

        // Interruptible spells `enemy` may cast: profile entries plus the last cast seen (C++
        // scripted creatures have no profile spells). `fn(spellId, ref or nullptr, profile)`.
        template <typename Fn>
        void ForEachInterruptibleCast(EnemyFacts const& enemy, Fn fn)
        {
            bool lastSeen = false;
            if (std::shared_ptr<CreatureProfile const> creature = sProfileStore.GetCreature(enemy.entry))
            {
                for (CreatureSpellRef const& ref : creature->spells)
                {
                    if (!IsAvailable(ref, enemy))
                        continue;
                    std::shared_ptr<SpellProfile const> sp = sProfileStore.GetSpell(ref.spellId);
                    if (!sp || !sp->interruptible)
                        continue;
                    lastSeen = lastSeen || ref.spellId == enemy.lastCastSpellId;
                    fn(ref.spellId, &ref, *sp);
                }
            }
            if (enemy.lastCastSpellId && !lastSeen)
                if (std::shared_ptr<SpellProfile const> sp = sProfileStore.GetSpell(enemy.lastCastSpellId))
                    if (sp->interruptible)
                        fn(enemy.lastCastSpellId, nullptr, *sp);
        }

        // Planning a heal without a target: it goes to an injured ally of the caster.
        EnemyFacts const* MostInjured(WorldModel const& world)
        {
            EnemyFacts const* best = nullptr;
            for (EnemyFacts const& e : world.enemies)
                if (e.acting && (!best || e.maxHealth - e.health > best->maxHealth - best->health))
                    best = &e;
            return best;
        }

        float ControlLoss(WorldModel const& world, SpellProfile const& profile, SpellAmounts const& amounts,
                          PartyMemberFacts const& member)
        {
            float seconds = amounts.controlSec;
            // Sleep on someone being hit: the next hit wakes them (scenario 3, sleep on the tank).
            if (profile.breaksOnDamage && Value::IncomingOn(world, member.guid) > 0.0f)
                seconds = std::min(seconds, WAKE_ON_DAMAGE_SEC);
            return Value::LostAction(world, member, seconds);
        }

        float ManaLoss(WorldModel const& world, SpellAmounts const& amounts, PartyMemberFacts const& member)
        {
            // Burn / drain does nothing to a target without mana as its active power: manaPct,
            // not UsesMana(). A druid in bear / cat form is immune (Spell::EffectPowerBurn
            // and EffectPowerDrain check GetPowerType()).
            if (member.manaPct < 0.0f)
                return 0.0f;
            float points = Value::Points(world, amounts.mana * HEAL_PER_MANA + amounts.TotalDamage());
            if (member.isBot && member.role == ROLE_HEALER)
                points *= MANA_DRAIN_HEALER_MULT;
            return points;
        }

        // Average over the party for a target not known yet (random target, or planning).
        template <typename Fn>
        float PartyAverage(WorldModel const& world, Fn lossOf)
        {
            float total = 0.0f;
            uint32 count = 0;
            for (PartyMemberFacts const& m : world.party)
            {
                if (m.isPet || !m.CanAct())
                    continue;
                total += lossOf(m);
                ++count;
            }
            return count ? total / float(count) : 0.0f;
        }

        // Damage of the summoned creatures while they exist, plus the fight lasting as long as
        // it takes us to kill them. Health = creature_classlevelstats x health_multiplier, as in
        // Creature::InitStatsForLevel. Returns < 0 when the summon is unknown.
        float SummonPoints(WorldModel const& world, EnemyFacts const& enemy, SpellEntry const* spell,
                           SpellAmounts const& amounts)
        {
            if (!amounts.summonEntry)
                return -1.0f;
            std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(amounts.summonEntry);
            CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(amounts.summonEntry);
            if (!profile || !info)
                return -1.0f;

            // Spell::EffectSummonWild and friends use the caster's level.
            uint32 const level = std::max(1u, enemy.level);
            CreatureClassLevelStats const* cls = sObjectMgr.GetCreatureClassLevelStats(profile->unitClass ? profile->unitClass : 1, level);
            if (!cls)
                return -1.0f;
            float const count = std::max(1.0f, amounts.summonCount);
            float const health = Creature::_GetHealthMod(int32(info->rank)) * cls->health * info->health_multiplier * count;

            int32 const durationMs = spell->GetDuration();
            float const lifeSec = durationMs > 0 ? std::min(durationMs / 1000.0f, SUMMON_MAX_SEC) : SUMMON_DEFAULT_SEC;
            // Totems do not melee (their effect is a spell we do not model here).
            float const dps = info->type == CREATURE_TYPE_TOTEM ? 0.0f : CreatureProfiles::EstimateMeleeDps(*profile, level);

            return Value::Points(world, dps * lifeSec * count) +
                   Value::FightExtension(world, health / Value::DealtDps(world));
        }
    }

    bool CanStop(InterruptAbility const& ability, EnemyFacts const& enemy)
    {
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(ability.spellId);
        std::shared_ptr<CreatureProfile const> creature = sProfileStore.GetCreature(enemy.entry);
        if (!spell || !creature)
            return true;
        // Hammer of Justice on a stun-immune boss "succeeds" (immunity is checked on hit) and
        // goes on cooldown while the cast completes (scenario 7, problem 4).
        return !SpellTags::IsStopImmune(spell, creature->mechanicImmuneMask, creature->schoolImmuneMask);
    }

    // Evaluate without the planning cache. With an empty target it reads from `enemy` only
    // entry, level, meleeHit, victim and maxHealth (guid and pos only matter for a cast at
    // the caster itself). Keep SamePlanInputs below in step when this changes.
    static CastValue EvaluateUncached(WorldModel const& world, EnemyFacts const& enemy, uint32 spellId, ObjectGuid target)
    {
        CastValue v;
        v.spellId = spellId;
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
        std::shared_ptr<SpellProfile const> profile = sProfileStore.GetSpell(spellId);
        if (!spell || !profile)
            return v;

        // Who it lands on: the cast's unit target if that is one of us, else the enemy's victim.
        bool const known = !target.IsEmpty();
        bool const onCaster = known && target == enemy.guid;
        PartyMemberFacts const* victim = known ? world.FindMember(target) : nullptr;
        // A unit target that is not one of us (Shadow Bolt at a friendly NPC, a heal on an
        // enemy): its harmful part hits nobody we care about. Do not fall back to the victim.
        bool const offParty = known && !onCaster && !victim;
        if (!known)
            victim = world.FindMember(enemy.victim);

        SpellAmounts const amounts = SpellProfiles::Estimate(spell, enemy.level, enemy.meleeHit);
        switch (profile->category)
        {
            case CastCategory::Heal:
            {
                // Healing is capped by the missing health of the healed enemy, when we see it
                // (not on our threat lists = no cap). Full heals heal exactly the missing health.
                EnemyFacts const* healed = known ? world.FindEnemy(target) : MostInjured(world);
                float amount = amounts.TotalHeal();
                if (healed)
                {
                    float const missing = float(healed->maxHealth - std::min(healed->health, healed->maxHealth));
                    if (amounts.healsToFull)
                        amount += missing;
                    amount = std::min(amount, missing);
                }
                else if (amounts.healsToFull)
                    amount += float(enemy.maxHealth);   // unknown ally: about one creature like the caster
                // The enemy (or its friend) lives longer: the fight lasts heal / our dps seconds more.
                v.points = Value::FightExtension(world, amount / Value::DealtDps(world));
                break;
            }
            case CastCategory::Control:
                if (offParty)
                    break;
                if (known && victim)
                    v.points = ControlLoss(world, *profile, amounts, *victim);
                else    // random target (no unit target, or not cast yet): party average, not the tank
                    v.points = PartyAverage(world, [&](PartyMemberFacts const& m) { return ControlLoss(world, *profile, amounts, m); });
                break;
            case CastCategory::ManaDrain:
                if (offParty)
                    break;
                if (known && victim)
                    v.points = ManaLoss(world, amounts, *victim);
                else    // mana burns pick a random mana user
                    v.points = PartyAverage(world, [&](PartyMemberFacts const& m) { return ManaLoss(world, amounts, m); });
                break;
            case CastCategory::Damage:
            {
                if (offParty)
                    break;
                float const damage = amounts.TotalDamage();
                // Area spells cast on the caster itself are centred on the caster.
                bool const hasCenter = onCaster || victim;
                Position3 const center = onCaster ? enemy.pos : (victim ? victim->pos : Position3());
                uint32 hit = 0;
                for (PartyMemberFacts const& m : world.party)
                {
                    if (m.isPet || !m.alive)
                        continue;
                    bool const isVictim = !onCaster && victim && m.guid == victim->guid;
                    bool const inRadius = profile->radius > 0.0f && hasCenter &&
                                          PackResolver::Distance(m.pos, center) <= profile->radius;
                    if (!isVictim && !inRadius)
                        continue;
                    ++hit;
                    if (damage >= float(m.health))
                    {
                        v.lethal = true;
                        v.points += Value::DeathPenalty(m);
                    }
                }
                if (onCaster && !hit)
                    break;  // area around the caster with nobody of us in it
                v.points += Value::Points(world, damage * float(std::max(1u, hit)));
                break;
            }
            case CastCategory::Summon:
            {
                float const summon = SummonPoints(world, enemy, spell, amounts);
                v.points = summon >= 0.0f ? summon : SUMMON_POINTS;
                break;
            }
            case CastCategory::Other:
                v.points = OTHER_POINTS;
                break;
        }

        // Control also does damage sometimes (e.g. a stun with a hit): count both.
        if (profile->category == CastCategory::Control && !offParty && amounts.TotalDamage() > 0.0f)
            v.points += Value::Points(world, amounts.TotalDamage());

        float mult = profile->hasOverride ? profile->override.interruptMult : 1.0f;
        if (std::shared_ptr<CreatureProfile const> creature = sProfileStore.GetCreature(enemy.entry))
            if (creature->hasOverride)
                mult *= creature->override.interruptMult;
        v.points *= mult;
        if (mult <= 0.0f)
            v.lethal = false;
        return v;
    }

    // ---- Planning cache -------------------------------------------------------------------
    // Empty-target Evaluate is a pure function of (world model, the enemy's facts listed at
    // EvaluateUncached, spellId, static profiles). InterruptAssigner asks for the same values
    // for every bot x ability (LockoutBenefit, InterruptCost over all enemies, PlannedCast),
    // FocusAssigner and FocusBehavior again: ~1000 calls per refresh with 5 bots and 6
    // casters, for ~20 distinct (enemy, spell) pairs.
    //
    // Why thread_local is safe here:
    //  - A WorldModel belongs to one PartyCommander and is only written by
    //    PartyCommander::Refresh, which ends with ++version (and a new refreshedAtMs). Every
    //    Evaluate caller (assigners, behaviors, encounter modules, GM commands) runs after
    //    that, on whatever thread currently owns the map, while nothing writes the model.
    //  - Each thread has its own table, so no locks and no sharing between map threads. A map
    //    may move to another thread between ticks; that thread's table is keyed by the model's
    //    address + version + refresh time, so it can never return values of an older refresh.
    //  - The key holds one world model at a time; any other key clears the table. A new model
    //    at a freed address would also need the same version and refresh time to collide.
    //  - version 0 = not refreshed yet, or a temporary (PartyCommander::Refresh's `previous`):
    //    never cached.
    //  - Profiles can change without a refresh (`.partybot ai reload overrides`): that bumps
    //    s_profileGeneration (InvalidateCache), which is part of the key.
    // Not covered: an IEncounter::AddFacts that changes the model after ++version would make
    // values cached between the bump and AddFacts stale. Today only ZulFarrakPyramid::AddFacts
    // writes the model, and only allies[].tankLike, which neither Evaluate nor HazardZones
    // reads; nothing evaluates between the bump and AddFacts in PartyCommander::Update. A
    // module that changes party or enemy facts there must bump the version again.
    namespace
    {
        std::atomic<uint32> s_profileGeneration{ 0 };

        struct PlanKey
        {
            uint64 enemy;
            uint32 spellId;
            bool operator==(PlanKey const& o) const { return enemy == o.enemy && spellId == o.spellId; }
        };

        struct PlanKeyHash
        {
            size_t operator()(PlanKey const& k) const
            {
                return std::hash<uint64>()(k.enemy ^ (uint64(k.spellId) << 40) ^ uint64(k.spellId));
            }
        };

        struct PlanCache
        {
            WorldModel const* world = nullptr;
            uint32 version = 0;
            uint32 refreshedAtMs = 0;
            uint32 generation = 0;
            std::unordered_map<PlanKey, CastValue, PlanKeyHash> values;
        };

        thread_local PlanCache t_planCache;

        // Every enemy field EvaluateUncached reads with an empty target. Callers may pass a
        // modified copy of the world's facts (InterruptBehavior::LiveFacts fills in the live
        // cast); such a copy shares the cache entry only while these fields are equal.
        bool SamePlanInputs(EnemyFacts const& a, EnemyFacts const& b)
        {
            return a.entry == b.entry && a.level == b.level && a.meleeHit == b.meleeHit &&
                   a.victim == b.victim && a.maxHealth == b.maxHealth;
        }

        // This thread's table for `world`, emptied if it held another model, refresh or
        // profile generation. nullptr = do not cache this call.
        PlanCache* PlanCacheFor(WorldModel const& world, EnemyFacts const& enemy)
        {
            if (!world.version)
                return nullptr;
            EnemyFacts const* own = world.FindEnemy(enemy.guid);
            if (!own || (own != &enemy && !SamePlanInputs(*own, enemy)))
                return nullptr;     // not an enemy of this model, or a copy with other inputs

            PlanCache& cache = t_planCache;
            uint32 const generation = s_profileGeneration.load(std::memory_order_acquire);
            if (cache.world != &world || cache.version != world.version ||
                cache.refreshedAtMs != world.refreshedAtMs || cache.generation != generation)
            {
                cache.values.clear();
                cache.world = &world;
                cache.version = world.version;
                cache.refreshedAtMs = world.refreshedAtMs;
                cache.generation = generation;
            }
            return &cache;
        }
    }

    CastValue Evaluate(WorldModel const& world, EnemyFacts const& enemy, uint32 spellId, ObjectGuid target)
    {
        // Live casts at a unit target depend on that target: computed every time.
        if (!target.IsEmpty())
            return EvaluateUncached(world, enemy, spellId, target);
        PlanCache* cache = PlanCacheFor(world, enemy);
        if (!cache)
            return EvaluateUncached(world, enemy, spellId, target);

        PlanKey const key{ enemy.guid.GetRawValue(), spellId };
        auto const itr = cache->values.find(key);
        if (itr != cache->values.end())
            return itr->second;
        CastValue const v = EvaluateUncached(world, enemy, spellId, target);
        cache->values.emplace(key, v);
        return v;
    }

    void InvalidateCache()
    {
        s_profileGeneration.fetch_add(1, std::memory_order_acq_rel);
    }

    CastValue PlannedCast(WorldModel const& world, EnemyFacts const& enemy)
    {
        // Not only the last cast: the Scarlet Adept opens with Holy Strike, its heal comes later
        // and needs an interrupter before the first one (scenario 2, 3.5 item 1).
        CastValue best;
        ForEachInterruptibleCast(enemy, [&](uint32 spellId, CreatureSpellRef const*, SpellProfile const&)
        {
            CastValue const v = Evaluate(world, enemy, spellId, ObjectGuid());
            if (v.points > best.points)
                best = v;
        });
        return best;
    }

    CastValue InterruptBenefit(WorldModel const& world, EnemyFacts const& enemy)
    {
        bool const live = enemy.castingSpellId && enemy.castInterruptible;
        CastValue v;
        uint32 remainingMs = enemy.castRemainingMs;
        std::shared_ptr<SpellProfile const> p;
        if (live)
        {
            v = Evaluate(world, enemy, enemy.castingSpellId, enemy.castTarget);
            p = sProfileStore.GetSpell(enemy.castingSpellId);
        }
        else
        {
            v = PlannedCast(world, enemy);
            p = v.spellId ? sProfileStore.GetSpell(v.spellId) : nullptr;
            if (!p)
                return CastValue();
            remainingMs = p->channeled ? p->durationMs : p->castMs;
        }

        // It may die before the cast completes.
        float const remainingSec = remainingMs / 1000.0f;
        float const ttd = Value::EnemyTimeToDie(world, enemy);
        if (remainingSec > 0.0f && ttd < remainingSec)
        {
            v.points *= ttd / remainingSec;
            v.lethal = v.lethal && ttd >= remainingSec * 0.5f;
        }
        // A heal on an ally that dies before the heal lands is worth nothing.
        if (live && p && p->category == CastCategory::Heal && enemy.castTarget != enemy.guid)
        {
            if (EnemyFacts const* healed = world.FindEnemy(enemy.castTarget))
            {
                float const healedTtd = Value::EnemyTimeToDie(world, *healed);
                if (remainingSec > 0.0f && healedTtd < remainingSec)
                    v.points *= healedTtd / remainingSec;
            }
        }
        return v;
    }

    float LockoutBenefit(WorldModel const& world, EnemyFacts const& enemy,
                         InterruptAbility const& ability, uint32 interruptedSpellId)
    {
        if (!ability.lockoutMs || !interruptedSpellId)
            return 0.0f;
        std::shared_ptr<SpellProfile const> interrupted = sProfileStore.GetSpell(interruptedSpellId);
        std::shared_ptr<CreatureProfile const> creature = sProfileStore.GetCreature(enemy.entry);
        if (!interrupted || !creature)
            return 0.0f;

        // SpellCaster::LockOutSpells locks the whole school: Kicking Holy Strike also blocks
        // the heal (scenario 2, 1.3). Only while the enemy lives.
        float const window = std::min(ability.lockoutMs / 1000.0f, Value::EnemyTimeToDie(world, enemy));
        uint8 const school = interrupted->school;
        float const elapsed = CastElapsedSec(enemy, interruptedSpellId);

        // Main ranged spell locked while the enemy stands back: it walks into melee
        // (CreatureAI.cpp DoSpellsListCasts), where melee can hit it and nobody has to run.
        // Counted as its ranged damage meanwhile (walkIn).
        // walkIn and the blocked casts of that same spell (loop below) are two views of one
        // gain: the ranged damage it does not deal during the lockout (rangedDps is that
        // spell's damage per second). Adding both counted it twice, so the main ranged spell
        // counts only the larger of the two; other spells of the school are added as before.
        bool walkInApplies = false;
        float walkIn = 0.0f;
        if (creature->mainRangedSpellId && !enemy.meleeEnabled)
        {
            std::shared_ptr<SpellProfile const> ranged = sProfileStore.GetSpell(creature->mainRangedSpellId);
            if (ranged && ranged->school == school)
            {
                walkInApplies = true;
                walkIn = Value::Points(world, enemy.rangedDps * window);
            }
        }

        float points = 0.0f;
        float mainRangedBlocked = 0.0f;     // blocked casts of the main ranged spell (walkInApplies only)
        for (CreatureSpellRef const& ref : creature->spells)
        {
            if (!IsAvailable(ref, enemy))
                continue;
            std::shared_ptr<SpellProfile const> sp = sProfileStore.GetSpell(ref.spellId);
            if (!sp || sp->school != school)
                continue;
            // One try per spell list check at most; otherwise its average repeat delay.
            float const interval = std::max({ RepeatSec(&ref), CastSec(*sp), WorldModelBuilder::CREATURE_SPELL_CHECK_SEC });
            // The interrupted spell's own repeat delay already runs from the start of its cast.
            // The others' run from their latest cast seen: a spell cast 2 s ago with a 10 s
            // delay cannot come in the first 8 s of the lockout anyway.
            float const wait = ref.spellId == interruptedSpellId ? std::max(0.0f, ref.repeatMinMs / 1000.0f - elapsed)
                                                                 : RepeatWaitSec(world, enemy, ref.spellId, &ref);
            float const blocked = std::max(0.0f, window - wait) / interval;
            if (blocked <= 0.0f)
                continue;
            float const gain = blocked * Evaluate(world, enemy, ref.spellId, ObjectGuid()).points;
            if (walkInApplies && ref.spellId == creature->mainRangedSpellId)
                mainRangedBlocked += gain;
            else
                points += gain;
        }

        if (walkInApplies)
            points += std::max(mainRangedBlocked, walkIn);
        return points;
    }

    float InterruptCost(WorldModel const& world, PartyMemberFacts const& who,
                        InterruptAbility const& ability, EnemyFacts const& enemy, CastValue const& cast)
    {
        float cost = Value::LostAction(world, who, Value::GCD_SEC);

        float const outOfRange = PackResolver::Distance(who.pos, enemy.pos) - ability.maxRange;
        if (outOfRange > 0.0f)
            cost += Value::LostAction(world, who, outOfRange / RUN_YARDS_PER_SEC);

        // The ability is gone for its cooldown. That costs something when this or another enemy
        // casts something worse than this cast during that time. The same enemy counts too: with
        // one caster, kicking Holy Strike may leave the heal free (scenario 2, 3.5 item 4).
        float const cooldownSec = ability.cooldownMs / 1000.0f;
        if (cooldownSec <= 0.0f)
            return cost;

        std::shared_ptr<SpellProfile const> castProfile = cast.spellId ? sProfileStore.GetSpell(cast.spellId) : nullptr;
        float const lockoutSec = ability.lockoutMs / 1000.0f;
        float worst = 0.0f;
        for (EnemyFacts const& other : world.enemies)
        {
            if (other.crowdControlled || !other.acting || other.silenced)
                continue;
            bool const same = other.guid == enemy.guid;
            float const elapsed = same ? CastElapsedSec(enemy, cast.spellId) : 0.0f;
            ForEachInterruptibleCast(other, [&](uint32 spellId, CreatureSpellRef const* ref, SpellProfile const& sp)
            {
                float window = cooldownSec;
                if (same && spellId == cast.spellId)
                    // Its repeat delay runs from the start of the interrupted cast: no recast before repeatMin.
                    window -= std::max(0.0f, (ref ? ref->repeatMinMs / 1000.0f : 0.0f) - elapsed);
                else
                {
                    // Not before its repeat delay from its latest cast seen has passed. A cast
                    // running right now is one we would miss: no wait for it.
                    float const wait = other.castingSpellId == spellId ? 0.0f : RepeatWaitSec(world, other, spellId, ref);
                    bool const locked = same && castProfile && lockoutSec > 0.0f && sp.school == castProfile->school;
                    window -= locked ? std::max(lockoutSec, wait) : wait;   // locked by this interrupt anyway
                }
                if (window <= 0.0f)
                    return;
                float const repeat = RepeatSec(ref);
                float const interval = repeat > 0.0f ? repeat : CAST_INTERVAL_SEC;
                float const share = std::min(1.0f, window / interval);
                float const value = Evaluate(world, other, spellId, ObjectGuid()).points;
                worst = std::max(worst, share * std::max(0.0f, value - cast.points));
            });
        }
        return cost + worst;
    }
}
}
