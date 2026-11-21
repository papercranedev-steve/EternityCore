#include "Commander/Coordination/Doctrine/HealerDamage.h"
#include "Commander/Coordination/Doctrine/FightPlace.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Forecast/FightLoad.h"
#include "Commander/Perception/WorldModel.h"
#include "Timer.h"

namespace Commander
{
    namespace
    {
        using HD = HealerDamageAssigner;

        // Swings a weapon as a healer (a mail wearer with a shield or a two-hander).
        bool MeleeClass(PartyMemberFacts const& m)
        {
            return m.classId == CLASS_PALADIN || m.classId == CLASS_SHAMAN;
        }

        uint32 ActingEnemies(WorldModel const& world)
        {
            uint32 count = 0;
            for (EnemyFacts const& e : world.enemies)
                if (e.acting)
                    ++count;
            return count;
        }

        // Dungeon (header): facts of now only.
        bool LowPressure(WorldModel const& world, uint32 acting)
        {
            bool allNearlyDead = true;
            for (EnemyFacts const& e : world.enemies)
            {
                if (!e.acting)
                    continue;
                if (e.boss)
                    return false;
                if (e.healthPct > HD::NEARLY_DEAD_PCT)
                    allNearlyDead = false;
            }
            if (acting > HD::LOW_PRESSURE_ENEMIES && !allNearlyDead)
                return false;
            for (PartyMemberFacts const& m : world.party)
                if (!m.isPet && m.alive && m.healthPct < HD::LOW_PRESSURE_HEALTH_PCT)
                    return false;
            return true;
        }
    }

    char const* HealerDamageAssigner::Name(uint8 tier)
    {
        switch (tier)
        {
            case HEALER_DAMAGE_NONE:  return "none";
            case HEALER_DAMAGE_FREE:  return "free";
            case HEALER_DAMAGE_SPARE: return "spare";
            case HEALER_DAMAGE_FULL:  return "full";
            default:                  return "?";
        }
    }

    bool HealerDamageAssigner::Attacks(PartyMemberFacts const& m, TeamPlan const& plan)
    {
        HealerDamageDuty const* d = plan.healerDamage.Find(m.guid);
        if (!d)
            return false;
        return d->tier >= HEALER_DAMAGE_SPARE || (d->tier == HEALER_DAMAGE_FREE && (m.canShootWand || d->melee));
    }

    void HealerDamageAssigner::Clear(TeamPlan& plan) const
    {
        plan.healerDamage = HealerDamagePlan();
    }

    void HealerDamageAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        HealerDamagePlan const prev = plan.healerDamage;
        HealerDamagePlan& d = plan.healerDamage;
        d = HealerDamagePlan();

        uint32 const now = WorldTimer::getMSTime();
        bool const fight = world.HasPartyEnemy();
        uint32 const acting = fight ? ActingEnemies(world) : 0;
        // A change in the fight (header): raising a tier waits for one.
        bool const changed = acting != prev.enemyCount;
        d.enemyCount = acting;

        // Open world forecast, made once for every healer that needs it.
        d.forecastMs = prev.forecastMs;
        d.hard = prev.hard;
        bool forecastDone = false;
        auto hard = [&]() -> bool
        {
            if (forecastDone)
                return d.hard;
            forecastDone = true;
            if (!changed && prev.forecastMs && WorldTimer::getMSTimeDiff(prev.forecastMs, now) < FORECAST_MS)
                return d.hard;
            FightLoad::Input in;
            d.hard = FightLoad::FromWorld(world, in) && FightPlace::IsHard(FightLoad::Simulate(in, "healer damage"));
            d.forecastMs = now;
            return d.hard;
        };

        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.isBot || m.isPet || m.role != ROLE_HEALER)
                continue;
            HealerDamageDuty duty;
            duty.bot = m.guid;
            HealerDamageDuty const* before = prev.Find(m.guid);
            MemberCover const* cover = plan.coverage.Find(m.guid);

            if (m.healerDamageSet >= 0 && m.healerDamageSet < HEALER_DAMAGE_TIERS)
            {
                duty.tier = uint8(m.healerDamageSet);
                duty.reason = "set by the player";
            }
            else if (cover && (cover->added & FUNCTION_DAMAGE))
            {
                duty.tier = HEALER_DAMAGE_FULL;
                // (swings in the open world are a free attack too: melee below)
                duty.noReserve = !m.canShootWand && !(world.mapKind == MapKind::World && MeleeClass(m));
                duty.reason = duty.noReserve ? "nobody else deals damage, no wand: no reserve" : "nobody else deals damage";
            }
            else if (world.mapKind == MapKind::Raid)
            {
                duty.tier = HEALER_DAMAGE_NONE;
                duty.reason = "raid";
            }
            else if (world.mapKind == MapKind::Dungeon)
            {
                bool const wasUp = before && before->tier == HEALER_DAMAGE_SPARE;
                if (fight && LowPressure(world, acting) && (wasUp || changed))
                {
                    duty.tier = HEALER_DAMAGE_SPARE;
                    duty.reason = "dungeon, little pressure";
                }
                else
                {
                    duty.tier = HEALER_DAMAGE_NONE;
                    duty.reason = "dungeon";
                }
            }
            else
            {
                bool const wasDown = before && before->tier == HEALER_DAMAGE_SPARE;
                if (fight && hard())
                {
                    duty.tier = HEALER_DAMAGE_SPARE;
                    duty.reason = "open world, hard fight";
                }
                else if (fight && wasDown && !changed)
                {
                    duty.tier = HEALER_DAMAGE_SPARE;
                    duty.reason = "open world, was a hard fight";
                }
                else
                {
                    duty.tier = HEALER_DAMAGE_FULL;
                    duty.reason = "open world";
                }
            }
            // Melee (header): a paladin or shaman with a free attack tier in the open world.
            duty.melee = duty.tier >= HEALER_DAMAGE_FREE && world.mapKind == MapKind::World && MeleeClass(m);
            d.healers.push_back(duty);
        }
    }
}
