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

        // What every rating of this caster on this enemy shares.
        struct Context
        {
            EnemyFacts const* enemy = nullptr;
            PartyMemberFacts const* self = nullptr;
            uint32 sinceRefresh = 0;
            float perDamage = 0.0f;     // points per point of damage (header)
        };

        bool Prepare(Player* caster, WorldModel const& world, Unit const* target, SpellEntry const* filler,
                     uint32 nowMs, Context& c)
        {
            if (!caster || !target || !filler)
                return false;
            c.enemy = world.FindEnemy(target->GetObjectGuid());
            c.self = world.FindMember(caster->GetObjectGuid());
            if (!c.enemy || !c.self)
                return false;
            c.sinceRefresh = WorldTimer::getMSTimeDiff(world.refreshedAtMs, nowMs);
            float const fillerDps = DamageSpells::Estimate(caster, target, filler) /
                                    std::max(0.1f, DamageSpells::BusySec(caster, filler));
            if (fillerDps <= 0.0f)
                return false;
            float const share = 1.0f / float(std::max(1u, world.rates.damageDealers));
            c.perDamage = Value::Points(world, world.rates.incomingDps) * share / fillerDps;
            return c.perDamage > 0.0f;
        }

        // ManaWorth::Points; a caster that taps health into mana (warlock): at most the global
        // cooldowns of the taps that pay it back (header).
        float ManaPrice(WorldModel const& world, PartyMemberFacts const& self, float mana)
        {
            float const worth = ManaWorth::Points(world, self, mana);
            if (self.tapManaPerCast <= 0.0f || self.tapManaPerHealth <= 0.0f)
                return worth;
            return std::min(worth, Value::LostAction(world, self, Value::GCD_SEC * mana / self.tapManaPerCast));
        }

        void Finish(Context const& c, Rating& r, float manaWorth)
        {
            r.landed = std::min(r.landed, float(c.enemy->health));
            r.points = r.landed * c.perDamage - manaWorth;
            r.perSec = r.points / std::max(0.1f, r.busySec);
            r.known = true;
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
                SpellEntry const* filler, uint32 nowMs, SpellEntry const* consumes)
    {
        Rating r;
        Context c;
        if (!spell || !Prepare(caster, world, target, filler, nowMs, c))
            return r;

        uint32 const castMs = uint32(std::max<int32>(0, int32(spell->GetCastTime(caster))));
        uint32 const landMs = c.sinceRefresh + castMs + FinishForecast::FlightMs(c.self->pos, *c.enemy, spell->speed);
        r.busySec = DamageSpells::BusySec(caster, spell);

        DamageSpells::Periodic const dot = DamageSpells::EstimatePeriodic(caster, target, spell);
        uint32 const horizon = landMs + dot.ticks * dot.periodMs;
        uint32 const death = DeathMs(world, *c.enemy, c.self->guid, c.sinceRefresh + uint32(r.busySec * 1000.0f), horizon);

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
            r.landed -= OwnDotDamage(*c.enemy, c.self->guid, consumes->Id, landMs, death ? death : 0xFFFFFFFFu);

        Finish(c, r, ManaPrice(world, *c.self, DamageSpells::ManaCost(caster, spell)));
        return r;
    }

    Rating RateWand(Player* caster, WorldModel const& world, Unit const* target, SpellEntry const* shoot,
                    SpellEntry const* filler, uint32 nowMs)
    {
        Rating r;
        Context c;
        if (!shoot || !Prepare(caster, world, target, filler, nowMs, c))
            return r;

        uint32 const landMs = c.sinceRefresh + FinishForecast::FlightMs(c.self->pos, *c.enemy, shoot->speed);
        r.busySec = std::max(Value::GCD_SEC, float(caster->GetAttackTime(RANGED_ATTACK)) / 1000.0f);
        uint32 const death = DeathMs(world, *c.enemy, c.self->guid, landMs, landMs);
        if (!death || death > landMs)
            r.landed = (caster->GetFloatValue(UNIT_FIELD_MINRANGEDDAMAGE) + caster->GetFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE)) / 2.0f;

        Finish(c, r, 0.0f);
        return r;
    }
}
}
