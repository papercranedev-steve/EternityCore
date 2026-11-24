#include "Commander/Perception/Forecast/FightLoad.h"
#include "Commander/Perception/Forecast/ManaBudget.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Profiles/Tags.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModelBuilder.h"
#include "Commander/Trace/FightLoadJson.h"
#include "Commander/Trace/Trace.h"
#include "Creature.h"
#include "HealSpells.h"
#include "Map.h"
#include "SharedDefines.h"
#include <algorithm>
#include <cfloat>
#include <cstdio>

namespace Commander
{
namespace FightLoad
{
    namespace
    {
        // The cheapest heal that keeps up with `want` per second, else the fastest one
        // (HealSpells::Choose, Normal; an emergency passes FLT_MAX: the fastest).
        HealRate const* PickRate(std::vector<HealRate> const& rates, float want)
        {
            HealRate const* cheapest = nullptr;
            HealRate const* fastest = nullptr;
            for (HealRate const& r : rates)
            {
                if (r.hps <= 0.0f || r.perMana <= 0.0f)
                    continue;
                if (!fastest || r.hps > fastest->hps)
                    fastest = &r;
                if (r.hps >= want && (!cheapest || r.perMana > cheapest->perMana))
                    cheapest = &r;
            }
            return cheapest ? cheapest : fastest;
        }
    }

    Result Simulate(Input const& in, char const* why)
    {
        Result const r = Run(in, nullptr);
        if (Trace::On())
            Trace::Emit("fight", why ? why : "", FightLoadJson::ResultJson(r), FightLoadJson::InputJson(in));
        return r;
    }

