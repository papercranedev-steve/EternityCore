/*
 * PartyBot Commander: what a bot's class can do, one implementation per class.
 *
 * Commander modules are split by concern (threat, interrupts, escape, ...) and decide when and
 * whether; the class kit answers "which of my spells does this job" (user, 2026-11-01: per-class
 * logic without a switch on the class in every module). Each class overrides only what it has;
 * the defaults mean "this class has none". Adding an ability to a class touches its own file
 * (Classes/<Class>.cpp), not the modules.
 *
 * Injection: PartyBotAI creates its kit once the bot is in the world and its spells are known
 * (ClassKits::Create, PartyBotAI::OnPlayerLogin path / UpdateAI init) and modules ask
 * bot->Kit(). The kit keeps a reference to its bot and reads bot.m_spells on every call, so
 * spell data rebuilt after a role, level or talent change (ResetSpellData) is seen at once.
 * Lists may contain nullptr for spells the bot has not learned; callers skip them.
 *
 * Not here: class facts that also apply to players without a PartyBot AI (PullPlanner's ranged
 * pull of a player tank, ThreatView's heal threat modifier of a paladin): they read the unit.
 * Mechanics of casting (warrior stance swaps, druid forms) stay in the rotation (PartyBotAI).
 */

#ifndef MANGOS_COMMANDER_CLASS_KIT_H
#define MANGOS_COMMANDER_CLASS_KIT_H

#include "Common.h"
#include <memory>
#include <vector>

class PartyBotAI;
class SpellEntry;

namespace Commander
{
    // An area threat ability of a tank (ThreatBehavior, Deadmines VanCleef module).
    struct AreaThreatSpell
    {
        SpellEntry const* spell = nullptr;
        // Threat put on each enemy, as a fraction of one global cooldown of single target threat
        // (fallback when the spell has no threat data: ThreatBehavior ThreatPerEnemy).
        float share = 0.0f;
        bool taunt = false;         // forces every enemy around onto the tank (Challenging Shout / Roar)
        bool onTarget = false;      // cast at the current victim (Thunder Clap, Swipe) instead of self
        bool primary = false;       // the class's main one (Thunder Clap, Consecration, Swipe), not a shout
    };

    // An area damage spell of a damage dealer (AreaDamageBehavior).
    struct AreaDamageSpell
    {
        SpellEntry const* spell = nullptr;
        // Centred on the current victim (Blizzard) instead of around the caster (Arcane
        // Explosion, Cone of Cold: a cone, counted as a circle).
        bool onTarget = false;
    };

    // What a "keep someone alive" spell does (team rescue, Perception/SurvivalValue.h).
    enum class SurvivalEffect : uint8
    {
        Absorb,         // absorbs damage (Power Word: Shield; amount from the spell)
        Reduce,         // damage taken x factor for its duration (Shield Wall; from the spell's aura)
        Immune,         // no damage for its duration (Divine Shield, Ice Block)
        ImmunePhysical, // no physical damage for its duration (Blessing of Protection)
        Heal,           // heals the caster's maximum health (Lay on Hands; uses all mana)
        ExtraHealth,    // more health for a while: `fraction` of maximum health (Last Stand)
        DropThreat,     // the enemies on it turn to someone else (Vanish, Feign Death)
    };

    struct SurvivalSpell
    {
        SpellEntry const* spell = nullptr;
        SurvivalEffect effect = SurvivalEffect::Absorb;
        float fraction = 0.0f;      // ExtraHealth
        float durationSec = 0.0f;   // 0 = the spell's duration (Last Stand: its triggered aura's)
        bool selfOnly = false;
        bool notOnTank = false;     // not on a tank: it stops the tank holding (Blessing of Protection, Divine Shield)
        bool needsShield = false;   // Shield Wall
        bool casterStops = false;   // the caster cannot act while it lasts (Ice Block)
        bool targetStops = false;   // the target cannot attack while it lasts (Blessing of Protection)
        bool allMana = false;       // costs all the caster's mana (Lay on Hands)
    };

    class ClassKit
    {
    public:
        explicit ClassKit(PartyBotAI const& bot) : m_bot(bot) {}
        virtual ~ClassKit() = default;

