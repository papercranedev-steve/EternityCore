/*
 * PartyBot Commander layer 3: area damage spells of damage dealers ("aoe" module).
 *
 * The rotation casts its area spells (ClassKit::AreaDamage: mage Blizzard, Arcane Explosion,
 * Cone of Cold, Blast Wave) on AREA_MIN_ENEMIES or more enemies (Rotations/MageRotation.cpp).
 * This module vetoes (VetoType::Cast, PartyBotAI::IsCastVetoed) the ones that are not worth
 * it or not safe now (user, 2026-11-01: an area spell must not bring the pack onto the mage,
 * which then gets interrupted and wastes the mana).
 *
 * For every area spell the rotation could cast now (castable on its centre: the victim for
 * Blizzard, the bot itself otherwise), over the acting, not controlled enemies whose edge is
 * within the spell radius of the centre (a cone is counted as a circle):
 *   damage    per target: DamageSpells::Estimate on the victim (spell power included).
 *   threat    per target: damage x the spell's threat multiplier x the caster's threat
 *             modifiers (Frost Channeling, Arcane Subtlety, ...).
 *   Unsafe    an enemy not hitting the bot is new (seen < ThreatBehavior::NEW_ENEMY_SETTLE_SEC:
 *             the tank has not built threat on it yet), or the bot's threat on it plus the
 *             spell's would pass its victim's threat x 110% (the bot within melee reach of it)
 *             / 130% (otherwise; ThreatView), after TANK_GAIN_SHARE of the threat its victim
 *             builds meanwhile at its measured rate over the spell's busy time.
 *   Not worth the area damage (each target at most its health) does less than the filler
 *             (ClassKit::Filler on the victim) per second of casting or per mana.
 * Either: veto the spell (all ranks) for Arbiter::VETO_TICKS ticks. Not valued (the arbiter
 * never drops it): the comparison above already is the value.
 *
 * Known limits: crits, resistances and target caps are not counted; the tank's threat rate
 * is the recent one; enemies walking into Blizzard later are not counted.
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

        char const* GetId() const override { return "aoe"; }
        bool AppliesTo(PartyBotAI const* bot) const override;
        void Propose(PartyBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;

        // `spell` is one of the bot's ClassKit::AreaDamage spells, this module is on and the bot
        // is in combat (only then the arbiter runs it before the rotation): its threat is judged
        // here, PartyBotAI::PassesPartyCastChecks skips its rough check.
        static bool Judges(PartyBotAI const* bot, SpellEntry const* spell);
    };
}

#endif