    Result Run(Input const& in, std::vector<Step>* steps)
    {
        Result r;
        // Who is hit: 0 the tank, k + 1 Input::holders[k].
        size_t const targets = 1 + in.holders.size();
        std::vector<float> hp(targets), maxHp(targets);
        maxHp[0] = std::max(1.0f, in.tankMaxHealth);
        hp[0] = std::min(maxHp[0], std::max(0.0f, in.tankHealth));
        for (size_t k = 0; k < in.holders.size(); ++k)
        {
            maxHp[k + 1] = std::max(1.0f, in.holders[k].maxHealth);
            hp[k + 1] = std::min(maxHp[k + 1], std::max(0.0f, in.holders[k].health));
            r.lowestHolderPct = std::min(r.lowestHolderPct, hp[k + 1] / maxHp[k + 1] * 100.0f);
        }
        std::vector<bool> gaveUp(in.holders.size(), false);
        r.lowestTankPct = hp[0] / maxHp[0] * 100.0f;
        // The target foe i hits, and for how much.
        auto const on = [&](size_t i) -> size_t
        {
            int const h = in.foes[i].holder;
            return h >= 0 && size_t(h) < in.holders.size() && !gaveUp[h] ? size_t(h) + 1 : 0;
        };
        auto const dpsOf = [&](size_t i) { return on(i) ? in.foes[i].holderDps : in.foes[i].dps; };

        std::vector<float> health;
        float totalHealth = 0.0f;
        for (Foe const& f : in.foes)
        {
            health.push_back(std::max(0.0f, f.health));
            totalHealth += std::max(0.0f, f.health);
        }
        std::vector<float> mana;
        std::vector<float> itemMana;
        for (Healer const& h : in.healers)
        {
            mana.push_back(h.mana);
            itemMana.push_back(h.itemMana);
            r.manaStart += h.mana;
        }
        float const partyDps = std::max(1.0f, in.partyDps);
        float const waste = 1.0f - ManaBudget::OVERHEAL;

        // When each foe comes: death spawns only once their parent died (NEVER until then).
        float const NEVER = MAX_SEC * 2.0f;
        std::vector<float> arrival;
        std::vector<int> parent;
        for (Foe const& f : in.foes)
        {
            int p = -1;
            if (!f.parent.IsEmpty())
                for (size_t j = 0; j < in.foes.size(); ++j)
                    if (in.foes[j].guid == f.parent)
                        p = int(j);
            parent.push_back(p);
            arrival.push_back(f.parent.IsEmpty() ? f.arriveSec : NEVER);
        }
        // Killed this priority first: its damage, and a blast still to come spread over its life.
        auto const harm = [&in](size_t i)
        {
            Foe const& f = in.foes[i];
            return f.dps + (f.lifeSec > 0.0f ? f.burst / f.lifeSec : 0.0f);
        };

        float t = 0.0f;
        for (; t < MAX_SEC; t += STEP_SEC)
        {
            auto present = [&](size_t i) { return health[i] > 0.0f && arrival[i] <= t; };

            std::vector<float> hits(targets, 0.0f);     // per second on each target
            bool anyPresent = false;
            for (size_t i = 0; i < in.foes.size(); ++i)
            {
                if (!present(i))
                    continue;
                Foe const& f = in.foes[i];
                // A self-destruct at the end of its life: the blast, and it is gone.
                if (f.lifeSec > 0.0f && t >= arrival[i] + f.lifeSec)
                {
                    hits[on(i)] += f.burst / STEP_SEC;
                    health[i] = 0.0f;
                    anyPresent = true;
                    continue;
                }
                anyPresent = true;
                hits[on(i)] += dpsOf(i) * DAMAGE_MARGIN;
            }
            float incoming = 0.0f;
            for (float x : hits)
                incoming += x;
            if (!anyPresent)
            {
                // The rest comes after this fight: another fight. Unless it is still this one
                // (a controlled enemy waking up): the time passes, healers top up and regenerate.
                bool waiting = false;
                for (size_t i = 0; i < in.foes.size(); ++i)
                    if (health[i] > 0.0f && in.foes[i].sameFight && arrival[i] < NEVER)
                        waiting = true;
                if (!waiting)
                    break;
            }
            r.peakDps = std::max(r.peakDps, hits[0]);
            for (size_t k = 0; k < targets; ++k)
                hp[k] -= hits[k] * STEP_SEC;
            r.damage += incoming * STEP_SEC;
            float const healedBefore = r.healed;

            // Each one's need; every healer covers its share of all of them.
            std::vector<float> want(targets, 0.0f);
            float totalWant = 0.0f;
            // Which heal: the bots' own rule (HealSpells::Choose). The heal has to keep up with
            // the damage less what the health above SAFE_FLOOR_PCT can absorb over HORIZON_SEC,
            // so a healthy tank gets the cheap heals and sinks slowly while they fall short
            // (the paladin chaining Flash of Light); below EMERGENCY_PCT or dying within
            // EMERGENCY_TTD_SEC, the fastest.
            float required = 0.0f;
            bool emergency = false;
            if (!in.healers.empty())
                for (size_t k = 0; k < targets; ++k)
                {
                    float const deficit = maxHp[k] - hp[k];
                    if (deficit <= 0.0f || (k > 0 && hp[k] <= 0.0f))
                        continue;
                    want[k] = std::min(deficit / STEP_SEC, hits[k] + deficit / TOP_UP_SEC) / float(in.healers.size());
                    totalWant += want[k];
                    float const buffer = hp[k] - maxHp[k] * HealSpells::SAFE_FLOOR_PCT / 100.0f;
                    required += std::max(0.0f, hits[k] - buffer / HealSpells::HORIZON_SEC) / float(in.healers.size());
                    if (hits[k] > 0.0f && (hp[k] < maxHp[k] * HealSpells::EMERGENCY_PCT / 100.0f ||
                                           hp[k] / hits[k] < HealSpells::EMERGENCY_TTD_SEC))
                        emergency = true;
                }
            if (totalWant > 0.0f)
            {
                for (size_t h = 0; h < in.healers.size(); ++h)
                {
                    HealRate const* rate = PickRate(in.healers[h].rates, emergency ? FLT_MAX : required);
                    if (!rate || mana[h] <= 0.0f)
                        continue;
                    float heal = std::min(totalWant, rate->hps) * STEP_SEC;
                    float cost = heal / (rate->perMana * waste);
                    if (cost > mana[h])
                    {
                        heal *= mana[h] / cost;
                        cost = mana[h];
                    }
                    mana[h] -= cost;
                    for (size_t k = 0; k < targets; ++k)
                        hp[k] += heal * want[k] / totalWant;
                    r.healed += heal;
                }
                for (size_t k = 0; k < targets; ++k)
                    hp[k] = std::min(maxHp[k], hp[k]);
            }
            for (size_t h = 0; h < in.healers.size(); ++h)
            {
                Healer const& x = in.healers[h];
                float const regen = x.regenPerSec + (t < x.boostSec ? x.boostRegenPerSec : 0.0f);
                mana[h] = std::min(x.maxMana, mana[h] + regen * STEP_SEC);
                if (itemMana[h] > 0.0f && x.maxMana - mana[h] >= itemMana[h])
                {
                    mana[h] += itemMana[h];
                    itemMana[h] = 0.0f;
                }
            }

            r.lowestTankPct = std::min(r.lowestTankPct, std::max(0.0f, hp[0]) / maxHp[0] * 100.0f);
            // A holder low gives its foes up to the tank from the next step on.
            for (size_t k = 0; k < in.holders.size(); ++k)
            {
                float const pct = std::max(0.0f, hp[k + 1]) / maxHp[k + 1] * 100.0f;
                r.lowestHolderPct = std::min(r.lowestHolderPct, pct);
                if (!gaveUp[k] && pct < HOLDER_GIVE_UP_PCT)
                {
                    gaveUp[k] = true;
                    r.holderGaveUp = true;
                }
            }
            float const tank = hp[0];
            Step* step = nullptr;
            if (steps)
            {
                steps->emplace_back();
                step = &steps->back();
                step->t = t;
                step->incoming = incoming;
                step->healed = r.healed - healedBefore;
                step->tank = tank;
                for (float m : mana)
                    step->mana += m;
            }
            if (tank <= 0.0f)
            {
                r.tankDies = true;
                if (step)
                    step->health = health;
                break;
            }

            // The party's damage: most damage per remaining health first (it keeps the target
            // it started on, whose ratio only grows).
            float budget = partyDps * STEP_SEC;
            while (budget > 0.0f)
            {
                int best = -1;
                for (size_t i = 0; i < in.foes.size(); ++i)
                    if (present(i) && (best < 0 || harm(i) / health[i] > harm(best) / health[best]))
                        best = int(i);
                if (best < 0)
                    break;
                float const take = std::min(budget, health[best]);
                health[best] -= take;
                budget -= take;
            }
            // Death spawns of the foes that died this step come from the next one.
            for (size_t i = 0; i < in.foes.size(); ++i)
            {
                int const p = parent[i];
                if (p >= 0 && arrival[i] >= NEVER && health[p] <= 0.0f && arrival[p] <= t)
                    arrival[i] = t + STEP_SEC + in.foes[i].arriveSec;
            }
            if (step)
                step->health = health;
        }

        r.fightSec = t;
        float left = 0.0f;
        bool presentLeft = false;
        for (size_t i = 0; i < in.foes.size(); ++i)
        {
            if (health[i] > 0.0f && arrival[i] <= t)
            {
                left += health[i];
                presentLeft = true;
            }
        }
        r.unfinished = !r.tankDies && presentLeft;
        r.healthLeftPct = totalHealth > 0.0f ? left / totalHealth * 100.0f : 0.0f;
        for (float m : mana)
            r.manaLeft += m;
        return r;
    }

