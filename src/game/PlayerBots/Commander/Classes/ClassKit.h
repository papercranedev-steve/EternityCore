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
        // Speed its hits take off the enemies (0..1), from the caster's talents (Improved
        // Blizzard): a melee enemy it pulls takes longer to get to the caster.
        float slowPct = 0.0f;
    };

    // What a "keep someone alive" spell does (team rescue, Perception/Values/SurvivalValue.h).
    enum class SurvivalEffect : uint8
    {
        Absorb,         // absorbs damage (Power Word: Shield; amount from the spell)
        Reduce,         // damage taken x factor for its duration (Shield Wall; from the spell's aura)
        Immune,         // no damage for its duration (Divine Shield, Ice Block)
        ImmunePhysical, // no physical damage for its duration (Blessing of Protection)
        Heal,           // heals the caster's maximum health (Lay on Hands; uses all mana)
        ExtraHealth,    // more health for a while: `fraction` of maximum health (Last Stand)
        DropThreat,     // the enemies on it turn to someone else (Vanish, Feign Death)
        // Melee hits taken x `fraction` for its duration (Evasion, Deterrence: dodge / parry;
        // Barkskin, Stoneform), the rest as it is.
        ReducePhysical,
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

    // An instant off the global cooldown that makes the next heal better (HealPlanBehavior,
    // cooldown doc .kiro/docs/2026-11-14-cooldowns.md 7.1): put on right before it
    // (BotAction::prepSpell).
    // ClassKit::CombatHealUse.
    enum class HealUse : uint8
    {
        Normal,
        EmergencyOnly,
        Never,
    };

    struct HealBoostSpell
    {
        SpellEntry const* spell = nullptr;
        bool free = false;          // the next spell costs no mana (Inner Focus)
        float critChance = 0.0f;    // added to the next heal's crit chance (Inner Focus 0.25, Divine Favor 1)
        bool instant = false;       // the next heal with a cast time is instant (Nature's Swiftness)
    };

    // A damage cooldown (BurstBehavior, Perception/Values/BurstValue.h, cooldown doc
    // .kiro/docs/2026-11-14-cooldowns.md 6). A buff on itself (damageMult while it lasts), or a
    // pairing (`then`: put on right before that cast, BotAction::prepSpell; extraSec).
    // Numbers are untested guesses: what it adds, as a share of the bot's own damage.
    struct BurstSpell
    {
        SpellEntry const* spell = nullptr;
        float damageMult = 0.0f;        // own damage added while it lasts (Rapid Fire 0.3)
        float extraSec = 0.0f;          // pairing: seconds of own damage the boosted cast gains
        SpellEntry const* then = nullptr;   // pairing: the cast it boosts (Cold Blood: Eviscerate)
        float durationSec = 0.0f;       // 0 = the spell's (a buff used up by charges: 10 s)
        float damageTakenUp = 0.0f;     // damage taken x (1 + this) while it lasts (Recklessness 0.2)
        uint8 minEnemiesNear = 0;       // enemies around the bot (Blade Flurry, Sweeping Strikes 2)
        uint8 minAttackersOnSelf = 0;   // melee enemies hitting it (Retaliation 3)
        float minManaPct = 0.0f;        // Arcane Power 50 (its spells cost 30% more)
        bool notTank = false;           // armor or healing taken goes down (Death Wish, Blood Fury)
        bool healTakenCut = false;      // healing taken halved: not while it is hit (Blood Fury)
        bool needsPet = false;          // on the pet, the pet on the bot's victim (Bestial Wrath)
        bool needsCombo = false;        // pairing with a finisher: 5 combo points on the victim
        bool thenNotOnTarget = false;   // pairing with a curse: not while the bot's curse is up (Amplify Curse)
    };

    // A cooldown cast on a party member to help it (BoostAssigner, cooldown doc 2026-11-14 4).
    enum class AllyBoostKind : uint8
    {
        Mana,       // its mana regenerates faster (Innervate)
        Power,      // its spells do more (Power Infusion)
        PartyMana,  // mana for every member around the caster (Mana Tide Totem)
    };

    struct AllyBoostSpell
    {
        SpellEntry const* spell = nullptr;
        AllyBoostKind kind = AllyBoostKind::Mana;
        float manaPerTarget = 0.0f;     // PartyMana: mana each member gets over its duration
        float radius = 0.0f;            // PartyMana: around the caster
    };

    class ClassKit
    {
    public:
        explicit ClassKit(PartyBotAI const& bot) : m_bot(bot) {}
        virtual ~ClassKit() = default;

        // Interrupts the Commander assigns (InterruptAssigner, WorldModel interrupt facts).
        virtual std::vector<SpellEntry const*> Interrupts() const { return {}; }
        // Friendly dispels it has learned (best rank, fallbacks when the better spell is not
        // learned yet): what DispelAssigner can give it. Only the Commander casts them in
        // combat (DispelBehavior); out of combat the rotation does.
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
        // Drops its own threat so an enemy hitting the bot turns to someone else: on every
        // enemy for a while (priest Fade), off every list (rogue Vanish, hunter Feign Death) or
        // on its target only (rogue Feint, cat Cower). What each does is read from the spell data
        // (WorldModelBuilder fills ThreatDropAbility). Only once an enemy already hits the bot
        // (user 2026-11-14: not before, it would break the damage rhythm); the team rescue
        // decides which (RescueAssigner "loose enemy", RescueBehavior).
        virtual std::vector<SpellEntry const*> ThreatDrops() const { return {}; }
        bool HasThreatDrop() const
        {
            for (SpellEntry const* s : ThreatDrops())
                if (s)
                    return true;
            return false;
        }
        // Instant absorb on an ally: the healer itself hit while casting (HealPlanBehavior),
        // last resort while running (RetreatBehavior).
        virtual SpellEntry const* AllyShield() const { return nullptr; }
        // Spells that keep a member about to die alive. Only the Commander casts them
        // (team rescue: RescueAssigner picks one per member, RescueBehavior casts it).
        virtual std::vector<SurvivalSpell> Survival() const { return {}; }
        // Heal boosts (Inner Focus, Divine Favor, Nature's Swiftness). The heal module puts one
        // on before the heal it suits (a big deficit; an emergency the heal would not reach in
        // time); the rotations do not cast them while it decides heals.
        virtual std::vector<HealBoostSpell> HealBoosts() const { return {}; }
        // How the heal module may use one of its single-target heals while the party fights
        // (user 2026-11-15): Renew never (Heal ranks do more per mana), Regrowth only in an
        // emergency (a fast heal: expensive, its HoT is a bonus).
        virtual HealUse CombatHealUse(SpellEntry const* /*heal*/) const { return HealUse::Normal; }
        // Turns a HoT on the target into an instant heal and removes it (Swiftmend). The heal
        // module casts it: right before the HoT's last tick (nearly free), or in an emergency.
        virtual SpellEntry const* HotConsumer() const { return nullptr; }
        // Ticks of `hot` HotConsumer turns into its heal (Swiftmend, spell_druid.cpp:
        // Rejuvenation 4, Regrowth 6); 0 = it cannot eat it.
        virtual uint32 HotConsumeTicks(SpellEntry const* /*hot*/) const { return 0; }
        // Cooldowns for a party member (Innervate, Power Infusion, Mana Tide Totem): the brain picks
        // whom (BoostAssigner), BoostBehavior casts them; the rotations do not.
        virtual std::vector<AllyBoostSpell> AllyBoosts() const { return {}; }
        // Damage cooldowns: only BurstBehavior casts them, the rotations do not. Racial ones
        // come from RaceKit (merged in WorldModelBuilder).
        virtual std::vector<BurstSpell> Bursts() const { return {}; }
        // A heal on itself that costs no mana (priest Desperate Prayer): the heal module casts it
        // when the bot is hit and misses about that much health (also non-healer bots); the team
        // rescue may pick it for a bot about to die (it is in Survival too).
        virtual SpellEntry const* FreeSelfHeal() const { return nullptr; }
        // Area damage spells the Commander casts on packs (the brain's area phase decides who,
        // which, where and when, Coordination/Doctrine/AreaPhase.h; AreaDamageBehavior casts it; the
        // rotations do not cast them).
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
        // Breaks a fear on itself (warrior Berserker Rage): hands and feet, cast as soon as the
        // bot is feared (PartyBotAI::BreakControl), after the racial one (RaceKit::FearBreak).
        virtual SpellEntry const* FearBreak() const { return nullptr; }
        // Stuns, roots and slows that keep an enemy running away at low health from getting
        // far (flee interception, FleeStopAssigner, todo 13). What each does (stun / root
        // duration, slow percent, range) is read from the spell data (WorldModelBuilder fills
        // FleeStopAbility). Only the Commander casts them for that: the rotations keep their
        // own uses, except while the plan holds the spell (TeamPlan::HoldsSpell).
        virtual std::vector<SpellEntry const*> FleeStoppers() const { return {}; }
        // The finisher that spends combo points as damage on the current target (rogue
        // Eviscerate, cat Ferocious Bite): cast before a planned target switch so the points
        // are not lost (TargetBehavior, a suggested switch).
        virtual SpellEntry const* ComboFinisher() const { return nullptr; }
        // Its own stealth (rogue Stealth): the Sap controller of a pull goes into stealth on
        // its way (CrowdControlBehavior, Takeover::Stealth); the rotation decides otherwise.
        virtual SpellEntry const* Stealth() const { return nullptr; }
        // An absorb the bot puts on itself before an area spell that may turn enemies to it
        // (mage Ice Barrier). The area phase decides it (Coordination/Doctrine/AreaPhase.h).
        virtual SpellEntry const* SelfShield() const { return nullptr; }
        // Turns its health into mana (warlock Life Tap): its mana is worth less (ManaWorth.h).
        // Only the Commander casts it (HealthTradeBehavior: how low it may tap depends on the
        // healers it can count on); the rotation does not.
        virtual SpellEntry const* HealthToMana() const { return nullptr; }
        // A channel that damages its target and heals the bot (warlock Drain Life): its own heal,
        // also with healers around (they may not keep up). Only HealthTradeBehavior casts it.
        virtual SpellEntry const* DrainHeal() const { return nullptr; }
        // A channel that gives an item when its target dies while it runs (warlock Drain Soul: a
        // Soul Shard; the item is the spell's SPELL_AURA_CHANNEL_DEATH_ITEM effect). Only
        // HarvestBehavior casts it, on an enemy about to die; the rotation does not.
        virtual SpellEntry const* DeathHarvest() const { return nullptr; }
        // A spell the pet casts on itself to stay out of the fight while its party aura still
        // helps (warlock Imp: Phase Shift, user 2026-11-15: the Imp does no damage, so it does
        // not die). While the pet knows it, the rotation keeps it on (ClassRotation::
        // KeepPetAside) and nothing sends the pet at an enemy (ThreatBehavior::ProposePet).
        virtual SpellEntry const* PetStandAside() const { return nullptr; }
        // A jump forward the way it faces (mage Blink): the area caster gets room from what
        // runs at it (AreaRoomBehavior, doc .kiro/docs/2026-11-12-mage-hold.md 3.4).
        virtual SpellEntry const* Leap() const { return nullptr; }
        // A slow in a cone in front of it (mage Cone of Cold): the area caster slows what runs
        // at it (AreaRoomBehavior, doc 3.5). Not an area damage spell any more.
        virtual SpellEntry const* SlowCone() const { return nullptr; }
        // Yards from its victim's edge inside which its attack does not work (hunter Auto Shot,
        // the dead zone). The ranged stance keeps it out (RangedStanceBehavior), the kite module
        // gets it out from an enemy on it (KiteBehavior). 0 = none.
        virtual float MinAttackRange() const { return 0.0f; }
        // Slows that a melee enemy on the bot gets before the bot runs out of its dead zone
        // (hunter Wing Clip; KiteBehavior). What each does is read from the spell data.
        virtual std::vector<SpellEntry const*> KiteSlows() const { return {}; }
        // Cast on its pet: the pet's next hit stuns the pet's target (hunter Intimidation).
        // KiteBehavior's fallback when no slow takes.
        virtual SpellEntry const* PetStun() const { return nullptr; }

    protected:
        PartyBotAI const& m_bot;
    };

    namespace ClassKits
    {
        // The kit for the bot's class (Player::GetClass); the base kit (has nothing) for others.
        std::unique_ptr<ClassKit> Create(PartyBotAI const& bot);

        // One per class file (Classes/<Class>.cpp).
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
