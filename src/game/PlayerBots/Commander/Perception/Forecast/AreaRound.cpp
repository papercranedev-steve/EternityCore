#include "Commander/Perception/Forecast/AreaRound.h"
#include "Commander/Perception/Forecast/Runners.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Profiles/CreatureProfile.h"
#include "Commander/Perception/Values/AggroTurnCost.h"
#include "Commander/Perception/Values/AreaValue.h"
#include "SharedDefines.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace AreaRound
{
    namespace
    {
        bool SlowImmune(EnemyFacts const& e)
        {
            std::shared_ptr<CreatureProfile const> p = sProfileStore.GetCreature(e.entry);
            // Bit convention of Creature::IsImmuneToSpell(Effect): 1 << (mechanic - 1).
            return p && (p->mechanicImmuneMask & (1u << (MECHANIC_SNARE - 1))) != 0;
        }

        // Header "Arrival".
        float ArriveSec(EnemyFacts const& e, AreaSpellFacts const& spell, Position3 const& from, Position3 const& centre)
        {
            float const speed = e.slowed ? std::max(e.speed, AggroTurnCost::RUN_YARDS_PER_SEC) : std::max(0.1f, e.speed);
            float const reach = e.boundingRadius + AreaValue::MELEE_REACH;
            if (e.rooted)
                return spell.periodSec + AreaValue::WalkSecUnder(e.pos, speed, spell.slowPct, centre, spell.radius, from, reach);
            EnemyFacts free = e;
            free.speed = speed;
            free.slowed = false;
            return Runners::ReachSec(free, from, reach, &spell, centre);
        }
    }

    Result Of(WorldModel const& world, Request const& r)
    {
        Result out;
        out.why = "no caster or spell";
        if (!r.caster || !r.spell)
            return out;
        PartyMemberFacts const& m = *r.caster;
        AreaSpellFacts const& spell = *r.spell;
        out.why = "the spell is not ready";
        if (!spell.ready || !spell.channel || spell.periodSec <= 0.0f)
            return out;
        out.why = "not the mana for the spell and a rescue";
        if (float(m.mana) < spell.mana + r.rescueMana)
            return out;

        float const channelSec = float(spell.ticks) * spell.periodSec;
        out.needSec = r.rescueInSec >= 0.0f ? std::min(channelSec, r.rescueInSec) : channelSec;
        out.firstArriveSec = channelSec * 10.0f;
        for (EnemyFacts const& e : world.enemies)
        {
            if (!e.acting || e.crowdControlled || !e.health || e.victim != m.guid)
                continue;
            ++out.chasers;
            if (!e.meleeEnabled || e.rangedDps > 0.0f)
            {
                out.why = "a chaser hits from where it stands";
                return out;
            }
            if (!e.rooted && !e.slowed)
            {
                out.why = "a chaser is not under control";
                return out;
            }
            if (SlowImmune(e))
            {
                out.why = "a chaser is immune to slows";
                return out;
            }
            out.firstArriveSec = std::min(out.firstArriveSec, ArriveSec(e, spell, r.from, r.centre));
        }
        out.why = "no chaser";
        if (!out.chasers)
            return out;
        out.why = "a chaser would be on the caster before it is safe";
        if (out.firstArriveSec < out.needSec + REACT_SEC)
            return out;
        out.why = nullptr;
        out.ok = true;
        return out;
    }
}
}