    bool Bearable(Result const& r)
    {
        return !r.tankDies && !r.unfinished && r.lowestTankPct >= TANK_FLOOR_PCT;
    }

    float Score(Result const& r)
    {
        if (r.tankDies || r.unfinished)
            return -r.healthLeftPct;
        return r.lowestTankPct;
    }

    float Risk(Result const& r)
    {
        if (r.tankDies || r.unfinished)
            return 1.0f;
        return std::min(1.0f, std::max(0.0f, (TANK_FLOOR_PCT - r.lowestTankPct) / TANK_FLOOR_PCT));
    }

    float Loss(WorldModel const& world, Result const& r, float penalty)
    {
        return Value::Points(world, r.damage) + Risk(r) * penalty;
    }

    std::string Describe(Result const& r)
    {
        char buf[200];
        if (r.tankDies)
            snprintf(buf, sizeof(buf), "the tank dies after ~%.0f s with %.0f%% of their health left, incoming up to %.0f/s",
                r.fightSec, r.healthLeftPct, r.peakDps);
        else
            snprintf(buf, sizeof(buf), "fight ~%.0f s, tank down to %.0f%%, incoming up to %.0f/s",
                r.fightSec, r.lowestTankPct, r.peakDps);
        std::string text = buf;
        if (r.manaStart > 0.0f)
        {
            snprintf(buf, sizeof(buf), ", healer mana %.0f -> %.0f", r.manaStart, r.manaLeft);
            text += buf;
        }
        else
            text += ", no healer bot";
        return text;
    }

