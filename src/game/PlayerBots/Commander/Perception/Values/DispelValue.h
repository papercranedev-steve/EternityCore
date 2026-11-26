/*
 * PartyBot Commander layer 1 (eyes): what removing one debuff from a party member is worth, in
 * points (value-scoring doc), in combat. A fact for DispelAssigner (brain), which picks who
 * dispels what. Replaces the dispel vetoes of DispelBehavior (until 2026-11-03 the rotation
 * dispelled everything and the module vetoed the bad ones).
 *
 *   hand-written policy (custom_partybot_spell_override.dispel): Never / AfterCombat = 0;
 *            Now = treated like a control effect for its whole duration.
 *   control  (SpellProfile::controls: stun, sleep, fear, charm, silence, ...):
 *            Value::LostAction(member, time left); a control that breaks on damage on a member
 *            being hit: only WAKE_ON_DAMAGE_SEC (the next hit ends it, combat scenario 3).
 *   other effects, added up:
 *     periodic damage   ticks per second x the seconds it would still tick
 *     mana leech        mana per second x the seconds it would still drain (at most the
 *                       member's mana), CastValues::DrainedManaPoints; manaCritical when a
 *                       healer bot cannot spare it (ManaWorth::Critical). On a single target
 *                       channel (Soul Tap) the dispel also ends the caster's channel
 *                       (Unit::RemoveSpellAuraHolder interrupts it), so "would still" is the
 *                       channel time left: the same gain as interrupting it
 *     roots, slows      LostAction(member, min(left, ROOT_SEC / SLOW_SEC)) while the member has
 *                       to move (Perception/Spatial/MoveNeed.h, or `orderedToMove`: the plan's
 *                       retreat), else 0 (strat-20261102-012937: Dispel Magic on a chilled
 *                       rogue, twice; user 2026-11-26: roots the same, Blessing of Freedom)
 *     anything else     (stat, armor, cast speed debuffs ...) LostAction(member, min(left,
 *                       OTHER_SEC)): their effect is not estimated yet; one global cooldown of
 *                       the member's output roughly matches "worth a dispel when a healer has
 *                       the time", which is what the rotation used to do with them
 *   "would still" = time left, capped at DebuffFacts::reapplySec when the caster puts it back
 *   (decision 2026-11-01: only reapplied debuffs are judged by that), but not below the
 *   removal's own immunity (`immuneSec`: Blessing of Freedom keeps roots and slows off).
 */

#ifndef MANGOS_COMMANDER_DISPEL_VALUE_H
#define MANGOS_COMMANDER_DISPEL_VALUE_H

#include "Common.h"

namespace Commander
{
    struct WorldModel;
    struct PartyMemberFacts;
    struct DebuffFacts;

    namespace DispelValue
    {
        static constexpr float WAKE_ON_DAMAGE_SEC = 2.0f;
        static constexpr float SLOW_SEC = 3.0f;
        // A root stops the walk altogether: more of it is lost than to a slow.
        static constexpr float ROOT_SEC = 4.0f;
        static constexpr float OTHER_SEC = 1.5f;

        struct Worth
        {
            float points = 0.0f;
            bool control = false;   // the member cannot act (or a Now policy): urgent
            // A mana leech takes mana the healer bot needs for this fight (ManaWorth::Critical): urgent.
            bool manaCritical = false;
        };

        // immuneSec: the removal keeps the debuff's kind off that long (DispelAbility::immuneSec).
        // orderedToMove: the plan moves the member (retreat), whatever MoveNeed says.
        Worth Of(WorldModel const& world, PartyMemberFacts const& member, DebuffFacts const& debuff,
                 float immuneSec = 0.0f, bool orderedToMove = false);
    }
}

#endif
