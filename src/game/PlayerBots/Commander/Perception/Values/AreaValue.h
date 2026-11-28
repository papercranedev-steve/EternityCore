/*
 * PartyBot Commander layer 1 (eyes): what one area damage spell of one bot is worth, in points
 * (AoE phase, .kiro/docs/2026-11-08-aoe-phase.md). The one valuation of area damage: the
 * brain's area phase (Coordination/Doctrine/AreaPhase.h) starts it with it, AreaDamageBehavior's channel
 * watch goes on or stops with it, ending the channel costs its value (Behaviors/CurrentAction.h),
 * and (until 2026-11-09, ControlValue::AreaLoss) a crowd control that kept an area spell off a
 * pack gave it up; that choice is now the doctrine's (Coordination/Doctrine/TacticalDoctrine.h).
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
 * On the caster (2026-11-14, strat-20261114-180150: a mage at the tank with two enemies on it
 * cast Arcane Explosion twice for a net +0.05 and went from 73% to 28% health; they were
 * "free"). Only while a tank other than the caster is alive (Tanks::Main; no tank: nobody
 * takes them back, nothing changes):
 *   - not paid: not valid when the caster would not live SurvivalValue::DANGER_SEC + the
 *     spell's busy time (at least a GCD) under what hits it now (SurvivalValue::Of, heals
 *     included): no more threat grabbing in danger (user 2026-11-14);
 *   - cost after n ticks: the enemies on it stay there longer: the tank (110% of the caster's
 *     threat, at its measured rate one after another, LooseEnemy::SequentialRecovery, plus
 *     its walk; taunts not counted) with the caster's threat now and with n ticks more; the
 *     longer time x their hits beyond the tank's (Value::Points) + Value::HarassLoss, at most
 *     AggroTurnCost::HORIZON_SEC and their time to die. Added to the turn loss.
 * Tick plan (as AreaDamageBehavior before, now on facts): damage after n ticks = sum of
 * min(perTick x n, health). An enemy turns at the first tick k it survives where my threat (+
 * my bolt in the air at it) + k x tickThreat passes its holder's threat now x 110% (caster
 * within its melee reach from `from`) / 130%: the holder is counted as adding nothing during
 * the spell, the tank cannot hold (user 2026-11-12, doc .kiro/docs/2026-11-12-mage-hold.md
 * 3.1: the mage plays the worst case, a tank that holds is a gain). Turn loss after n ticks
 * = AggroTurnCost::OfGroup over every enemy turned by then (with the shield, `from`, the busy
 * tanks; the caster dying under all of them is in there). A holder that is not a party member:
 * all its hits (Value::HitLoss).
 * Pushback (a channel, 2026-11-12, doc .kiro/docs/2026-11-11-aoe-threat.md 4.4): the enemies
 * turned by then (from their tick + walk) and those on the caster already swing at it until
 * the end (the tank taking them back sooner is not counted); each hit the caster's absorbs (+
 * the shield put up first) do not take whole cuts the channel's end (Spell::DelayedChannel).
 * The share of the n ticks lost x (benefit - time cost) is added to the turn loss. This is
 * what Ice Barrier keeps ("While the shield holds, spells will not be interrupted").
 * Fodder (Request::noHold, user 2026-11-28, rivendare-20261128-230646: Arcane Explosion on six
 * Mindless Skeletons was refused all fight as "a new enemy, the tank has no threat on it yet";
 * the tank never takes them): such an enemy is free whoever it hits, it does not stop the
 * spell while new, turns nothing and is not "kept on the caster". The danger rule below still
 * counts it, with the shield put up first (turn.extraAbsorb) on the caster's health.
 * Others (2026-11-15, several casters in one phase): the damage per second other area spells of
 * the phase put on an enemy is taken off its health first, tick by tick (the worst case for
 * this spell): what this one does to it and the tick that kills it come sooner.
 * Later (Request::inSec, user 2026-11-29: adds run at whoever they hit while the caster walks
 * to its spot): every position of an enemy above (in the radius, melee reach of `from`, its
 * walk back) is where Runners::PosAfter puts it after inSec; health and threat stay as now.
 * Value of n ticks: benefit = LostAction(damage(n) / filler damage per second) [+ paid: the
 * filler's mana for that time, ManaWorth], cost = LostAction(n x period) + turn loss(n).
 *   not paid  the whole spell is judged (user 2026-11-08: a Blizzard is paid in full at the
 *             start, so starting one planned to stop is wasted mana): net = value(all ticks) -
 *             ManaWorth(mana above what the filler spends in that time); not valid when it does
 *             less damage per mana than the filler.
 *   paid      the best n of the ticks left (the channel watch stops it only when every n loses).
 * Melee area spells (2026-11-26, warrior Whirlwind):
 *   At most N targets  AreaSpellFacts::maxTargets (Whirlwind 4, the server picks them at
 *             random): with more in the circle the damage is the expected share; the turns
 *             are counted as if each were hit.
 *   Rage and energy  as the mana rules, with the filler's own power (fillerPowerPerSec, same
 *             power type): not valid with less damage per point than the filler; the power
 *             above what the filler spends in the same time costs the filler seconds it
 *             would have paid for, LostAction(extra / fillerPowerPerSec) (Result::power).
 *             White swings go on through an instant melee spell: its time cost is the GCD
 *             the filler loses, as for a caster.
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
        static constexpr float MELEE_REACH = 5.0f;

        struct OtherDamage
        {
            ObjectGuid enemy;
            float perSec = 0.0f;
        };

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
            // The tank need not hold it (the brain's doctrine NO_HOLD: fodder, a trap swarm;
            // header "Fodder"): free, no settle wait, no turn, not kept on the caster.
            std::function<bool(EnemyFacts const&)> noHold;
            // Other area spells of the phase on the same enemies (AreaPhase: the casters planned
            // before this one): damage per second each enemy takes from them. This spell only
            // counts what they leave of its health (header "Others").
            std::vector<OtherDamage> others;
            // The spell starts this many seconds from now (the caster walks to `from` first):
            // enemies are judged where Runners::PosAfter puts them then, those on the caster
            // running at `from` (header "Later"). 0 = where they are now.
            float inSec = 0.0f;
        };

        struct Result
        {
            bool valid = false;
            char const* invalid = nullptr;  // why not valid (the calculation trace)
            uint32 ticks = 0;               // judged
            float damage = 0.0f;            // of `ticks`
            float benefit = 0.0f;
            float cost = 0.0f;              // time + turns (+ mana when not paid)
            float turnLoss = 0.0f;
            float mana = 0.0f;              // points of the mana (not paid)
            float power = 0.0f;             // points of the rage or energy (not paid)
            float net = 0.0f;
            std::vector<ObjectGuid> targets;
            std::vector<ObjectGuid> turning;    // enemies that turn to the caster within `ticks`
        };

        Result Evaluate(WorldModel const& world, Request const& r);

        // The caster's area spell facts of `spellId` (any rank of it), null = none.
        AreaSpellFacts const* SpellOf(PartyMemberFacts const& caster, uint32 spellId);

        // How long the spell's slow lingers once an enemy has left its area (Improved Blizzard's
        // Chilled, renewed every tick inside: 1.5 s on average after the last one).
        static constexpr float SLOW_LINGER_SEC = 1.5f;

        // Seconds an enemy at `from` running at `speed` takes to get within `reach` of `to`,
        // slowed by `slowPct` only while it is inside the area (`centre`, `radius`) and
        // SLOW_LINGER_SEC after (2026-11-12, strat-20261112-204141: the slow was counted over
        // the whole walk, so a mage 22-27 yd away looked out of reach for the whole channel).
        float WalkSecUnder(Position3 const& from, float speed, float slowPct, Position3 const& centre, float radius,
                           Position3 const& to, float reach);

        // `e` running at `to` from the spell's area: the average speed of that walk
        // (WalkSecUnder; an enemy slowed already keeps its speed, the chill is not counted twice).
        float SpeedTo(EnemyFacts const& e, AreaSpellFacts const& spell, Position3 const& centre, Position3 const& to);
    }
}

#endif