    Foe FoeOf(Creature const* creature, PartyMemberFacts const& tank)
    {
        Foe f;
        f.guid = creature->GetObjectGuid();
        f.health = float(creature->GetHealth());
        float const hit = (creature->GetFloatValue(UNIT_FIELD_MINDAMAGE) + creature->GetFloatValue(UNIT_FIELD_MAXDAMAGE)) / 2.0f;
        uint32 const attackMs = creature->GetAttackTime(BASE_ATTACK);
        float melee = attackMs ? hit / (attackMs / 1000.0f) : 0.0f;
        bool const physical = creature->GetCreatureInfo()->damage_school == SPELL_SCHOOL_NORMAL;
        melee *= Value::MeleeTaken(tank, creature->GetLevel(), physical);
        float ranged = 0.0f;
        uint32 rangedSpell = 0;
        if (std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(creature->GetEntry()))
            if (profile->mainRangedSpellId)
            {
                rangedSpell = profile->mainRangedSpellId;
                ranged = WorldModelBuilder::RangedSpellDps(*profile, creature->GetLevel(), hit);
            }
        f.dps = std::max(melee, ranged);
        if (Trace::On())
            Trace::Emit("foe", creature->GetName(), Trace::Format(
                "{\"g\":\"%s\",\"level\":%u,\"hp\":%.0f,\"hit\":%.1f,\"attackMs\":%u,\"physical\":%s,\"armorCut\":%.3f,"
                "\"avoid\":%.3f,\"melee\":%.1f,\"rangedSpell\":%u,\"ranged\":%.1f,\"dps\":%.1f}",
                Trace::Guid(f.guid).c_str(), creature->GetLevel(), f.health, hit, attackMs, physical ? "true" : "false",
                physical ? Value::ArmorReduction(tank.armor, creature->GetLevel()) : 0.0f,
                Value::MeleeAvoidance(tank, creature->GetLevel()), melee, rangedSpell, ranged, f.dps));
        return f;
    }

    Foe FoeOf(EnemyFacts const& enemy, PartyMemberFacts const& tank)
    {
        Foe f;
        f.guid = enemy.guid;
        f.health = float(enemy.health);
        float const melee = enemy.meleeDps * Value::MeleeTaken(tank, enemy.level, enemy.meleePhysical);
        f.dps = std::max(melee, enemy.rangedDps);
        return f;
    }

    Foe FoeOf(PackCreature const& creature, PartyMemberFacts const& tank, float arriveSec)
    {
        Foe f;
        f.guid = creature.guid;
        f.health = float(creature.health);
        f.dps = creature.meleeDps * Value::MeleeTaken(tank, creature.level, true);
        f.arriveSec = arriveSec;
        f.sameFight = true;
        return f;
    }

    namespace
    {
        // A creature that is not there yet, from its template (a death spawn, an aggro summon).
        Foe FoeOfEntry(uint32 entry, PartyMemberFacts const& tank)
        {
            Foe f;
            std::shared_ptr<CreatureProfile const> p = sProfileStore.GetCreature(entry);
            if (!p)
                return f;
            uint32 const level = (p->levelMin + std::max(p->levelMin, p->levelMax)) / 2;
            f.health = CreatureProfiles::EstimateHealth(*p, level);
            f.dps = CreatureProfiles::EstimateMeleeDps(*p, level) * Value::MeleeTaken(tank, level, true);
            f.sameFight = true;
            if (p->selfDestruct.spellId && p->selfDestruct.afterMaxMs)
            {
                f.lifeSec = (p->selfDestruct.afterMinMs + p->selfDestruct.afterMaxMs) / 2000.0f;
                f.burst = p->selfDestruct.damage *
                          (p->selfDestruct.physical ? 1.0f - Value::ArmorReduction(tank.armor, level) : 1.0f);
            }
            return f;
        }

