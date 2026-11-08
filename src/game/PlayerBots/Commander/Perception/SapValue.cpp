#include "Commander/Perception/SapValue.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/PullOdds.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/ValueModel.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Commander
{
namespace SapValue
{
    namespace
    {
        // Seconds an aura of `durationSec` lasts when every HEARTBEAT_SEC it stays with chance `keep`.
        float ExpectedHold(float durationSec, float keep)
        {
            if (durationSec <= HEARTBEAT_MIN_SEC)
                return durationSec;
            uint32 const checks = uint32((durationSec - 0.001f) / HEARTBEAT_SEC);
            float sec = 0.0f;
            float alive = 1.0f;
            for (uint32 k = 0; k < checks; ++k)
            {
                sec += HEARTBEAT_SEC * alive;
                alive *= keep;
            }
            return sec + (durationSec - HEARTBEAT_SEC * checks) * alive;
        }

        PackCreature const* FindIdle(WorldModel const& world, ObjectGuid guid)
        {
            for (PackFacts const& p : world.packs)
                for (PackCreature const& c : p.members)
                    if (c.guid == guid)
                        return &c;
            return nullptr;
        }

        // Loss of `fight` with the idle creatures around `at` joining (header: disruption).
        float AddLoss(WorldModel const& world, FightLoad::Input const& fight, FightLoad::Result const& asIs,
                      Position3 const& at, std::vector<ObjectGuid> const& expected)
        {
            PartyMemberFacts const* tank = Tanks::Main(world);
            if (!tank)
                return 0.0f;
            PullOdds::AddRisk const risk = PullOdds::NearbyAdds(world, at, expected);
            if (risk.sources.empty())
                return 0.0f;
            FightLoad::Input with = fight;
            for (PullOdds::RiskSource const& s : risk.sources)
            {
                PackCreature const* c = FindIdle(world, s.guid);
                if (!c || s.weight <= 0.0f)
                    continue;
                // The weight is the distance fade (0..1) x ELITE_RISK for elites; the creature's
                // own numbers already say how strong it is.
                float const chance = std::min(1.0f, s.weight / (s.elite ? PullOdds::ELITE_RISK : 1.0f));
                FightLoad::Foe f = FightLoad::FoeOf(*c, *tank, ControlValue::ADD_ARRIVE_SEC);
                f.dps *= chance;
                f.health *= chance;
                with.foes.push_back(f);
            }
            float const penalty = Value::DeathPenalty(*tank);
            return std::max(0.0f, FightLoad::Loss(world, FightLoad::Simulate(with, "sap: with"), penalty) -
                                  FightLoad::Loss(world, asIs, penalty));
        }
    }

    Worth Of(WorldModel const& world, Map* map, FightLoad::Input const& fight, FightLoad::Result const& asIs,
             PartyMemberFacts const& member, ControlAbility const& a, Input const& in)
    {
        Worth w;
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(a.spellId);
        if (!spell || !in.rogue || !in.target || !map)
            return w;
        Player* rogue = in.rogue;
        Creature* target = in.target;

        int32 const skillDiff = int32(rogue->GetWeaponSkillValue(BASE_ATTACK, target)) -
                                int32(target->GetDefenseSkillValue(rogue));
        w.hit = 1.0f - rogue->GetMeleeMissChance(target, BASE_ATTACK, skillDiff, spell) / 100.0f;
        float const keep = std::min(1.0f, std::max(0.0f, rogue->MagicSpellHitChance(target, spell) / 10000.0f));
        w.holdSec = std::max(0.0f, ExpectedHold(a.durationSec, keep) - in.tankTravelSec);

        ControlAbility held = a;
        held.durationSec = w.holdSec;
        w.control = ControlValue::OfPull(world, fight, asIs, member, held, target->GetObjectGuid(), in.nearTarget);

        // Failure: everything expected on the rogue until the tank is there, no heals.
        float dps = 0.0f;
        for (ObjectGuid guid : in.expected)
            if (Creature* c = map->GetCreature(guid))
                if (c->IsAlive())
                    dps += FightLoad::FoeOf(c, member).dps * FightLoad::DAMAGE_MARGIN;
        PartyMemberFacts const* tank = Tanks::Main(world);
        float const walk = tank ? std::max(0.0f, PackResolver::Distance(tank->pos, in.spot) - MELEE_REACH_YD) : 0.0f;
        float const sec = TAKEOVER_REACT_SEC + walk / RUN_YARDS_PER_SEC;
        float const damage = dps * sec;
        float const health = float(member.health);
        w.failure = Value::Points(world, std::min(damage, health));
        if (damage >= health)
            w.failure += Value::DeathPenalty(member);

        if (in.hasPlannedSpot)
            w.disruption = std::max(0.0f, AddLoss(world, fight, asIs, in.spot, in.expected) -
                                          AddLoss(world, fight, asIs, in.plannedSpot, in.expected));
        return w;
    }

    float Optimistic(WorldModel const& world, FightLoad::Input const& fight, FightLoad::Result const& asIs,
                     PartyMemberFacts const& member, ControlAbility const& a, ObjectGuid target, uint32 nearTarget)
    {
        return ControlValue::OfPull(world, fight, asIs, member, a, target, nearTarget).Net();
    }

    std::string Describe(Worth const& w)
    {
        char buf[300];
        snprintf(buf, sizeof(buf), "net %+.1f (hit %.0f%%, %.0f s held: benefit %.1f cost %.1f | failure %.1f disruption %.1f)",
            w.Net(), w.hit * 100.0f, w.holdSec, w.control.Benefit(), w.control.Cost(), w.failure, w.disruption);
        return buf;
    }
}
}
