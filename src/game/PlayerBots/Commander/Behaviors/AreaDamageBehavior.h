/*
 * PartyBot Commander layer 3: area damage spells of damage dealers ("aoe" module).
 *
 * Since 2026-11-02 (user: the rotation's own "3 enemies near the victim" rule is too rough) the
 * rotation casts no area spells at all (Rotations/MageRotation.cpp: without the Commander the
 * mage casts Frostbolt). This module decides when, which and where; the arbiter casts it.
 * Before, it only vetoed the rotation's casts and judged an 8 s Blizzard's whole threat against
 * the tank's threat now, so Blizzard came when the pack was nearly dead (strat-20261102-205906).
 *
 * In combat, a victim, not walking for another module. For every ClassKit::AreaDamage spell:
 *   centre    a ground spell (Blizzard, onTarget): each enemy in sight as the centre, the one
 *             giving most damage wins; the others: the bot itself (a cone is counted as a
 *             circle and goes the way the bot faces). CanTryToCastSpell on the centre (range,
 *             mana, cooldown, PartyBotAI::PassesPartyCastChecks: no idle creature in the area).
 *   ticks     a channel deals its damage in duration / amplitude ticks (Blizzard 8 x 1 s),
 *             anything else at once after its busy time.
 *   safe      over the acting enemies whose edge is within the radius of the centre:
 *             - one the plan keeps unbroken (TeamPlan::KeepsUnbroken: polymorph, sap, a
 *               breakable stun on it, or about to be put on it by CrowdControlAssigner): not
 *               at all; since 2026-11-07 a fear or a stun damage does not end is no reason
 *               (it is not counted as a target either);
 *             - one hitting the bot or nobody: no limit;
 *             - a new one (seen < ThreatBehavior::NEW_ENEMY_SETTLE_SEC): not at all;
 *             - else it turns to the bot at the first tick k it survives (health > k ticks:
 *               the tick that kills it turns nothing) where my threat + my bolt still in the
 *               air towards it (cast finished, not landed) + k x tick threat passes (its
 *               victim's threat + TANK_GAIN_SHARE x the victim's measured threat rate x k x
 *               period) x 110% (bot in its melee reach) / 130% (ThreatView). The rate only
 *               counts when the victim is hitting this enemy (a party member hitting another
 *               one: 0). Bolt and rate rule since 2026-11-07 (strat-20261107-194655).
 *               From tick k on, the turn costs Perception/AggroTurnCost (its holder takes it
 *               back by threat or a taunt; a melee enemy hits only once it walked over, slowed
 *               by ClassKit's AreaDamageSpell::slowPct, Improved Blizzard). A holder that is
 *               not a party member: all its hits on the bot (Value::HitLoss).
 *             Tick threat = damage per tick x the spell's threat multiplier x the caster's
 *             threat modifiers.
 *   ticks     since 2026-11-08 (user: a Blizzard losing an enemy for a moment is fine, the
 *             slow lets the tank take it back; strat-20261108-133706: cancelled after 1 tick
 *             for a Berserker one tick from death): the number of ticks n with the best
 *             net = LostAction(damage(n) / filler damage per second) - LostAction(n x period)
 *             - the turns up to tick n. Before: the ticks before the first unsafe one.
 *   damage    sum over the targets of min(per tick x n, health); DamageSpells::Estimate
 *             on the victim (spell power included).
 *   mana      the area spell must not do less damage per mana than the filler over those n
 *             ticks (ClassKit::Filler on the victim). No fight length guess (adds come
 *             unannounced).
 *   value     benefit = LostAction(damage(n) / filler damage per second); cost =
 *             LostAction(n x period) + the turns. Proposed as a Cast, tier Assigned, when
 *             the net is positive.
 *
 * Channel watch (WatchChannel, every frame from PartyBotAI::UpdateAI, at most every
 * CHANNEL_CHECK_MS): while one of these spells is channelled, the same tick plan over the
 * ticks left, with live health, threat, positions and speeds of the enemies in the area at
 * the channel's destination. The mana is paid already, so going on also saves the filler's
 * mana for that time (ManaWorth). The channel is cancelled only when every number of further
 * ticks loses points.
 *
 * Known limits: crits, resistances and target caps are not counted; the tank's threat rate
 * is the recent one; enemies walking into Blizzard later are not counted; the tank's walk
 * after an enemy that runs to the bot is from where it stands now; a cone is judged as a
 * circle.
 */

#ifndef MANGOS_COMMANDER_AREA_DAMAGE_BEHAVIOR_H
#define MANGOS_COMMANDER_AREA_DAMAGE_BEHAVIOR_H

#include "Commander/Behaviors/IBehavior.h"

class SpellEntry;

namespace Commander
{
    class AreaDamageBehavior : public IBehavior
    {
    public:
        static constexpr float TANK_GAIN_SHARE = 0.5f;
        static constexpr float MELEE_REACH = 5.0f;       // bot within this of an enemy's edge: 110% rule
        static constexpr uint32 CHANNEL_CHECK_MS = 200;

        char const* GetId() const override { return "aoe"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;

        // `spell` is one of the bot's ClassKit::AreaDamage spells, this module is on and the bot
        // is in combat: its threat is judged here, PartyBotAI::PassesPartyCastChecks skips its
        // rough check (which would block the module's own casts).
        static bool Judges(PartyBotAI const* bot, SpellEntry const* spell);

        // Every frame from PartyBotAI::UpdateAI (channel watch above). True = cancelled.
        static bool WatchChannel(PartyBotAI* bot, uint32& lastCheckMs);
    };
}

#endif