        // The foe at `index` (entry `entry`) brings its death spawns when it dies and its aggro
        // summons with it (`aggro`: it has not aggroed yet, a pull).
        void AddSpawns(Input& out, ObjectGuid parentGuid, uint32 entry, bool aggro, PartyMemberFacts const& tank)
        {
            std::shared_ptr<CreatureProfile const> p = sProfileStore.GetCreature(entry);
            if (!p)
                return;
            for (SpawnOn const& s : p->deathSpawns)
                for (uint32 n = 0; n < s.count; ++n)
                {
                    Foe f = FoeOfEntry(s.creatureEntry, tank);
                    if (f.health <= 0.0f)
                        break;
                    f.parent = parentGuid;
                    out.foes.push_back(f);
                }
            if (!aggro)
                return;
            for (SpawnOn const& s : p->aggroSpawns)
                for (uint32 n = 0; n < s.count; ++n)
                {
                    Foe f = FoeOfEntry(s.creatureEntry, tank);
                    if (f.health <= 0.0f)
                        break;
                    out.foes.push_back(f);
                }
        }

        bool StartInput(WorldModel const& world, Input& out, PartyMemberFacts const*& tank)
        {
            if (!tank)
                tank = Tanks::HitTaker(world);   // the main tank; with nobody to hold, the one hit
            if (!tank || !tank->alive)
                return false;
            out = Input();
            out.tankHealth = float(tank->health);
            out.tankMaxHealth = float(std::max(1u, tank->maxHealth));
            out.partyDps = Value::CapacityDps(world);   // a fight to come, not the last seconds
            return true;
        }

        void AddHealers(WorldModel const& world, Input& out)
        {
            for (PartyMemberFacts const& m : world.party)
            {
                if (!m.HealsTeam() || !m.CanAct() || m.healRates.empty())
                    continue;
                Healer h;
                h.guid = m.guid;
                h.rates = m.healRates;
                h.mana = float(m.mana);
                h.maxMana = float(m.maxMana);
                h.regenPerSec = m.manaRegenCasting;
                h.restRegenPerSec = m.manaRegen;
                out.healers.push_back(std::move(h));
            }
        }
    }

    Input AfterWait(Input const& in, float sec)
    {
        Input out = in;
        for (Healer& h : out.healers)
            h.mana = std::min(h.maxMana, h.mana + h.restRegenPerSec * std::max(0.0f, sec));
        return out;
    }

    bool FromWorld(WorldModel const& world, Input& out)
    {
        return FromWorld(world, Lineup(), out);
    }

