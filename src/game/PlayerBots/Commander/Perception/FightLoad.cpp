#include "Commander/Perception/FightLoad.h"
#include "Commander/Perception/ManaBudget.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/WorldModelBuilder.h"
#include "Creature.h"
#include "Map.h"
#include "SharedDefines.h"
#include <algorithm>
#include <cstdio>

namespace Commander
{
namespace FightLoad
{
    namespace
    {
        // The cheapest heal that keeps up with `want` per second, else the fastest one.
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

    Result Simulate(Input const& in)
    {
        Result r;
        float const maxHealth = std::max(1.0f, in.tankMaxHealth);
        float tank = std::min(maxHealth, std::max(0.0f, in.tankHealth));
        r.lowestTankPct = tank / maxHealth * 100.0f;

        std::vector<float> health;
        float totalHealth = 0.0f;
        for (Foe const& f : in.foes)
        {
            health.push_back(std::max(0.0f, f.health));
            totalHealth += std::max(0.0f, f.health);
        }
        std::vector<float> mana;
        for (Healer const& h : in.healers)
        {
            mana.push_back(h.mana);
            r.manaStart += h.mana;
        }
        float const partyDps = std::max(1.0f, in.partyDps);
        float const waste = 1.0f - ManaBudget::OVERHEAL;

        float t = 0.0f;
        for (; t < MAX_SEC; t += STEP_SEC)
        {
            auto present = [&](size_t i) { return health[i] > 0.0f && in.foes[i].arriveSec <= t; };

            float incoming = 0.0f;
            bool anyPresent = false;
            for (size_t i = 0; i < in.foes.size(); ++i)
            {
                if (!present(i))
                    continue;
                anyPresent = true;
                incoming += in.foes[i].dps * DAMAGE_MARGIN;
            }
            if (!anyPresent)
                break;      // the rest comes after this fight: another fight
            r.peakDps = std::max(r.peakDps, incoming);
            tank -= incoming * STEP_SEC;
            r.damage += incoming * STEP_SEC;

            float const deficit = maxHealth - tank;
            if (deficit > 0.0f && !in.healers.empty())
            {
                float const want = std::min(deficit / STEP_SEC, incoming + deficit / TOP_UP_SEC) /
                                   float(in.healers.size());
                for (size_t h = 0; h < in.healers.size(); ++h)
                {
                    HealRate const* rate = PickRate(in.healers[h].rates, want);
                    if (!rate || mana[h] <= 0.0f)
                        continue;
                    float heal = std::min(want, rate->hps) * STEP_SEC;
                    float cost = heal / (rate->perMana * waste);
                    if (cost > mana[h])
                    {
                        heal *= mana[h] / cost;
                        cost = mana[h];
                    }
                    mana[h] -= cost;
                    tank += heal;
                    r.healed += heal;
                }
                tank = std::min(maxHealth, tank);
            }
            for (size_t h = 0; h < in.healers.size(); ++h)
                mana[h] = std::min(in.healers[h].maxMana, mana[h] + in.healers[h].regenPerSec * STEP_SEC);

            r.lowestTankPct = std::min(r.lowestTankPct, std::max(0.0f, tank) / maxHealth * 100.0f);
            if (tank <= 0.0f)
            {
                r.tankDies = true;
                break;
            }

            // The party's damage: most damage per remaining health first (it keeps the target
            // it started on, whose ratio only grows).
            float budget = partyDps * STEP_SEC;
            while (budget > 0.0f)
            {
                int best = -1;
                for (size_t i = 0; i < in.foes.size(); ++i)
                    if (present(i) && (best < 0 || in.foes[i].dps / health[i] > in.foes[best].dps / health[best]))
                        best = int(i);
                if (best < 0)
                    break;
                float const take = std::min(budget, health[best]);
                health[best] -= take;
                budget -= take;
            }
        }

        r.fightSec = t;
        float left = 0.0f;
        bool presentLeft = false;
        for (size_t i = 0; i < in.foes.size(); ++i)
        {
            if (health[i] > 0.0f && in.foes[i].arriveSec <= t)
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
        if (creature->GetCreatureInfo()->damage_school == SPELL_SCHOOL_NORMAL)
            melee *= 1.0f - Value::ArmorReduction(tank.armor, creature->GetLevel());
        float ranged = 0.0f;
        if (std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(creature->GetEntry()))
            if (profile->mainRangedSpellId)
                ranged = WorldModelBuilder::RangedSpellDps(*profile, creature->GetLevel(), hit);
        f.dps = std::max(melee, ranged);
        return f;
    }

    bool Build(WorldModel const& world, Map* map, std::vector<ObjectGuid> const& creatures, Input& out)
    {
        PartyMemberFacts const* tank = Tanks::Main(world);
        if (!tank || !tank->alive)
            return false;
        out = Input();
        out.tankHealth = float(tank->health);
        out.tankMaxHealth = float(std::max(1u, tank->maxHealth));
        out.partyDps = Value::DealtDps(world);
        for (ObjectGuid g : creatures)
            if (Creature const* c = map->GetCreature(g))
                if (c->IsAlive())
                    out.foes.push_back(FoeOf(c, *tank));
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.isBot || m.isPet || m.role != ROLE_HEALER || !m.CanAct() || m.healRates.empty())
                continue;
            Healer h;
            h.rates = m.healRates;
            h.mana = float(m.mana);
            h.maxMana = float(m.maxMana);
            h.regenPerSec = m.manaRegenCasting;
            out.healers.push_back(std::move(h));
        }
        return true;
    }
}
}
