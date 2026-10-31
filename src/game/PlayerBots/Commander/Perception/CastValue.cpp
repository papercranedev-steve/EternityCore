#include "Commander/Perception/CastValue.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/PackResolver.h"
#include "SpellMgr.h"
#include <algorithm>

namespace Commander
{
namespace CastValues
{
    CastValue Evaluate(WorldModel const& world, EnemyFacts const& enemy, uint32 spellId, ObjectGuid target)
    {
        CastValue v;
        v.spellId = spellId;
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
        std::shared_ptr<SpellProfile const> profile = sProfileStore.GetSpell(spellId);
        if (!spell || !profile)
            return v;

        // Who it lands on: the cast's unit target if that is one of us, else the enemy's victim.
        PartyMemberFacts const* victim = target.IsEmpty() ? nullptr : world.FindMember(target);
        if (!victim)
            victim = world.FindMember(enemy.victim);

        SpellAmounts const amounts = SpellProfiles::Estimate(spell, enemy.level, enemy.meleeHit);
        switch (profile->category)
        {
            case CastCategory::Heal:
            {
                // The enemy (or its friend) lives longer: the fight lasts heal / our dps seconds more.
                float const seconds = amounts.TotalHeal() / Value::DealtDps(world);
                v.points = Value::FightExtension(world, seconds);
                break;
            }
            case CastCategory::Control:
                if (victim)
                    v.points = Value::LostAction(world, *victim, amounts.controlSec);
                break;
            case CastCategory::Damage:
            {
                float const damage = amounts.TotalDamage();
                uint32 hit = 0;
                for (PartyMemberFacts const& m : world.party)
                {
                    if (m.isPet || !m.alive)
                        continue;
                    bool const isVictim = victim && m.guid == victim->guid;
                    bool const inRadius = profile->radius > 0.0f && victim &&
                                          PackResolver::Distance(m.pos, victim->pos) <= profile->radius;
                    if (!isVictim && !inRadius)
                        continue;
                    ++hit;
                    if (damage >= float(m.health))
                    {
                        v.lethal = true;
                        v.points += Value::DeathPenalty(m);
                    }
                }
                v.points += Value::Points(world, damage * float(std::max(1u, hit)));
                break;
            }
            case CastCategory::Summon:
                v.points = SUMMON_POINTS;
                break;
            case CastCategory::Other:
                v.points = OTHER_POINTS;
                break;
        }

        // Control also does damage sometimes (e.g. a stun with a hit): count both.
        if (profile->category == CastCategory::Control && victim && amounts.TotalDamage() > 0.0f)
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

    CastValue InterruptBenefit(WorldModel const& world, EnemyFacts const& enemy)
    {
        bool const live = enemy.castingSpellId && enemy.castInterruptible;
        uint32 const spellId = live ? enemy.castingSpellId : enemy.lastCastSpellId;
        if (!spellId)
            return CastValue();

        // It may die before the cast completes.
        uint32 remainingMs = enemy.castRemainingMs;
        if (!live)
        {
            std::shared_ptr<SpellProfile const> p = sProfileStore.GetSpell(spellId);
            if (!p || !p->interruptible)
                return CastValue();
            remainingMs = p->channeled ? p->durationMs : p->castMs;
        }
        CastValue v = Evaluate(world, enemy, spellId, live ? enemy.castTarget : ObjectGuid());
        float const remainingSec = remainingMs / 1000.0f;
        float const ttd = Value::EnemyTimeToDie(world, enemy);
        if (remainingSec > 0.0f && ttd < remainingSec)
        {
            v.points *= ttd / remainingSec;
            v.lethal = v.lethal && ttd >= remainingSec * 0.5f;
        }
        return v;
    }

    float InterruptCost(WorldModel const& world, PartyMemberFacts const& who,
                        InterruptAbility const& ability, EnemyFacts const& enemy, float thisBenefit)
    {
        float cost = Value::LostAction(world, who, Value::GCD_SEC);

        float const outOfRange = PackResolver::Distance(who.pos, enemy.pos) - ability.maxRange;
        if (outOfRange > 0.0f)
            cost += Value::LostAction(world, who, outOfRange / RUN_YARDS_PER_SEC);

        // The ability is gone for its cooldown. That only costs something when another
        // enemy casts something worse than this cast during that time.
        float const cooldownShare = std::min(1.0f, (ability.cooldownMs / 1000.0f) / CAST_INTERVAL_SEC);
        if (cooldownShare > 0.0f)
        {
            float worstOther = 0.0f;
            for (EnemyFacts const& other : world.enemies)
            {
                if (other.guid == enemy.guid || other.crowdControlled || !other.lastCastSpellId)
                    continue;
                std::shared_ptr<SpellProfile const> p = sProfileStore.GetSpell(other.lastCastSpellId);
                if (!p || !p->interruptible)
                    continue;
                worstOther = std::max(worstOther, Evaluate(world, other, other.lastCastSpellId, ObjectGuid()).points);
            }
            cost += cooldownShare * std::max(0.0f, worstOther - thisBenefit);
        }
        return cost;
    }
}
}