    bool FromWorld(WorldModel const& world, Lineup const& lineup, Input& out)
    {
        PartyMemberFacts const* tank = nullptr;
        // The stand-in with its armor once set up (a copy: the facts stay as they are).
        PartyMemberFacts standIn;
        auto risen = [&lineup](ObjectGuid g)
        {
            return std::find(lineup.risen.begin(), lineup.risen.end(), g) != lineup.risen.end();
        };
        if (!lineup.tank.IsEmpty())
        {
            PartyMemberFacts const* m = world.FindMember(lineup.tank);
            bool const rises = m && !m->alive && m->selfRes && risen(m->guid);
            if (!m || (!m->CanAct() && !rises))
                return false;
            tank = m;
            if (lineup.tankArmorMult != 1.0f || rises)
            {
                standIn = *m;
                standIn.armor = uint32(float(m->armor) * std::max(0.0f, lineup.tankArmorMult));
                if (rises)
                {
                    standIn.alive = true;
                    standIn.controlled = false;
                    standIn.health = std::max(1u, m->riseHealth);
                }
                tank = &standIn;
            }
        }
        if (!StartInput(world, out, tank))
            return false;
        for (EnemyFacts const& e : world.enemies)
        {
            if (e.onAllyOnly || !e.acting || !e.health)
                continue;
            Foe f = FoeOf(e, *tank);
            if (e.control.spellId)
            {
                f.arriveSec = e.control.remainingMs / 1000.0f;
                f.sameFight = true;
            }
            // A self-destruct on the way: the rest of its life, then the blast.
            BlastFacts const blast = Tags::BlastOf(e);
            if (blast.valid && blast.latestMs)
            {
                uint32 const at = blast.earliestMs + (blast.latestMs - blast.earliestMs) / 2;
                f.lifeSec = std::max(STEP_SEC, int32(at - world.refreshedAtMs) / 1000.0f);
                f.burst = blast.damage * (blast.physical ? 1.0f - Value::ArmorReduction(tank->armor, e.level) : 1.0f);
            }
            out.foes.push_back(f);
            AddSpawns(out, e.guid, e.entry, false, *tank);
        }
        AddHealers(world, out);
        for (ObjectGuid g : lineup.spareHealers)
        {
            PartyMemberFacts const* m = world.FindMember(g);
            if (!m || !m->CanAct() || m->spareHealRates.empty() || !m->UsesMana())
                continue;
            // Healing instead of hitting: its own damage leaves the party's.
            out.partyDps = std::max(1.0f, out.partyDps - m->soloDps);
            Healer h;
            h.guid = m->guid;
            h.rates = m->spareHealRates;
            if (!m->isBot)
                for (HealRate& r : h.rates)
                    r.hps *= PLAYER_HEAL_SHARE;
            h.mana = float(m->mana);
            h.maxMana = float(m->maxMana);
            h.regenPerSec = m->manaRegenCasting;
            h.restRegenPerSec = m->manaRegen;
            out.healers.push_back(std::move(h));
        }
        for (ObjectGuid g : lineup.risen)
        {
            PartyMemberFacts const* m = world.FindMember(g);
            if (!m || m->alive || !m->selfRes || m->spareHealRates.empty() || !m->UsesMana())
                continue;
            Healer h;
            h.guid = m->guid;
            h.rates = m->spareHealRates;
            h.mana = float(m->riseMana);
            h.maxMana = float(m->maxMana);
            h.regenPerSec = m->manaRegenCasting;
            h.restRegenPerSec = m->manaRegen;
            out.healers.push_back(std::move(h));
        }
        return true;
    }

    bool PutOn(WorldModel const& world, Input& in, ObjectGuid enemy, PartyMemberFacts const& holder)
    {
        EnemyFacts const* e = world.FindEnemy(enemy);
        auto foe = std::find_if(in.foes.begin(), in.foes.end(), [enemy](Foe const& f) { return f.guid == enemy; });
        if (!e || foe == in.foes.end())
            return false;
        auto h = std::find_if(in.holders.begin(), in.holders.end(), [&holder](Holder const& x) { return x.guid == holder.guid; });
        if (h == in.holders.end())
        {
            Holder x;
            x.guid = holder.guid;
            x.health = float(holder.health);
            x.maxHealth = float(std::max(1u, holder.maxHealth));
            in.holders.push_back(x);
            h = in.holders.end() - 1;
        }
        foe->holder = int(h - in.holders.begin());
        foe->holderDps = FoeOf(*e, holder).dps;
        return true;
    }

    bool Build(WorldModel const& world, Map* map, std::vector<ObjectGuid> const& creatures, Input& out)
    {
        PartyMemberFacts const* tank = nullptr;
        if (!StartInput(world, out, tank))
            return false;
        if (Trace::On())
            Trace::Emit("party", "fight input", Trace::Format(
                "{\"partyDps\":%.1f,\"dpsSource\":\"%s\",\"tank\":\"%s\",\"tankHealth\":%u,\"tankMax\":%u,\"tankArmor\":%u}",
                out.partyDps, Value::CapacityDpsSource(world), Trace::Guid(tank->guid).c_str(), tank->health, tank->maxHealth,
                uint32(tank->armor)));
        for (ObjectGuid g : creatures)
            if (Creature const* c = map->GetCreature(g))
                if (c->IsAlive())
                {
                    out.foes.push_back(FoeOf(c, *tank));
                    AddSpawns(out, c->GetObjectGuid(), c->GetEntry(), !c->IsInCombat(), *tank);
                }
        AddHealers(world, out);
        return true;
    }
}
}