        // Interrupts the Commander assigns (InterruptAssigner, WorldModel interrupt facts).
        virtual std::vector<SpellEntry const*> Interrupts() const { return {}; }
        // Interrupts the class rotation casts by itself behind PartyBotAI::SkipsReflexInterrupt:
        // only when the interrupt module does not control the bot (module off, no commander);
        // VetoChecks::ImmuneToReflexInterrupts checks immunity against these.
        virtual std::vector<SpellEntry const*> ReflexInterrupts() const { return {}; }
        // Friendly dispels it has learned (best rank, fallbacks when the better spell is not
        // learned yet): what DispelAssigner can give it. Only the Commander casts them in
        // combat (Takeover::Dispel); out of combat the rotation does.
        virtual std::vector<SpellEntry const*> Dispels() const { return {}; }
        bool CanDispel() const
        {
            for (SpellEntry const* s : Dispels())
                if (s)
                    return true;
            return false;
        }
        // Crowd control it can put on enemies: polymorph, sap, shackle, sleep, banish, fears
        // (crowd control doc .kiro/docs/2026-11-07/2026-11-07-crowd-control.md). What each
        // spell does (creature types, duration, area, breaks on damage) is read from its data
        // (WorldModelBuilder fills ControlAbility); ControlValue values them.
        virtual std::vector<SpellEntry const*> Controls() const { return {}; }
        // Area threat abilities, strongest first (ThreatBehavior group threat).
        virtual std::vector<AreaThreatSpell> AreaThreat() const { return {}; }
        // Roots / stuns that stop a melee enemy chasing the bot (EscapeBehavior; what each spell
        // does is read from its data there).
        virtual std::vector<SpellEntry const*> HoldSpells() const { return {}; }
        // Absorb shields on itself the rotation puts up when attacked (EscapeBehavior counts them
        // into survival before they are cast). Active absorbs are read from auras for every class.
        virtual std::vector<SpellEntry const*> ReactiveShields() const { return {}; }
        // Drops its own threat for a while so enemies turn to someone else (priest Fade). Only
        // the Commander casts it (ThreatBehavior takes it over, Takeover::ThreatDrop).
        virtual SpellEntry const* ThreatDrop() const { return nullptr; }
        // Instant absorb on an ally: the healer itself hit while casting (HealPlanBehavior),
        // last resort while running (RetreatBehavior).
        virtual SpellEntry const* AllyShield() const { return nullptr; }
        // Spells that keep a member about to die alive. Only the Commander casts them
        // (team rescue: RescueAssigner picks one per member, Takeover::Survival).
        virtual std::vector<SurvivalSpell> Survival() const { return {}; }
        // Area damage spells the Commander casts on packs (AreaDamageBehavior decides when and
        // where; the rotations do not cast them).
        virtual std::vector<AreaDamageSpell> AreaDamage() const { return {}; }
        // The single target damage spell the rotation casts most: what an area spell is compared
        // with (AreaDamageBehavior) and how fast mana goes (ManaBudgetBehavior self restore).
        virtual SpellEntry const* Filler() const { return nullptr; }
        // Channelled restore of its own mana (Evocation). Only the Commander casts it
        // (ManaBudgetBehavior): in combat, when the mana would run out long before the fight ends.
        virtual SpellEntry const* SelfManaRestore() const { return nullptr; }
        // Charge at an enemy to open a fight (warrior Charge). Only the Commander casts it
        // (PullBehavior: a walk-in pull fought where the pack stands); the tank rotation does not.
        virtual SpellEntry const* OpenerCharge() const { return nullptr; }

    protected:
        PartyBotAI const& m_bot;
    };

    namespace ClassKits
    {
        // The kit for the bot's class (Player::GetClass); the base kit (has nothing) for others.
        std::unique_ptr<ClassKit> Create(PartyBotAI const& bot);

        // One per class file (Classes/<Class>.cpp). Hunters: Feign Death only so far (todo item 18).
        std::unique_ptr<ClassKit> MakeWarrior(PartyBotAI const& bot);
        std::unique_ptr<ClassKit> MakePaladin(PartyBotAI const& bot);
        std::unique_ptr<ClassKit> MakeRogue(PartyBotAI const& bot);
        std::unique_ptr<ClassKit> MakePriest(PartyBotAI const& bot);
        std::unique_ptr<ClassKit> MakeShaman(PartyBotAI const& bot);
        std::unique_ptr<ClassKit> MakeMage(PartyBotAI const& bot);
        std::unique_ptr<ClassKit> MakeDruid(PartyBotAI const& bot);
        std::unique_ptr<ClassKit> MakeWarlock(PartyBotAI const& bot);
        std::unique_ptr<ClassKit> MakeHunter(PartyBotAI const& bot);
    }
}

#endif
