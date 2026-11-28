/*
 * PartyBot Commander layer 1: spell facts derived from DBC data.
 * Pure functions on SpellEntry (static data), no game objects.
 */

#ifndef MANGOS_COMMANDER_SPELL_TAGS_H
#define MANGOS_COMMANDER_SPELL_TAGS_H

#include "Common.h"

class SpellEntry;

namespace Commander
{
    // What an enemy cast does, for interrupt priority. Higher value = interrupt first
    // when one spell has effects of several categories. Values are not stored anywhere.
    enum class CastCategory : uint8
    {
        Other     = 0,
        Damage    = 1,
        ManaDrain = 2,  // mana burn / drain: lost mana is lost healing
        Summon    = 3,  // summons creatures (adds, totems, guardians)
        Control   = 4,  // fear, sleep, polymorph, charm, silence, stun on someone else
        Heal      = 5,
    };

    namespace SpellTags
    {
        CastCategory Categorize(SpellEntry const* spell);
        char const* CategoryName(CastCategory category);

        // Effect `i` puts a control aura (fear, stun, charm, sleep, silence, ...) on the
        // spell's target. Auras on the caster itself do not count (Serpent Form silences
        // its own caster).
        bool IsControlEffect(SpellEntry const* spell, uint32 i);
        // The narrower part of IsControlEffect that takes the target out of the fight: it
        // cannot attack or cast (fear, confuse / polymorph, stun / sleep / sap / shackle /
        // banish, charm). Silence and pacify are left out: the target still fights.
        bool IsDisableEffect(SpellEntry const* spell, uint32 i);
        // Mechanics of the spell's control effects, 1 << Mechanics: the effect's mechanic or
        // the spell's, a fear aura always MECHANIC_FEAR, a charm / possess MECHANIC_CHARM
        // (sleep: MECHANIC_SLEEP from the data). What a mechanic removal or immunity answers
        // (Tremor Totem: charm, fear, sleep; Fear Ward: fear). Boss doc G8.
        uint32 ControlMechanics(SpellEntry const* spell);

        // Some effect deals damage (direct, weapon, periodic, a ground area like Blizzard /
        // Consecration): wakes a target under a control that breaks on damage.
        bool DealsDamage(SpellEntry const* spell);

        // Largest effect radius in yards, 0 = none.
        float AreaRadius(SpellEntry const* spell);

        // Damage that jumps from the target to enemies next to it (Cleave, Multi-Shot, Chain
        // Lightning: EffectChainTarget > 1 on a damage effect). They are not area spells
        // (IsAreaOfEffectSpell), so the area checks missed them.
        bool ChainsToNearby(SpellEntry const* spell);

        // Takes all its target's threat away on the caster's list (SPELL_EFFECT_MODIFY_THREAT_PERCENT
        // -100 on the target), or the script effects that do it (SCRIPTED_THREAT_WIPES): cast on
        // the tank, the next on the list gets the enemy (Patchwork Horror's Knock Away,
        // strat-20261126-224508). Threat wipes in C++ boss scripts are not in the spell data
        // (their encounter module's).
        bool WipesTargetThreat(SpellEntry const* spell);
        static constexpr uint32 SCRIPTED_THREAT_WIPES[] = { 10101 };     // Knock Away (Spell::EffectScriptEffect)
        // How far a chained hit jumps from its target (guess: melee cleaves reach about this
        // far, ranged chains further but rarely matter next to a sleeping enemy).
        static constexpr float CHAIN_JUMP_YD = 8.0f;

        // An aura that makes the caster's melee hits also hit an enemy next to its victim:
        // that distance from the victim (UnitAuraProcHandler: Blade Flurry 5 yd, Sweeping
        // Strikes ATTACK_DISTANCE), 0 = none. Its cast deals no damage itself.
        float MeleeSplashRadius(SpellEntry const* spell);
        // The spells MeleeSplashRadius knows (the auras to look for on a bot).
        static constexpr uint32 MELEE_SPLASH_SPELLS[] = { 13877, 12292 };     // Blade Flurry, Sweeping Strikes

        // Effect `i` summons creatures (EffectMiscValue = creature entry). Object and
        // critter summons are left out.
        bool IsCreatureSummonEffect(uint32 effect);

        // Same condition Spell::EffectInterruptCast uses (SpellEffects.cpp ~3560):
        // silence-type prevention, and the cast is a non-instant generic cast with the
        // pushback interrupt flag, or a channel that action-cancels.
        bool IsInterruptible(SpellEntry const* spell, bool channeled, int32 castTimeMs);

        // Interrupts, silences and stuns a bot can use to stop a cast.
        bool StopsCasts(SpellEntry const* spell);

        // Mechanics that stop the cast (1 << (MECHANIC_x - 1)): spell and effect mechanics.
        // Same bits as WorldModelBuilder fills into InterruptAbility::mechanicMask.
        uint32 StopMechanicMask(SpellEntry const* spell);

        // A target with these creature_template immunities is not stopped by `spell`:
        // every stopping mechanic is immune (stun on a stun-immune boss), or the spell's
        // school is immune. Same bit rules as Creature::IsImmuneToSpell / IsImmuneToSpellEffect.
        bool IsStopImmune(SpellEntry const* spell, uint32 mechanicImmuneMask, uint32 schoolImmuneMask);
    }
}

#endif
