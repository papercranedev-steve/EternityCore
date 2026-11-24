#include "Commander/Perception/Values/FormExitCost.h"
#include "Commander/Perception/Forecast/ThreatRunway.h"
#include "Commander/Perception/Forecast/ThreatView.h"
#include "Commander/Perception/Profiles/StandIn.h"
#include "Commander/Perception/Values/AggroTurnCost.h"
#include "Commander/Perception/Values/SurvivalValue.h"
#include "Commander/Perception/Values/ValueModel.h"
#include <algorithm>

namespace Commander
{
namespace FormExitCost
{
    namespace
    {
        bool OnCaster(EnemyFacts const& e, PartyMemberFacts const& caster)
        {
            return !e.onAllyOnly && e.acting && !e.crowdControlled && e.health && e.victim == caster.guid;
        }

        // Physical damage taken without the form, over with it.
        float ArmorLossFactor(WorldModel const& world, PartyMemberFacts const& caster)
        {
            if (!caster.tankStance)
                return 1.0f;
            uint32 level = caster.level;
            for (EnemyFacts const& e : world.enemies)
                if (OnCaster(e, caster))
                    level = std::max(level, e.level);
            float const mult = caster.level >= StandIn::DIRE_BEAR_LEVEL ? StandIn::DIRE_BEAR_ARMOR_MULT
                                                                         : StandIn::BEAR_ARMOR_MULT;
            uint32 const without = uint32(float(caster.armor) / mult);
            float const kept = 1.0f - Value::ArmorReduction(caster.armor, level);
            float const keptWithout = 1.0f - Value::ArmorReduction(without, level);
            return kept > 0.0f ? keptWithout / kept : 1.0f;
        }
    }

    Result Of(WorldModel const& world, PartyMemberFacts const& caster, float outSec)
    {
        Result r;
        float const rebuildSec = caster.tankStance ? RAGE_REBUILD_SEC : 0.0f;
        float const idleSec = outSec + rebuildSec;

        SurvivalValue::Danger const danger = SurvivalValue::Of(world, caster);
        if (danger.InDanger())
            return r;
        float const extraDps = danger.physicalDps * (ArmorLossFactor(world, caster) - 1.0f);
        float const drain = danger.dps * idleSec + extraDps * outSec;
        float const floor = float(caster.maxHealth) * MIN_HEALTH_PCT / 100.0f;
        if (float(caster.health) + caster.absorb - drain < floor)
            return r;

        r.allowed = true;
        r.cost = Value::LostAction(world, caster, FORM_BACK_SEC + rebuildSec) +
                 Value::Points(world, std::max(0.0f, extraDps) * outSec);

        // The enemies that turn away while it adds no threat, grouped by who takes them.
        struct Taker
        {
            PartyMemberFacts const* who = nullptr;
            std::vector<AggroTurnCost::Turn> turns;
        };
        std::vector<Taker> takers;
        for (EnemyFacts const& e : world.enemies)
        {
            if (!OnCaster(e, caster))
                continue;
            ThreatRunway::Result const runway = ThreatRunway::Idle(world, e, caster.guid);
            if (runway.sec >= idleSec || runway.taker.IsEmpty())
                continue;
            PartyMemberFacts const* who = world.FindMember(runway.taker);
            if (!who || !who->alive)
                continue;
            AggroTurnCost::Turn t;
            t.enemy = &e;
            t.holder = &caster;
            t.speed = e.rooted ? 0.0f : e.speed;
            // Once it turned the taker has the caster's threat x its aggro factor; the caster
            // needs 10% over that in melee to take it back.
            ThreatEntry const* own = ThreatView::FindEntry(e, caster.guid);
            ThreatEntry const* rival = ThreatView::FindEntry(e, who->guid);
            float const factor = rival && rival->inMeleeReach ? ThreatView::MELEE_AGGRO_FACTOR
                                                              : ThreatView::RANGED_AGGRO_FACTOR;
            float const ownThreat = own ? own->threat : 0.0f;
            t.gap = std::max(0.0f, ownThreat * (factor * ThreatView::MELEE_AGGRO_FACTOR - 1.0f));
            auto it = std::find_if(takers.begin(), takers.end(), [who](Taker const& x) { return x.who == who; });
            if (it == takers.end())
            {
                takers.push_back(Taker());
                it = takers.end() - 1;
                it->who = who;
            }
            it->turns.push_back(t);
        }
        for (Taker const& t : takers)
            r.cost += AggroTurnCost::OfGroup(world, t.turns, *t.who);
        return r;
    }
}
}
