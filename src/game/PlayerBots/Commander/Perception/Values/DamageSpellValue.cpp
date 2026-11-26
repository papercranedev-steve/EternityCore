#include "Commander/Perception/Values/DamageSpellValue.h"
#include "Commander/Perception/Forecast/FinishForecast.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "DamageSpells.h"
#include "Player.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
namespace DamageSpellValue
{
    namespace
    {
        // Ticks of `self`'s `spellId` on `enemy` from `fromMs` until `untilMs` (ms after the refresh).
        float OwnDotDamage(EnemyFacts const& enemy, ObjectGuid self, uint32 spellId, uint32 fromMs, uint32 untilMs)
        {
            for (PartyDot const& d : enemy.partyDots)
            {
                if (d.caster != self || d.spellId != spellId || !d.periodMs)
                    continue;
                uint32 const until = std::min(untilMs, d.remainingMs);
                uint32 ticks = 0;
                for (uint32 t = d.nextTickMs; t <= until; t += d.periodMs)
                    if (t >= fromMs)
                        ++ticks;
                return d.perTick * float(ticks);
            }
            return 0.0f;
        }
    }

    uint32 DeathMs(WorldModel const& world, EnemyFacts const& enemy, ObjectGuid self, uint32 selfFromMs,
                   uint32 horizonMs)
    {
        uint32 death = FinishForecast::DeathMs(world, enemy, self, selfFromMs, horizonMs);
        float const dyingSec = Value::EnemyDyingSec(world, enemy);
        if (dyingSec * 1000.0f <= float(horizonMs))
        {
            uint32 const shared = std::max<uint32>(1, uint32(dyingSec * 1000.0f));
            death = death ? std::min(death, shared) : shared;
        }
        return death;
    }

    Rating Rate(Player* caster, WorldModel const& world, Unit const* target, SpellEntry const* spell,
                uint32 nowMs, SpellEntry const* consumes)
    {
        Rating r;
        if (!caster || !target || !spell)
            return r;
        EnemyFacts const* enemy = world.FindEnemy(target->GetObjectGuid());
        PartyMemberFacts const* self = world.FindMember(caster->GetObjectGuid());
        if (!enemy || !self)
            return r;

        uint32 const sinceRefresh = WorldTimer::getMSTimeDiff(world.refreshedAtMs, nowMs);
        uint32 const castMs = uint32(std::max<int32>(0, int32(spell->GetCastTime(caster))));
        uint32 const landMs = sinceRefresh + castMs + FinishForecast::FlightMs(self->pos, *enemy, spell->speed);
        r.busySec = DamageSpells::BusySec(caster, spell);

        DamageSpells::Periodic const dot = DamageSpells::EstimatePeriodic(caster, target, spell);
        uint32 const horizon = landMs + dot.ticks * dot.periodMs;
        uint32 const death = DeathMs(world, *enemy, self->guid, sinceRefresh + uint32(r.busySec * 1000.0f), horizon);

        if (!death || death > landMs)
        {
            r.landed = DamageSpells::EstimateDirect(caster, target, spell);
            if (dot.ticks && dot.periodMs)
            {
                uint32 const ticks = death ? std::min(dot.ticks, (death - landMs) / dot.periodMs) : dot.ticks;
                r.landed += dot.perTick * float(ticks);
            }
        }
        if (consumes)
            r.landed -= OwnDotDamage(*enemy, self->guid, consumes->Id, landMs, death ? death : 0xFFFFFFFFu);
        r.landed = std::min(r.landed, float(enemy->health));

        r.points = Value::Points(world, std::max(0.0f, r.landed)) -
                   ManaWorth::Points(world, *self, DamageSpells::ManaCost(caster, spell));
        if (r.landed < 0.0f)
            r.points -= Value::Points(world, -r.landed);
        r.perSec = r.points / std::max(0.1f, r.busySec);
        return r;
    }
}
}
