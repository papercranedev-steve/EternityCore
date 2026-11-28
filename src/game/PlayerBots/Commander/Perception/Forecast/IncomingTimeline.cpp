#include "Commander/Perception/Forecast/IncomingTimeline.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Profiles/SpellProfile.h"
#include "ScriptMgr.h"          // ScriptCommands.h: TARGET_T_x, CF_x (creature_spells)
#include "SharedDefines.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace IncomingTimeline
{
    namespace
    {
        uint32 const HORIZON_MS = uint32(HORIZON_SEC * 1000.0f);

        struct Hit
        {
            float expected = 0.0f;
            float worst = 0.0f;
            float spike = 0.0f;
        };

        struct Busy
        {
            float fromSec = 0.0f;
            float toSec = 0.0f;
        };

        bool InMeleeReach(EnemyFacts const& e, Position3 const& pos)
        {
            return PackResolver::Distance(e.pos, pos) - e.boundingRadius <= Value::ROOTED_REACH_YD;
        }

        // One swing of `e` on `m` (SpellCaster::RollMeleeOutcomeAgainst: miss, dodge, parry,
        // crit, crushing; block left out), after armor and the member's damage taken factor.
        Hit SwingOn(EnemyFacts const& e, PartyMemberFacts const& m, float minDamage, float maxDamage)
        {
            Hit h;
            if (maxDamage <= 0.0f || (e.meleePhysical ? m.immunePhysical : m.immuneMagic))
                return h;
            float const mult = (e.meleePhysical ? 1.0f - Value::ArmorReduction(m.armor, e.level) : 1.0f) *
                               m.damageTakenMult;
            float const avoid = Value::MeleeAvoidance(m, e.level);
            float const skill = 5.0f * float(e.level);
            // Unit::GetUnitCriticalChance: 5% + 0.04% per weapon skill above the defense.
            float const crit = std::max(0.0f, 5.0f + (skill - float(m.defenseSkill)) * 0.04f) / 100.0f;
            float crush = 0.0f;
            if (e.canCrush)
            {
                // 2% per skill point lacking, minus 15%, from 15 points (defense above the cap counts as the cap).
                float const lack = skill - std::min(float(m.defenseSkill), 5.0f * float(m.level));
                if (lack >= 15.0f)
                    crush = (lack * 2.0f - 15.0f) / 100.0f;
            }
            float const left = std::max(0.0f, 1.0f - avoid);
            float const pCrit = std::min(crit, left);
            float const pCrush = e.alwaysCrush ? left - pCrit : std::min(crush, left - pCrit);
            float const pNormal = std::max(0.0f, left - pCrit - pCrush);

            float const average = (minDamage + maxDamage) / 2.0f * mult;
            h.expected = average * (pNormal + pCrit * MELEE_CRIT_MULT + pCrush * CRUSH_MULT);
            float const top = maxDamage * mult;
            h.worst = e.alwaysCrush ? top * CRUSH_MULT : top;
            float const spikeMult = crit > 0.0f ? MELEE_CRIT_MULT : (crush > 0.0f ? CRUSH_MULT : 1.0f);
            h.spike = std::max(0.0f, top * spikeMult - h.worst);
            return h;
        }

        // An enrage to come (EnemyFacts::enrages) on one weapon's swings: from boostSec on,
        // `hit` and the period x periodMult. boostSec NO_LIMIT = none.
        struct Boost
        {
            float boostSec = NO_LIMIT;
            float periodMult = 1.0f;
            Hit hit;
        };

        // Swings from `startSec` on, each period, pushed past the enemy's casts. The swing timer
        // running when the haste lands is kept (ApplyAttackTimePercentMod changes the period).
        void AddSwings(Timeline& t, EnemyFacts const& e, Hit const& h, uint32 timerMs, uint32 periodMs,
                       float startSec, std::vector<Busy> const& busy, Boost const& boost)
        {
            if (!periodMs || h.worst <= 0.0f)
                return;
            float const period = periodMs / 1000.0f;
            float at = std::max(timerMs / 1000.0f, startSec);
            while (at < HORIZON_SEC)
            {
                for (Busy const& b : busy)
                    if (at >= b.fromSec && at < b.toSec)
                        at = b.toSec;
                if (at >= HORIZON_SEC)
                    break;
                bool const boosted = at >= boost.boostSec;
                Hit const& hit = boosted ? boost.hit : h;
                Event ev;
                ev.atSec = at;
                ev.expected = hit.expected;
                ev.worst = hit.worst;
                ev.worstSpike = hit.spike;
                ev.source = e.guid;
                t.events.push_back(ev);
                at += boosted ? period * boost.periodMult : period;
            }
            t.steadyDps += boost.boostSec < HORIZON_SEC ? boost.hit.expected / (period * boost.periodMult)
                                                         : h.expected / period;
        }

        // The first enrage to come on `e` and when: at once below its threshold, else when the
        // party's damage brings it there (Value::EnemyTimeToDie, linear in health).
        EnrageFacts const* NextEnrage(WorldModel const& world, EnemyFacts const& e, float& atSec)
        {
            EnrageFacts const* next = nullptr;
            atSec = NO_LIMIT;
            for (EnrageFacts const& f : e.enrages)
            {
                float at = 0.0f;
                if (e.healthPct > f.atHealthPct)
                {
                    float const ttd = Value::EnemyTimeToDie(world, e);
                    at = ttd < NO_LIMIT ? ttd * (e.healthPct - f.atHealthPct) / e.healthPct : NO_LIMIT;
                }
                if (at < atSec)
                {
                    atSec = at;
                    next = &f;
                }
            }
            return next;
        }

        Boost BoostOf(EnemyFacts const& e, PartyMemberFacts const& m, EnrageFacts const* enrage, float atSec,
                      float minDamage, float maxDamage)
        {
            Boost b;
            if (!enrage || maxDamage <= 0.0f)
                return b;
            b.boostSec = atSec;
            b.periodMult = enrage->periodMult;
            b.hit = SwingOn(e, m, minDamage * enrage->damageMult + enrage->damageFlat,
                            maxDamage * enrage->damageMult + enrage->damageFlat);
            return b;
        }

        // Spell damage of `e` on `m` per cast (direct, periodic), after armor (physical) and
        // the member's damage taken factor.
        struct SpellHit
        {
            float direct = 0.0f;
            float periodic = 0.0f;
        };

        SpellHit SpellOn(EnemyFacts const& e, PartyMemberFacts const& m, SpellEntry const* spell, SpellProfile const& p)
        {
            SpellHit s;
            bool const physical = p.school == SPELL_SCHOOL_NORMAL;
            if (physical ? m.immunePhysical : m.immuneMagic)
                return s;
            SpellAmounts const a = SpellProfiles::Estimate(spell, e.level, e.meleeHit);
            float const mult = (physical ? 1.0f - Value::ArmorReduction(m.armor, e.level) : 1.0f) * m.damageTakenMult;
            s.direct = a.damage * mult;
            s.periodic = a.periodicDamage * mult;
            return s;
        }

        // Direct part at `landSec`, periodic part in ticks over the duration (3 s apart, or
        // the channel's time spread evenly).
        void AddSpellHits(Timeline& t, EnemyFacts const& e, uint32 spellId, SpellHit const& s, float landSec,
                          float durationSec, float share)
        {
            if (landSec < HORIZON_SEC && s.direct > 0.0f)
            {
                Event ev;
                ev.atSec = landSec;
                ev.expected = s.direct * share;
                ev.worst = s.direct;
                ev.worstSpike = s.direct * (SPELL_CRIT_MULT - 1.0f);
                ev.spellId = spellId;
                ev.source = e.guid;
                t.events.push_back(ev);
            }
            if (s.periodic > 0.0f && durationSec > 0.0f)
            {
                int const ticks = std::max(1, int(std::lround(durationSec / 3.0f)));
                float const period = durationSec / float(ticks);
                for (int i = 1; i <= ticks; ++i)
                {
                    float const at = landSec + period * float(i);
                    if (at >= HORIZON_SEC)
                        break;
                    Event ev;
                    ev.atSec = at;
                    ev.expected = s.periodic / float(ticks) * share;
                    ev.worst = s.periodic / float(ticks);
                    ev.spellId = spellId;
                    ev.source = e.guid;
                    t.events.push_back(ev);
                }
            }
        }

        float FlightSec(SpellEntry const* spell, EnemyFacts const& e, Position3 const& target)
        {
            if (spell->speed <= 0.0f)
                return 0.0f;
            return std::max(5.0f, PackResolver::Distance(e.pos, target)) / spell->speed;
        }

        std::vector<ThreatEntry const*> Listed(EnemyFacts const& e)
        {
            std::vector<ThreatEntry const*> rows;
            for (ThreatEntry const& r : e.threat)
                if (r.onList)
                    rows.push_back(&r);
            return rows;
        }

        // Share of a creature_spells row's casts aimed at `m` (CreatureAI GetTargetByType):
        // 1 = surely, 1 / n = one of n random candidates, 0 = someone else. `aimed`: the unit
        // it is aimed at when that is sure (its splash may reach `m`).
        float TargetShare(WorldModel const& world, EnemyFacts const& e, uint8 castTarget, PartyMemberFacts const& m,
                          ObjectGuid& aimed)
        {
            std::vector<ThreatEntry const*> const rows = Listed(e);
            bool const listed = std::any_of(rows.begin(), rows.end(), [&m](ThreatEntry const* r) { return r->guid == m.guid; });
            switch (castTarget)
            {
                case TARGET_T_HOSTILE:
                    aimed = e.victim;
                    break;
                case TARGET_T_HOSTILE_SECOND_AGGRO:
                    aimed = rows.size() > 1 ? rows[1]->guid : e.victim;
                    break;
                case TARGET_T_HOSTILE_LAST_AGGRO:
                    aimed = rows.empty() ? e.victim : rows.back()->guid;
                    break;
                case TARGET_T_HOSTILE_RANDOM:
                    return listed && !rows.empty() ? 1.0f / float(rows.size()) : 0.0f;
                case TARGET_T_HOSTILE_RANDOM_NOT_TOP:
                    if (!listed || m.guid == e.victim)
                        return 0.0f;
                    return rows.size() > 1 ? 1.0f / float(rows.size() - 1) : 0.0f;
                case TARGET_T_HOSTILE_NEAREST:
                case TARGET_T_HOSTILE_FARTHEST:
                {
                    bool const nearest = castTarget == TARGET_T_HOSTILE_NEAREST;
                    float best = nearest ? NO_LIMIT : -1.0f;
                    for (ThreatEntry const* r : rows)
                        if (PartyMemberFacts const* o = world.FindMember(r->guid))
                        {
                            float const d = PackResolver::Distance(e.pos, o->pos);
                            if (nearest ? d < best : d > best)
                            {
                                best = d;
                                aimed = o->guid;
                            }
                        }
                    break;
                }
                default:
                    return 0.0f;    // the caster itself, friends, objects: not a hostile target
            }
            return aimed == m.guid ? 1.0f : 0.0f;
        }

        bool SelfCentred(uint8 castTarget)
        {
            return castTarget == TARGET_T_PROVIDED_TARGET || castTarget == TARGET_T_OWNER_OR_SELF;
        }

        // Targets of a chain spell on enemies (Cleave, Chain Lightning: an effect on
        // TARGET_UNIT_ENEMY with EffectChainTarget > 1), 1 = no chain.
        uint32 ChainCount(SpellEntry const* spell)
        {
            uint32 count = 1;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                if (spell->Effect[i] && spell->EffectImplicitTargetA[i] == TARGET_UNIT_ENEMY)
                    count = std::max(count, spell->EffectChainTarget[i]);
            return count;
        }

        // `m` is hit by a cast aimed at `aimed` (a party member) besides it: an area spell's
        // radius around it, or a chain spell's jumps as Spell.cpp picks them (TARGET_UNIT_ENEMY):
        // candidates within the search range of the first target (the caster with
        // SPELL_ATTR_EX2_CHAIN_FROM_CASTER): the radius for melee spells, radius + targets x
        // CHAIN_SPELL_JUMP_RADIUS for the others; each jump to the nearest one left within
        // CHAIN_SPELL_JUMP_RADIUS of the previous target. Party members only (allies not counted).
        bool ReachedFrom(WorldModel const& world, EnemyFacts const& e, SpellEntry const* spell, float radius,
                         ObjectGuid aimed, PartyMemberFacts const& m)
        {
            PartyMemberFacts const* first = aimed.IsEmpty() ? nullptr : world.FindMember(aimed);
            if (!first || first->guid == m.guid)
                return false;
            uint32 const chain = ChainCount(spell);
            if (chain <= 1)
                return radius > 0.0f && PackResolver::Distance(first->pos, m.pos) <= radius;

            float const range = spell->DmgClass == SPELL_DAMAGE_CLASS_MELEE
                ? radius : radius + float(chain) * CHAIN_SPELL_JUMP_RADIUS;
            Position3 const& center = spell->HasAttribute(SPELL_ATTR_EX2_CHAIN_FROM_CASTER) ? e.pos : first->pos;
            std::vector<PartyMemberFacts const*> pool;
            for (PartyMemberFacts const& o : world.party)
                if (o.alive && o.guid != first->guid && PackResolver::Distance(center, o.pos) <= range)
                    pool.push_back(&o);
            Position3 prev = first->pos;
            for (uint32 jump = 1; jump < chain && !pool.empty(); ++jump)
            {
                auto next = std::min_element(pool.begin(), pool.end(), [&prev](PartyMemberFacts const* a, PartyMemberFacts const* b)
                    { return PackResolver::Distance(prev, a->pos) < PackResolver::Distance(prev, b->pos); });
                if (PackResolver::Distance(prev, (*next)->pos) > CHAIN_SPELL_JUMP_RADIUS)
                    break;
                if ((*next)->guid == m.guid)
                    return true;
                prev = (*next)->pos;
                pool.erase(next);
            }
            return false;
        }

        // Share of a cast's damage that reaches `m`: an area spell around the caster or around
        // a sure target reaches everyone in its radius; a chain spell its jumps (ReachedFrom).
        // A cone of `e` (SpellProfile::coneArc) pointed at `aim` (a party member; else where `e`
        // faces now) reaches `m` (boss doc G4).
        bool InConeOf(WorldModel const& world, EnemyFacts const& e, SpellProfile const& p, ObjectGuid aim,
                      PartyMemberFacts const& m)
        {
            float facing = e.facing;
            if (PartyMemberFacts const* at = aim.IsEmpty() ? nullptr : world.FindMember(aim))
                if (PackResolver::Distance(at->pos, e.pos) > 0.1f)
                    facing = std::atan2(at->pos.y - e.pos.y, at->pos.x - e.pos.x);
            return SpellTags::InCone(e.pos.x, e.pos.y, e.pos.z, facing, p.coneArc, p.coneRadius,
                                     m.pos.x, m.pos.y, m.pos.z);
        }

        float HitShare(WorldModel const& world, EnemyFacts const& e, SpellEntry const* spell, uint8 castTarget,
                       float radius, PartyMemberFacts const& m)
        {
            // A cone: where it points (its victim, or where it faces), not a circle. At a random
            // target: the chance that it is the one (it turns to it).
            std::shared_ptr<SpellProfile const> p = sProfileStore.GetSpell(spell->Id);
            if (p && p->coneArc != 0.0f)
            {
                if (SelfCentred(castTarget) || castTarget == 0xFF)
                    return InConeOf(world, e, *p, ObjectGuid(), m) ? 1.0f : 0.0f;
                if (castTarget == TARGET_T_HOSTILE)
                    return InConeOf(world, e, *p, e.victim, m) ? 1.0f : 0.0f;
                ObjectGuid aimed;
                return TargetShare(world, e, castTarget, m, aimed);
            }
            if (SelfCentred(castTarget))
                return radius > 0.0f && PackResolver::Distance(e.pos, m.pos) <= radius + e.boundingRadius ? 1.0f : 0.0f;
            ObjectGuid aimed;
            float const share = TargetShare(world, e, castTarget, m, aimed);
            if (share < 1.0f && ReachedFrom(world, e, spell, radius, aimed, m))
                return 1.0f;
            return share;
        }

        bool HasMana(EnemyFacts const& e, SpellEntry const* spell)
        {
            if (spell->powerType != POWER_MANA || e.maxMana == 0)
                return true;
            return e.mana >= spell->manaCost + spell->ManaCostPercentage * e.createMana / 100;
        }

        // A control spell sure to reach the member, landing at `landSec` (header: Lockout).
        void AddLockout(Timeline& t, EnemyFacts const& e, PartyMemberFacts const& m, SpellEntry const* spell,
                        SpellProfile const& p, float landSec)
        {
            if (!p.controls || landSec >= HORIZON_SEC)
                return;
            // Immune to every mechanic it has (Fear Ward on a fear): no lockout. A control
            // without a known mechanic stays.
            uint32 const mechanics = p.controlMechanics & ~m.immuneMechanics;
            if (p.controlMechanics && !mechanics)
                return;
            float sec = SpellProfiles::Estimate(spell, e.level, e.meleeHit).controlSec;
            if (sec <= 0.0f)
                sec = p.durationMs / 1000.0f;
            if (sec <= 0.0f)
                return;
            Lockout l;
            l.startSec = landSec;
            l.endSec = landSec + sec;
            l.spellId = spell->Id;
            l.source = e.guid;
            l.mechanics = mechanics;
            auto const at = std::upper_bound(t.lockouts.begin(), t.lockouts.end(), l,
                                             [](Lockout const& a, Lockout const& b) { return a.startSec < b.startSec; });
            t.lockouts.insert(at, l);
        }

        // The spell `e` is casting now, if it reaches `m`.
        void AddCurrentCast(Timeline& t, WorldModel const& world, EnemyFacts const& e, PartyMemberFacts const& m)
        {
            if (!e.castingSpellId)
                return;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(e.castingSpellId);
            std::shared_ptr<SpellProfile const> p = sProfileStore.GetSpell(e.castingSpellId);
            if (!spell || !p)
                return;
            bool reaches = e.castTarget == m.guid;
            if (p->coneArc != 0.0f)
                reaches = InConeOf(world, e, *p, e.castTarget == e.guid ? ObjectGuid() : e.castTarget, m);
            else if (!reaches && (e.castTarget.IsEmpty() || e.castTarget == e.guid))
                reaches = p->radius > 0.0f && PackResolver::Distance(e.pos, m.pos) <= p->radius + e.boundingRadius;
            else if (!reaches)
                reaches = ReachedFrom(world, e, spell, p->radius, e.castTarget, m);
            if (!reaches)
                return;
            SpellHit s = SpellOn(e, m, spell, *p);
            float const leftSec = e.castRemainingMs / 1000.0f;
            if (e.castChanneled)
            {
                // A channel deals its damage over its time left.
                float const share = e.castTotalMs ? std::min(1.0f, float(e.castRemainingMs) / float(e.castTotalMs)) : 1.0f;
                s.periodic = (s.periodic + s.direct) * share;
                s.direct = 0.0f;
                AddSpellHits(t, e, e.castingSpellId, s, 0.0f, leftSec, 1.0f);
                return;
            }
            float const duration = p->durationMs / 1000.0f;
            float const landSec = leftSec + FlightSec(spell, e, m.pos);
            AddSpellHits(t, e, e.castingSpellId, s, landSec, duration, 1.0f);
            AddLockout(t, e, m, spell, *p, landSec);
        }

        // creature_spells rows (CreatureAI::DoSpellsListCasts) over the horizon. Every row
        // keeps the caster busy, also those that miss `m`; `busy` gets its casts.
        void AddSpellList(Timeline& t, WorldModel const& world, EnemyFacts const& e, PartyMemberFacts const& m,
                          float busySec, std::vector<Busy>& busy)
        {
            if (e.spellList.empty() || e.silenced)
                return;
            struct Row
            {
                ListSpellFacts const* f;
                SpellEntry const* spell;
                std::shared_ptr<SpellProfile const> profile;
                float share;
                SpellHit hit;
                uint32 readyMs;
            };
            PartyMemberFacts const* victim = world.FindMember(e.victim);
            bool const victimInMelee = victim && InMeleeReach(e, victim->pos);
            std::vector<Row> rows;
            for (ListSpellFacts const& f : e.spellList)
            {
                SpellEntry const* spell = sSpellMgr.GetSpellEntry(f.spellId);
                std::shared_ptr<SpellProfile const> p = sProfileStore.GetSpell(f.spellId);
                if (!spell || !p || !f.probability || !HasMana(e, spell))
                    continue;
                if (((f.castFlags & CF_ONLY_IN_MELEE) && !victimInMelee) ||
                    ((f.castFlags & CF_NOT_IN_MELEE) && victimInMelee))
                    continue;
                Row r{ &f, spell, p, 0.0f, SpellHit(), 0 };
                r.share = HitShare(world, e, spell, f.castTarget, p->radius, m);
                if ((f.castFlags & CF_TARGET_CASTING) && !m.casting)
                    r.share = 0.0f;
                if (r.share > 0.0f)
                    r.hit = SpellOn(e, m, spell, *p);
                // The cooldown counts down by one check at each check.
                uint32 const checks = std::max(1u, (f.cooldownMs + SPELL_CHECK_MS - 1) / SPELL_CHECK_MS);
                r.readyMs = e.spellCheckInMs + (checks - 1) * SPELL_CHECK_MS;
                rows.push_back(r);
            }

            uint32 busyUntilMs = uint32(busySec * 1000.0f);
            for (uint32 check = e.spellCheckInMs; check < HORIZON_MS; check += SPELL_CHECK_MS)
            {
                bool castThisCheck = false;
                for (Row& r : rows)
                {
                    if (r.readyMs > check)
                        continue;
                    uint16 const flags = r.f->castFlags;
                    bool const triggered = (flags & CF_TRIGGERED) != 0;
                    if (!(flags & (CF_TRIGGERED | CF_INTERRUPT_PREVIOUS)) && (castThisCheck || check < busyUntilMs))
                        continue;       // waits, ready, for the next check
                    uint32 const castMs = triggered ? 0 : r.profile->castMs;
                    uint32 const channelMs = r.profile->channeled ? r.profile->durationMs : 0;
                    if (!triggered)
                    {
                        castThisCheck = true;
                        busyUntilMs = check + castMs + channelMs;
                        if (castMs + channelMs)
                            busy.push_back({ check / 1000.0f, busyUntilMs / 1000.0f });
                    }
                    if (r.share > 0.0f)
                    {
                        float const chance = r.f->probability / 100.0f;
                        float const landSec = (check + castMs) / 1000.0f + FlightSec(r.spell, e, m.pos);
                        size_t const first = t.events.size();
                        AddSpellHits(t, e, r.f->spellId, r.hit, landSec,
                                     (channelMs ? channelMs : r.profile->durationMs) / 1000.0f, r.share);
                        for (size_t i = first; i < t.events.size(); ++i)
                            t.events[i].expected *= chance;
                        // Sure to reach it and sure to be cast: a lockout.
                        if (r.share >= 1.0f && r.f->probability >= 100)
                            AddLockout(t, e, m, r.spell, *r.profile, landSec);
                    }
                    uint32 const repeat = (r.f->repeatMinMs + r.f->repeatMaxMs) / 2;
                    r.readyMs = r.f->once ? HORIZON_MS :
                        check + std::max(1u, (repeat + SPELL_CHECK_MS - 1) / SPELL_CHECK_MS) * SPELL_CHECK_MS;
                }
            }

            for (Row const& r : rows)
            {
                if (r.share <= 0.0f || r.f->once)
                    continue;
                float const repeatSec = (r.f->repeatMinMs + r.f->repeatMaxMs) / 2000.0f;
                float const cycle = std::max({ repeatSec, r.profile->castMs / 1000.0f + SPELL_CHECK_MS / 2000.0f,
                                               SPELL_CHECK_MS / 1000.0f });
                t.steadyDps += (r.hit.direct + r.hit.periodic) * r.share * r.f->probability / 100.0f / cycle;
            }
        }
    }

    void Timeline::AddHeal(float atSec, float amount, uint32 spellId, ObjectGuid source)
    {
        if (amount <= 0.0f || atSec >= HORIZON_SEC)
            return;
        Event ev;
        ev.atSec = std::max(0.0f, atSec);
        ev.expected = -amount;
        ev.worst = -amount;
        ev.spellId = spellId;
        ev.source = source;
        auto const at = std::upper_bound(events.begin(), events.end(), ev,
                                         [](Event const& a, Event const& b) { return a.atSec < b.atSec; });
        events.insert(at, ev);
    }

    void Timeline::AddHots(PartyMemberFacts const& member)
    {
        for (PartyHot const& h : member.hots)
        {
            if (!h.periodMs || h.perTick <= 0.0f)
                continue;
            for (uint32 at = h.nextTickMs; at <= h.remainingMs && at < HORIZON_MS; at += h.periodMs)
                AddHeal(at / 1000.0f, h.perTick, h.spellId, h.caster);
        }
    }

    Lockout const* Timeline::NextLockout() const
    {
        for (Lockout const& l : lockouts)
            if (l.startSec > 0.0f)
                return &l;
        return nullptr;
    }

    float Timeline::TimeToLose(float amount, bool worst, Relief const& relief) const
    {
        if (amount <= 0.0f)
            return 0.0f;
        float lost = 0.0f;
        float spike = 0.0f;
        float t = 0.0f;
        auto factorAt = [&relief](float sec) { return sec < relief.factorSec ? relief.factor : 1.0f; };
        // Steady drain (residual x factor, minus the healers' rate once it starts) up to
        // `until`: the second it reaches `amount`, or < 0.
        auto drain = [&](float until) -> float
        {
            while (t < until)
            {
                float end = until;
                if (t < relief.healDelaySec)
                    end = std::min(end, relief.healDelaySec);
                if (t < relief.factorSec)
                    end = std::min(end, relief.factorSec);
                float const rate = residualDps * factorAt(t) - (t >= relief.healDelaySec ? relief.healPerSec : 0.0f);
                float const next = lost + rate * (end - t);
                if (rate > 0.0f && next + spike >= amount)
                    return t + std::max(0.0f, amount - spike - lost) / rate;
                lost = std::max(next, -headroom);
                t = end;
            }
            return -1.0f;
        };
        for (Event const& ev : events)
        {
            float const dies = drain(ev.atSec);
            if (dies >= 0.0f)
                return dies;
            float const damage = worst ? ev.worst : ev.expected;
            float const factor = damage > 0.0f ? factorAt(ev.atSec) : 1.0f;
            lost = std::max(lost + damage * factor, -headroom);
            if (worst)
                spike = std::max(spike, ev.worstSpike * factor);
            if (lost + spike >= amount)
                return ev.atSec;
        }
        float const dies = drain(HORIZON_SEC);
        if (dies >= 0.0f)
            return dies;
        float const rate = (steadyDps + residualDps) * factorAt(HORIZON_SEC) - relief.healPerSec;
        if (rate <= 0.0f)
            return NO_LIMIT;
        return HORIZON_SEC + std::max(0.0f, amount - spike - lost) / rate;
    }

    Timeline Of(WorldModel const& world, PartyMemberFacts const& member)
    {
        Timeline t;
        t.headroom = std::max(0.0f, float(member.maxHealth) - float(member.health));
        if (!member.alive || (member.immunePhysical && member.immuneMagic))
            return t;
        for (EnemyFacts const& e : world.enemies)
        {
            if (!e.acting || e.crowdControlled)
                continue;
            float const castLeftSec = e.castingSpellId ? e.castRemainingMs / 1000.0f : 0.0f;
            std::vector<Busy> busy;
            if (castLeftSec > 0.0f)
                busy.push_back({ 0.0f, castLeftSec });
            AddCurrentCast(t, world, e, member);
            AddSpellList(t, world, e, member, castLeftSec, busy);

            if (!e.meleeEnabled || e.victim != member.guid)
                continue;
            // Out of reach: rooted, none; else from its arrival.
            float startSec = 0.0f;
            float const gap = PackResolver::Distance(e.pos, member.pos) - e.boundingRadius - Value::ROOTED_REACH_YD;
            if (gap > 0.0f)
            {
                if (e.rooted || e.speed <= 0.0f)
                    continue;
                startSec = gap / e.speed;
            }
            float enrageSec;
            EnrageFacts const* enrage = NextEnrage(world, e, enrageSec);
            AddSwings(t, e, SwingOn(e, member, e.meleeMin, e.meleeMax), e.swingInMs, e.swingPeriodMs, startSec, busy,
                      BoostOf(e, member, enrage, enrageSec, e.meleeMin, e.meleeMax));
            AddSwings(t, e, SwingOn(e, member, e.offMin, e.offMax), e.offSwingInMs, e.offSwingPeriodMs, startSec, busy,
                      BoostOf(e, member, enrage, enrageSec, e.offMin, e.offMax));
        }
        std::stable_sort(t.events.begin(), t.events.end(),
                         [](Event const& a, Event const& b) { return a.atSec < b.atSec; });
        // Measured damage beyond the model: area damage of scripts, boss script abilities.
        t.residualDps = std::max(0.0f, member.takenDps - t.steadyDps);
        return t;
    }
}
}
