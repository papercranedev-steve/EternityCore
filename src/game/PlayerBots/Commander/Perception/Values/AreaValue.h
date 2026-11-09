/*
 * PartyBot Commander layer 1 (eyes): what one area damage spell of one bot is worth, in points
 * (AoE phase, .kiro/docs/2026-11-08-aoe-phase.md). The one valuation of area damage: the
 * brain's area phase (Coordination/Doctrine/AreaPhase.h) starts it with it, AreaDamageBehavior's channel
 * watch goes on or stops with it, ending the channel costs its value (Behaviors/CurrentAction.h),
 * and a crowd control that keeps an area spell off a pack gives it up (ControlValue::AreaLoss).
 * Before 2026-11-08 the control side had its own guess (full area damage for the whole control,
 * no threat, no mana): 97 points where the area module saw 3 (strat-20261108-161025).
 *
 * Request: caster, spell (PartyMemberFacts::areaSpells), centre (an enemy for a ground spell,
 * the caster's place otherwise), where the caster stands (`from`, a spot it may walk to), the
 * ticks to judge, whether it already runs (`paid`), a shield put up first, tanks the plan
 * sends elsewhere.
 *
 * Targets: acting enemies whose edge is within the radius of the centre.
 *   - one `forbidden` says must stay unbroken (TeamPlan::KeepsUnbroken), or a new one
 *     (seen < ThreatRoom::NEW_ENEMY_SETTLE_SEC, not judged when paid): not valid at all;
 *   - crowd controlled ones (fear, a stun damage does not break): not counted;
 *   - one hitting the caster or nobody: free (no turn to fear).
 * Tick plan (as AreaDamageBehavior before, now on facts): damage after n ticks = sum of
 * min(perTick x n, health). An enemy turns at the first tick k it survives where my threat (+
 * my bolt in the air at it) + k x tickThreat passes (its holder's threat + TANK_GAIN_SHARE x
 * the holder's threat rate on it x k x period) x 110% (caster within its melee reach from
 * `from`) / 130%. A holder hitting another enemy gains nothing here. Turn loss after n ticks
 * = AggroTurnCost::OfGroup over every enemy turned by then (with the shield, `from`, the busy
 * tanks; the caster dying under all of them is in there). A holder that is not a party member:
 * all its hits (Value::HitLoss).
 * Value of n ticks: benefit = LostAction(damage(n) / filler damage per second) [+ paid: the
 * filler's mana for that time, ManaWorth], cost = LostAction(n x period) + turn loss(n).
 *   not paid  the whole spell is judged (user 2026-11-08: a Blizzard is paid in full at the
 *             start, so starting one planned to stop is wasted mana): net = value(all ticks) -
 *             ManaWorth(mana above what the filler spends in that time); not valid when it does
 *             less damage per mana than the filler.
 *   paid      the best n of the ticks left (the channel watch stops it only when every n loses).
 * Pure: WorldModel facts.
 */

#ifndef MANGOS_COMMANDER_AREA_VALUE_H
#define MANGOS_COMMANDER_AREA_VALUE_H

#include "Commander/Perception/Values/AggroTurnCost.h"
#include "Commander/Perception/WorldModel.h"
#include <functional>
#include <vector>

namespace Commander
{
    namespace AreaValue
    {
        static constexpr float TANK_GAIN_SHARE = 0.5f;
        static constexpr float MELEE_REACH = 5.0f;

        struct Request
        {
            PartyMemberFacts const* caster = nullptr;
            AreaSpellFacts const* spell = nullptr;
            Position3 centre;
            Position3 from;
            uint32 ticks = 0;               // 0 = all of the spell
            bool paid = false;              // the channel runs: mana paid, no settle check
            bool settle = true;             // false: a new enemy does not stop it (what a control gives up over its duration)
            AggroTurnCost::Options turn;    // extraAbsorb (a shield first), busyTanks
            std::function<bool(EnemyFacts const&)> forbidden;
        };

        struct Result
        {
            bool valid = false;
            uint32 ticks = 0;               // judged
            float damage = 0.0f;            // of `ticks`
            float benefit = 0.0f;
            float cost = 0.0f;              // time + turns (+ mana when not paid)
            float turnLoss = 0.0f;
            float mana = 0.0f;              // points of the mana (not paid)
            float net = 0.0f;
            std::vector<ObjectGuid> targets;
            std::vector<ObjectGuid> turning;    // enemies that turn to the caster within `ticks`
        };

        Result Evaluate(WorldModel const& world, Request const& r);

        // The caster's area spell facts of `spellId` (any rank of it), null = none.
        AreaSpellFacts const* SpellOf(PartyMemberFacts const& caster, uint32 spellId);
    }
}

#endif
