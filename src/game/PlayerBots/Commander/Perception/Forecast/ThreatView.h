/*
 * PartyBot Commander layer 1: threat queries.
 *
 * Aggro rule (ThreatContainer::selectNextVictim, ThreatManager.cpp ~286):
 * a unit takes the enemy from its current victim when its threat is above
 *   110% of the victim's threat if the enemy can reach it in melee, otherwise 130%.
 *
 * Heal threat (Spell.cpp ~1365, HostileRefManager::threatAssist):
 *   effective heal * 0.5 (paladin 0.25) * spell threat multiplier,
 *   divided evenly between all creatures that have the healed unit on their threat list,
 *   then the healer's threat modifiers apply (ThreatCalcHelper::CalcThreat).
 *   Creatures that are feared / confused / in a breakable stun get nothing.
 *
 * Everything except PredictHealThreat works on WorldModel data only.
 */

#ifndef MANGOS_COMMANDER_THREAT_VIEW_H
#define MANGOS_COMMANDER_THREAT_VIEW_H

#include "Commander/Perception/WorldModel.h"
#include <limits>

class Unit;
class SpellEntry;

namespace Commander
{
namespace ThreatView
{
    float const NO_LIMIT = std::numeric_limits<float>::infinity();

    float const MELEE_AGGRO_FACTOR = 1.1f;
    float const RANGED_AGGRO_FACTOR = 1.3f;

    ThreatEntry const* FindEntry(EnemyFacts const& enemy, ObjectGuid who);

    // Threat `who` must exceed to take the enemy from its current victim.
    float AggroThreshold(EnemyFacts const& enemy, ThreatEntry const& who);

    // How much more threat `who` can generate on this enemy before taking aggro.
    // NO_LIMIT if `who` already is the victim. Can be negative for a moment
    // (threshold passed, enemy switches on its next target update).
    float Headroom(EnemyFacts const& enemy, ObjectGuid who);

    // For the current victim (usually the tank): the smallest headroom of any other
    // party-side unit, i.e. how close the next person is to pulling aggro.
    // NO_LIMIT if nobody else is on the list.
    float VictimLead(EnemyFacts const& enemy, ObjectGuid* closestOut = nullptr);

    bool WouldPullAggro(EnemyFacts const& enemy, ObjectGuid who, float addedThreat);

    struct EnemyThreatDelta
    {
        ObjectGuid enemy;
        float added = 0.0f;
    };

    // Smallest headroom left on any enemy after `who` gains the given threat.
    // Negative = at least one enemy would switch to `who`. NO_LIMIT if no delta applies.
    float WorstHeadroomAfter(WorldModel const& world, ObjectGuid who,
                             std::vector<EnemyThreatDelta> const& deltas, ObjectGuid* worstEnemyOut = nullptr);

    // Map thread only (reads live units): threat each creature would give `healer`
    // for healing `healTarget` by `effectiveHeal` (already capped at missing health).
    void PredictHealThreat(Unit* healer, Unit* healTarget, float effectiveHeal,
                           SpellEntry const* spell, std::vector<EnemyThreatDelta>& out);

    // Map thread only: threat one cast of `spell` by `caster` on `target` (null: itself) puts
    // on each creature (2026-11-12, the threat opening's rule, Coordination/Doctrine/ThreatOpening.h):
    //   helpful  its flat spell_threat + healing x 0.5 (paladins 0.25, PredictHealThreat), split
    //            over the creatures that have the target on their list (HostileRefManager::
    //            threatAssist); none on it: no threat (Ice Barrier before anything fights the mage);
    //   harmful  on the target and, for an area spell, every creature within its radius of it:
    //            damage x the spell's multiplier + its flat threat, at least 1 (a control, a
    //            Counterspell: it joins the fight on that creature).
    // `hitsIdle`: a harmful cast reaches a creature not in combat yet (it would start a fight).
    void PredictCastThreat(Unit* caster, Unit* target, SpellEntry const* spell,
                           std::vector<EnemyThreatDelta>& out, bool* hitsIdle = nullptr);
}
}

#endif
