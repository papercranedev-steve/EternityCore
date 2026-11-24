/*
 * PartyBot Commander layer 1 (perception): facts about one party on one map.
 * Filled by Perception/WorldModelBuilder.cpp, read-only for every other layer.
 * Plain data only (GUIDs, numbers), so the query functions in ThreatView /
 * PackResolver can later be tested on recorded snapshots without a server.
 */

#ifndef MANGOS_COMMANDER_WORLD_MODEL_H
#define MANGOS_COMMANDER_WORLD_MODEL_H

#include "Common.h"
#include "ObjectGuid.h"
#include "RoleCapability.h"
#include "Commander/CommanderTypes.h"
#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

namespace Commander
{
    class PositionField;
    struct Position3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // A bot spell that can stop an enemy cast (interrupt, stun, silence).
    struct InterruptAbility
    {
        uint32 spellId = 0;
        float maxRange = 0.0f;       // spell range; melee abilities are ~5
        uint32 cooldownMs = 0;       // full cooldown, cost of using it
        uint32 lockoutMs = 0;        // school lockout of an interrupt effect (0 for stuns and silences)
        // Mechanics that stop the cast (1 << MECHANIC_x): spell mechanic and effect mechanics.
        // A target immune to all of them is not stopped (stun on a stun-immune boss).
        uint32 mechanicMask = 0;
        bool ready = false;          // off cooldown and enough power (target checks happen at cast time)
        // The target must face the caster (Gouge: Spell::CheckCast, attributes 0x150010). An
        // enemy faces its victim, so only its victim can count on it.
        bool needsFront = false;
        // What it puts on the enemy ends on the next hit (Gouge: AURA_INTERRUPT_DAMAGE_CANCELS).
        bool breaksOnDamage = false;
    };

    // A friendly dispel a bot has learned (ClassKit::Dispels).
    struct DispelAbility
    {
        uint32 spellId = 0;
        uint32 dispelMask = 0;       // 1 << DispelType of every SPELL_EFFECT_DISPEL
        float maxRange = 0.0f;
        bool ready = false;          // off cooldown, enough mana, can act and cast
        // Can also target a member mind controlled by a creature (Dispel Magic; the friendly-only
        // ones fail on such a target: TeamBotAI::SelectDispelTarget).
        bool hostileOk = false;
    };

    // A crowd control spell a bot has learned (ClassKit::Controls): what it can take out of the
    // fight and for how long. Read from the spell data (Perception/Values/ControlValue.h values it).
    // Control kinds for the doctrine (doc .kiro/docs/2026-11-09-doctrine 3.4):
    //   Mez   breaks on damage (polymorph, shackle, sap, sleep): no area spell near it
    //   Fear  runs around (Psychic Scream, Fear): scatters a pile
    //   Hold  damage does not end it (stun, banish): a pile under it can still be hit
    enum class ControlKind : uint8 { Mez, Fear, Hold };
    inline ControlKind KindOfControl(bool breaksOnDamage, bool fear)
    {
        return fear ? ControlKind::Fear : breaksOnDamage ? ControlKind::Mez : ControlKind::Hold;
    }

    struct ControlAbility
    {
        uint32 spellId = 0;
        uint32 creatureTypeMask = 0;    // SpellEntry::TargetCreatureType (1 << (type - 1)), 0 = any type
        uint32 mechanicMask = 0;        // 1 << (mechanic - 1): what an immune creature resists
        uint8 school = 0;
        float maxRange = 0.0f;          // single target: spell range
        float radius = 0.0f;            // > 0: every enemy within this of the caster (Psychic Scream)
        uint32 castMs = 0;
        float durationSec = 0.0f;       // full effect duration
        uint32 cooldownMs = 0;
        uint32 manaCost = 0;            // 0 for rage / energy abilities
        bool breaksOnDamage = false;    // AURA_INTERRUPT_DAMAGE_CANCELS: polymorph, sap, sleep
        bool fear = false;              // the target runs around (SPELL_AURA_MOD_FEAR)
        bool needsStealth = false;      // Sap
        bool peacefulOnly = false;      // target must not be in combat (Sap)
        // One target per caster (SpellEntry::HasSingleTargetAura: Shackle Undead, Polymorph,
        // Banish, Fear): casting it on another enemy frees `holding` (Unit::AddSpellAuraHolder).
        bool oneTarget = false;
        // oneTarget: the enemy this bot holds with it now (Unit::GetSingleCastSpellTargets), or
        // the one its cast is on its way to (cast done, aura not on yet: that one is held next).
        ObjectGuid holding;
        // Off cooldown, enough power, reagents, right form, can act and cast. Target checks
        // (creature type, immunity, range, line of sight) are the user's. A spell that needs
        // stealth (Sap) is also ready out of combat when the bot can go into stealth now
        // (ClassKit::Stealth): the Sap controller stealths on its way (CrowdControlBehavior).
        bool ready = false;

        // Doctrine doc .kiro/docs/2026-11-09-doctrine 3.4.
        ControlKind Kind() const { return KindOfControl(breaksOnDamage, fear); }
    };

    // A taunt a tank bot has learned (CombatBotBaseAI::m_spellListTaunt, one entry per spell):
    // what team rescue can give it (Perception/Values/RescueValue.h).
    struct TauntAbility
    {
        uint32 spellId = 0;
        float maxRange = 0.0f;
        uint32 cooldownMs = 0;          // its cooldown (Taunt: the 10 s category cooldown)
        // Without SPELL_EFFECT_ATTACK_ME (Mocking Blow) the threat it lends is taken back when
        // its aura fades: seconds it holds. 0 = the threat stays (RescueValue::TemporaryTauntSec).
        float holdSec = 0.0f;
        bool stanceSwap = false;        // needs a warrior stance swap first (TeamBotAI::StanceSwapFor)
        // On the global cooldown (StartRecoveryCategory): Mocking Blow, the area taunts. Taunt
        // and Growl are not: they cost the tank no action.
        bool gcd = false;
        // Off cooldown and enough power, after the stance swap if one is needed
        // (TeamBotAI::IsReadyWithStance). Range and sight are per enemy.
        bool ready = false;
    };

    // An attack power debuff a tank bot keeps on the enemies around it (ClassKit::
    // AttackPowerDebuffs: Demoralizing Shout / Roar), numbers from the spell data. Valued by
    // Perception/Values/MitigationValue.h, cast by MitigationBehavior.
    struct MitigationAbility
    {
        uint32 spellId = 0;
        float reduction = 0.0f;         // attack power taken off each enemy (positive)
        float durationSec = 0.0f;
        float radius = 0.0f;            // around the caster
        uint32 rageCost = 0;            // power units (10 per rage point), with the caster's talents
        bool stanceSwap = false;        // needs a warrior stance swap first (TeamBotAI::StanceSwapFor)
        bool ready = false;             // off cooldown, enough power (TeamBotAI::IsReadyWithStance), can act
    };

    // A spell on cooldown that its reset makes castable again (ClassKit::CooldownReset: Cold Snap
    // before Ice Block, Preparation before Vanish / Evasion). spellId 0 = the spell is ready by
    // itself (or no reset). The option's value pays the reset too: its cooldown points and, when
    // it starts the global cooldown (Preparation), one action.
    struct CooldownResetFacts
    {
        uint32 spellId = 0;
        uint32 cooldownMs = 0;
        bool gcd = false;
    };

    // A non-tank bot's own threat drop (ClassKit::ThreatDrops), one entry per spell:
    //   wipe      off every threat list for good (Vanish, Feign Death); drop, durationSec unused
    //   onTarget  `drop` off its target's list for good (Feint, Cower: SPELL_EFFECT_THREAT < 0)
    //   otherwise `drop` off every list for durationSec (Fade: SPELL_AURA_MOD_TOTAL_THREAT)
    struct ThreatDropAbility
    {
        uint32 spellId = 0;
        float drop = 0.0f;              // threat taken off (onTarget: after the caster's threat modifiers)
        float durationSec = 0.0f;       // 0 = for good
        // Takes it off every threat list for good (SPELL_EFFECT_SANCTUARY: Vanish, the
        // SPELL_AURA_FEIGN_DEATH aura: Unit::SetFeignDeath). An enemy with nobody else on its
        // list leaves the fight.
        bool wipe = false;
        bool onTarget = false;
        float maxRange = 0.0f;          // onTarget: yards to the enemy's edge
        uint32 manaCost = 0;
        uint32 cooldownMs = 0;
        // Off cooldown (or its reset ready: `reset`), enough mana, can act and cast.
        bool ready = false;
        CooldownResetFacts reset;
    };

    // A bot spell that keeps a member about to die alive (ClassKit::Survival, team rescue).
    // Effect kinds: SurvivalAbility::Effect = ClassKit's SurvivalEffect (same order).
    struct SurvivalAbility
    {
        enum class Effect : uint8 { Absorb, Reduce, Immune, ImmunePhysical, Heal, ExtraHealth, DropThreat, ReducePhysical };

        uint32 spellId = 0;
        Effect effect = Effect::Absorb;
        // Absorb: health absorbed. Reduce: damage taken factor (Shield Wall 0.25). Heal: health
        // healed (Lay on Hands: the caster's maximum health). ExtraHealth: health added.
        // ReducePhysical: melee damage taken factor (Evasion 0.5).
        float amount = 0.0f;
        float durationSec = 0.0f;
        float maxRange = 0.0f;
        uint32 cooldownMs = 0;
        uint32 manaCost = 0;            // Lay on Hands: all the caster's mana
        uint32 mechanic = 0;            // a target immune to it cannot get it (Weakened Soul, Forbearance)
        bool selfOnly = false;
        bool notOnTank = false;
        bool casterStops = false;
        bool targetStops = false;
        // Starts the global cooldown (the rescue pays an action for it); potions and
        // healthstones do not.
        bool gcd = true;
        // 0 = a spell; else the item whose on-use spell `spellId` is (ItemKit: healthstone,
        // healing potion), used with BotActionType::UseItem.
        uint32 itemId = 0;
        uint32 itemCategory = 0;        // the item's shared cooldown (ItemKit::Consumable::category)
        // Off cooldown (or its reset ready, the caster able to act: `reset`), enough power
        // (stance swap counted), can act (or controlled and the spell lifts it: Divine Shield,
        // Ice Block), a shield if needed.
        bool ready = false;
        CooldownResetFacts reset;
        bool renewWhileHit = false;     // SurvivalSpell::renewWhileHit (Evasion)
    };

    // A consumable of a bot that restores its own mana (ItemKit::Use::Mana: mana potions, mana
    // gems, Demonic / Dark Rune), the best held one per shared cooldown. ManaBudgetBehavior.
    struct ManaItemAbility
    {
        uint32 itemId = 0;
        uint32 spellId = 0;
        float mana = 0.0f;          // average restored
        float healthCost = 0.0f;    // average damage it does to the user (runes)
        uint32 cooldownMs = 0;
        uint32 category = 0;        // shared cooldown (ItemKit::Consumable::category)
        bool ready = false;         // can act, off cooldown, usable by it
    };

    // A damage cooldown of a bot (ClassKit::Bursts, RaceKit::Bursts), with the numbers of its
    // data; Perception/Values/BurstValue.h values it. Fields as ClassKit::BurstSpell.
    struct BurstAbility
    {
        uint32 spellId = 0;
        uint32 thenSpellId = 0;         // a pairing: the cast it boosts
        float damageMult = 0.0f;
        float extraSec = 0.0f;
        float durationSec = 0.0f;
        float damageTakenUp = 0.0f;
        uint32 cooldownMs = 0;
        bool gcd = false;               // starts the global cooldown (StartRecoveryTime)
        uint8 minEnemiesNear = 0;
        uint8 minAttackersOnSelf = 0;
        float minManaPct = 0.0f;
        bool notTank = false;
        bool healTakenCut = false;
        bool needsPet = false;
        bool needsCombo = false;
        bool thenNotOnTarget = false;
        // Off cooldown, enough power (stance swap counted), can act and cast.
        bool ready = false;
    };

    // A cooldown a bot casts on a party member (ClassKit::AllyBoosts); Perception/Values/
    // BoostValue.h values it per target. kind = ClassKit's AllyBoostKind (Mana, Power, PartyMana).
    struct AllyBoostAbility
    {
        uint32 spellId = 0;
        uint8 kind = 0;
        float maxRange = 0.0f;
        float radius = 0.0f;            // PartyMana: around the caster
        float manaPerTarget = 0.0f;     // PartyMana
        float durationSec = 0.0f;
        uint32 cooldownMs = 0;
        uint32 manaCost = 0;
        bool gcd = false;
        bool ready = false;             // off cooldown, the mana, can act and cast
        // Cast after leaving its form (TeamBotAI::NeedsFormExit: a druid in bear or cat form):
        // BoostValue adds what the time out of the form costs (FormExitCost).
        bool leavesForm = false;
    };

    // A resurrection a bot may cast in combat (ClassKit::CombatResurrection: Rebirth).
    // spellId 0 = none.
    struct CombatResAbility
    {
        uint32 spellId = 0;
        float maxRange = 0.0f;
        uint32 castMs = 0;
        uint32 cooldownMs = 0;
        uint32 manaCost = 0;
        // Off cooldown, the mana and the reagent, can act, castable in its form, not silenced.
        bool ready = false;
    };

    // A dead party member whose body or corpse is on this map (the ghost may be elsewhere):
    // what a resurrection in combat needs (ResurrectAssigner).
    struct CorpseFacts
    {
        ObjectGuid guid;                // the member
        Position3 pos;                  // its body, or its corpse once released
        uint8 role = 0;                 // CombatBotRoles (bots: theirs; players: as PartyMemberFacts::role)
        bool isBot = false;
        bool released = false;          // a ghost: resurrected at its corpse
        bool resRequested = false;      // a resurrection waits for its answer
    };

    // A paladin blessing or party aura a bot has learned, best rank (ClassKit::Blessings /
    // PartyAuras). kind = BlessingKind or PaladinAuraKind (Perception/Profiles/PaladinBuffs.h).
    struct PaladinBuffAbility
    {
        uint32 spellId = 0;
        uint8 kind = 0;
        uint32 greaterSpellId = 0;  // blessings: its Greater Blessing, 0 = not learned
    };

    // A damage curse a warlock bot has learned, best rank (ClassKit::Curses). kind = CurseKind
    // (Perception/Profiles/WarlockCurses.h).
    struct CurseAbility
    {
        uint32 spellId = 0;
        uint8 kind = 0;
    };

    // A party totem a shaman bot has learned, best rank (ClassKit::Totems). kind = TotemKind
    // (Perception/Profiles/ShamanTotems.h).
    struct TotemAbility
    {
        uint32 spellId = 0;
        uint8 kind = 0;
    };

    // A damage curse on an enemy, anyone's (CurseKind).
    struct CurseOnFacts
    {
        uint8 kind = 0;
        ObjectGuid caster;
        uint32 remainingMs = 0;
    };

    // A paladin blessing on a party member, anyone's (BlessingKind).
    struct BlessingOnFacts
    {
        uint8 kind = 0;
        ObjectGuid caster;
    };

    // The control effect an enemy is under now (the one lasting longest). A party member's
    // control cast that is done and on its way (Spell in SPELL_STATE_DELAYED: the server lands
    // it a few hundred ms later) counts as on, at its full duration: seeing nothing there
    // recast it (strat-20261108-140533, Shackle Undead cast twice in a row twice).
    struct ActiveControl
    {
        uint32 spellId = 0;             // 0 = none
        ObjectGuid caster;
        uint32 remainingMs = 0;
        bool landing = false;           // the cast on its way, not on yet
        bool breaksOnDamage = false;
        bool fear = false;
    };

    // A stun, root or slow a bot can put on an enemy running away, or a run speed boost on
    // itself to catch it (Sprint, Dash: user 2026-11-15) (ClassKit::FleeStoppers, flee
    // interception todo 13). Read from the spell data.
    struct FleeStopAbility
    {
        uint32 spellId = 0;
        float holdSec = 0.0f;           // stun or root: seconds it stands still (0 = a slow)
        float slowPct = 0.0f;           // slow: speed taken off, 0..1
        // Catch-up (SPELL_AURA_MOD_INCREASE_SPEED on itself): run speed added (0.7 = +70%)
        // for speedSec; the bot keeps hitting the runner once it is back in melee reach.
        float speedPct = 0.0f;
        float speedSec = 0.0f;
        float maxRange = 0.0f;          // yards (melee abilities: 5)
        float radius = 0.0f;            // around the caster (Frost Nova, Piercing Howl), 0 = on a target
        uint32 castMs = 0;
        uint32 cooldownMs = 0;
        uint32 manaCost = 0;            // 0 for rage / energy abilities
        bool needsCombo = false;        // a finisher (Kidney Shot): combo points on the target
        bool ready = false;             // off cooldown, enough power, the right form, can act
    };

    // An idle creature that may join if an enemy runs away (FleeFacts::joiners).
    struct FleeJoiner
    {
        ObjectGuid guid;
        // The run reaches it when it covers this many yards (its caller's distance - the call
        // radius; group members and helpers take their caller's).
        float needYd = 0.0f;
        float health = 0.0f;
        float dps = 0.0f;               // on the main tank, after armor (FightLoad::Foe)
        // Where its caller stands (the creature the runner calls; for the caller itself its own
        // place): the fight spot judges a run starting elsewhere from it (straight line,
        // FleePredictor::JoinPoints).
        Position3 callerPos;
    };

    // A creature of the planned pull that runs for help at low health (FleePredictor::
    // AnnotatePull, before the fight; WorldModel::pullRunners): who it may bring, as FleeFacts
    // for an enemy in the fight. Callers are searched from where it stands now, FIGHT_SPOT_YD
    // farther than its run (the fight spot may take the pile that far).
    struct PullRunnerFacts
    {
        ObjectGuid guid;
        Position3 pos;
        float reachYd = 0.0f;           // the run with the party on it (FleeFacts::focusedReachYd)
        float arriveSec = 0.0f;         // when its joiners come, from the fight's start (worst: it is killed first)
        std::vector<FleeJoiner> joiners;
    };

    // Running away at low health (Perception/Forecast/FleePredictor.h fills it; EnemyFacts::flee).
    struct FleeFacts
    {
        float atPct = 0.0f;             // flees at this health percent (CreatureProfile), 0 = never
        bool fleeing = false;           // running now (not a fear)
        bool spent = false;             // already ran and came back: once per life (until it evades)
        bool prevented = false;         // an aura stops it (Curse of Recklessness, Judgement of Justice)
        float secToFlee = 0.0f;         // until it reaches atPct at its share of the party's damage, 0 = there
        // Filled while it is fleeing or within FleePredictor::WINDOW_PCT above atPct, not
        // prevented: the idle creatures that may join if it runs (worst case: every one it can
        // reach, with their packs) and what that costs the party, in points.
        bool predicted = false;
        std::vector<FleeJoiner> joiners;
        float runSpeed = 0.0f;          // yards per second
        float reachYd = 0.0f;           // how far the whole run gets
        float points = 0.0f;            // the whole run (nobody kills it while it runs)
        // The party on it when it starts to run: seconds until it dies (melee lose it at once,
        // ranged keep hitting), how far it gets by then, and what that run brings in.
        float runKillSec = 0.0f;
        float focusedReachYd = 0.0f;
        float focusedPoints = 0.0f;

        // What having the party on it before it runs saves.
        float FocusSaves() const { return std::max(0.0f, points - focusedPoints); }
    };

    // A harmful, dispellable aura a creature put on a party member (players only).
    struct DebuffFacts
    {
        uint32 spellId = 0;
        uint8 dispelType = 0;        // SpellEntry::Dispel
        ObjectGuid caster;
        uint32 casterLevel = 0;
        uint32 remainingMs = 0;
        uint32 stacks = 1;
        // The caster is alive, hitting the member and its profile puts the spell back: seconds
        // until it does (on-hit proc: attack time / chance; spell list: repeat delay). 0 = not.
        float reapplySec = 0.0f;
    };

    // A bot kept trying one spell on one target and the cast never went out (BotDirectives
    // CastFailure: range, line of sight, power, form). Bots only.
    struct CastFailureFacts
    {
        ObjectGuid target;
        uint32 spellId = 0;
        uint32 count = 0;
        uint32 failingMs = 0;       // first to last failure of the run
    };
    // A run this long (longer than a global cooldown, which also makes a cast fail) and at least
    // this many tries: the bot cannot carry out that cast for now.
    static constexpr uint32 CAST_FAILING_MS = 1600;
    static constexpr uint32 CAST_FAILING_TRIES = 3;

    // One direct heal of a healer bot, chain cast (Perception/Forecast/HealCapacity.h).
    struct HealRate
    {
        float hps = 0.0f;           // healing per second: heal (crits included) / max(cast, GCD)
        float perMana = 0.0f;       // healing per mana
        float castSec = 0.0f;       // until the first one lands
    };

    // One ClassKit::AreaDamage spell of a damage dealer bot, with its numbers on the bot's
    // victim (out of combat: on itself). Perception/Values/AreaValue.h values it.
    struct AreaSpellFacts
    {
        uint32 spellId = 0;
        bool onTarget = false;      // ground spell centred on an enemy (Blizzard, Rain of Fire), else around the caster
        bool channel = false;
        uint32 ticks = 1;           // a channel's ticks (Blizzard 8), else 1
        float periodSec = 0.0f;     // between ticks; anything else: its busy time
        float perTick = 0.0f;       // damage on each target per tick (spell power included)
        float tickThreat = 0.0f;    // threat of one tick on one target (spell multiplier, caster modifiers)
        float mana = 0.0f;          // the whole cast
        float radius = 0.0f;
        float range = 0.0f;         // onTarget: how far the centre may be
        float slowPct = 0.0f;       // ClassKit::AreaDamageSpell::slowPct (Improved Blizzard)
        bool ready = false;         // off cooldown, the mana, can act and cast
    };

    // The bot's own area channel running now (Perception/Values/AreaValue.h continues it).
    struct AreaChannelFacts
    {
        uint32 spellId = 0;         // 0 = none
        Position3 centre;           // its destination (around the caster: the caster)
        uint32 ticksLeft = 0;
    };

    // A shield the bot puts on itself before an area spell (ClassKit::SelfShield: Ice Barrier).
    struct SelfShieldFacts
    {
        uint32 spellId = 0;         // 0 = none
        float absorb = 0.0f;
        uint32 manaCost = 0;
        bool ready = false;
        bool up = false;            // its aura is on
        // Seconds until its cooldown ends (SpellCaster::GetSpellCooldownLeft; 0 = off cooldown,
        // negative = held until an event): the area phase may wait for it.
        float cooldownSec = 0.0f;
    };

    // A member's direct damage spell on one unit that has not hit yet (PartyMemberFacts::pendingHit).
    struct PendingHit
    {
        ObjectGuid target;          // empty = none
        uint32 spellId = 0;
        uint32 landMs = 0;          // from WorldModel::refreshedAtMs until it hits
        float damage = 0.0f;        // DamageSpells::EstimateDirect (no crit, no resistance)
        // Threat it adds on the target when it hits (the spell's threat multiplier and the
        // caster's modifiers: Unit::ApplyTotalThreatModifier). Not on the list before then.
        float threat = 0.0f;
    };

    // A member's single target direct heal being cast (PartyMemberFacts::pendingHeal):
    // SurvivalValue::Of counts it on its target when it lands.
    struct PendingHeal
    {
        ObjectGuid target;          // empty = none (may be the caster itself)
        uint32 spellId = 0;
        uint32 landMs = 0;          // from WorldModel::refreshedAtMs until it lands
        float amount = 0.0f;        // HealSpells::Estimate direct part (no crit)
    };

    // A party member's (or pet's) direct damage on one enemy over the last few seconds, periodic
    // ticks left out (Perception/Forecast/DamageDealt.h, EnemyFacts::dealtBy).
    struct DealtRate
    {
        ObjectGuid source;
        float dps = 0.0f;
    };

    // One periodic damage effect a party member or pet put on an enemy (EnemyFacts::partyDots).
    struct PartyDot
    {
        ObjectGuid caster;
        uint32 spellId = 0;
        float perTick = 0.0f;       // Modifier::m_amount (bonuses included at apply)
        uint32 periodMs = 0;
        uint32 nextTickMs = 0;      // from the refresh
        uint32 remainingMs = 0;
    };

    // One periodic heal on a party member, anyone's (PartyMemberFacts::hots).
    struct PartyHot
    {
        ObjectGuid caster;
        uint32 spellId = 0;
        float perTick = 0.0f;       // Modifier::m_amount (the caster's bonuses included at apply)
        uint32 periodMs = 0;
        uint32 nextTickMs = 0;      // from the refresh
        uint32 remainingMs = 0;

        uint32 TicksLeft() const
        {
            if (!periodMs || nextTickMs > remainingMs)
                return 0;
            return 1 + (remainingMs - nextTickMs) / periodMs;
        }
        float PerSec() const { return periodMs ? perTick * 1000.0f / float(periodMs) : 0.0f; }
    };

    // A party member or a party member's pet on this map.
    struct PartyMemberFacts
    {
        ObjectGuid guid;
        ObjectGuid ownerGuid;       // pets: the owning member
        bool isBot = false;         // controlled by TeamBotAI
        bool isPet = false;
        // Can hold an enemy for the party besides the main tank (an off-tank, the doctrine's
        // OffTank.h): a bot's pet with a threat ability it uses on its own target (Growl,
        // Torment). No taunt, no area threat: what it can do is in its other facts (survival:
        // Shell Shield, threat drops: Cower, health, armor), the same as for a tank.
        bool canOffTank = false;
        bool alive = false;
        // CombatBotRoles. Bots: TeamBotAI::m_role. Players: the role they chose, else from
        // RoleCapability (tank only while no bot tanks), see Perception/Tanks.h. Pets: 0.
        uint8 role = 0;
        bool roleLocked = false;    // chosen by the user (bot or player); the "roles" assigner leaves it
        // TeamFunction bits (CommanderTypes.h): RoleFunctions(role), the role now (a stand-in's
        // too), plus what TeamPlan::coverage added because nobody else in the team does it (the
        // last RoleAssigner result: changes between fights only). Pets: 0. Read HealsTeam and
        // DealsDamage, not the role, to count who heals and who deals damage.
        uint8 functions = 0;
        RoleCapability cap;         // what this member can do (players and bots, not pets)
        uint8 classId = 0;
        float healthPct = 0.0f;
        uint32 health = 0;
        uint32 maxHealth = 0;
        uint32 level = 0;
        uint32 armor = 0;
        // Melee avoidance as the server rolls it (SpellCaster::RollMeleeOutcomeAgainst), before
        // the attacker's level (Value::MeleeAvoidance): dodge and parry percent (players: their
        // fields, parry only with CanParry and a weapon; creatures: 5 + auras) and the defense
        // skill for the miss chance. Not zeroed while casting or stunned (Unit::GetUnitDodgeChance
        // does): the forecast wants the steady value.
        float dodgePct = 0.0f;
        float parryPct = 0.0f;
        uint32 defenseSkill = 0;
        float manaPct = -1.0f;      // -1 = no mana, or mana is not the active power (druid in bear / cat form)
        // Mana pool, also while shapeshifted into a form without mana. maxMana 0 = no mana pool.
        uint32 mana = 0;
        uint32 maxMana = 0;
        Position3 pos;
        // Where it will stand: the end of the walk it is on (its movement spline), else `pos`.
        // The spot it holds for the others (FieldSpots: a cell another member stands on or walks
        // to is taken last among equally good ones, position field doc section 6).
        Position3 standAt;
        ObjectGuid victim;          // who this member is attacking
        ObjectGuid selection;       // players (bots too): what is selected
        // Bots: the first live enemy carrying one of the raid marks the player told this bot to
        // focus (`.partybot focusmark`, TeamBotAI::m_marksToFocus). Empty = none. A player
        // order for TargetAssigner.
        ObjectGuid focusMarkEnemy;
        bool casting = false;       // busy with a cast or channel (cannot react this tick)
        uint32 castRemainingMs = 0; // of that cast or channel
        // Its direct damage spell on one unit that has not hit yet: the cast in progress (not a
        // channel), or the bolt in the air. landMs = cast left + flight over the distance now.
        // Perception/Forecast/FinishForecast.h counts it for the target's other attackers.
        PendingHit pendingHit;
        // Its single target direct heal being cast (players too): SurvivalValue::Of counts it.
        PendingHeal pendingHeal;
        // Bots whose ClassKit::Filler has a cast time: that cast time and its flight speed
        // (yards per second, 0 = hits at once). FinishForecast::FillerLandMs.
        uint32 fillerCastMs = 0;
        float fillerSpeed = 0.0f;
        // Combo points (rogue, cat druid) and the enemy they are on: lost when it switches target.
        uint8 comboPoints = 0;
        ObjectGuid comboTarget;
        bool resting = false;       // eating, drinking or sitting
        bool inCombat = false;
        // Cannot act: stunned, asleep, feared, confused, polymorphed, or charmed by an enemy.
        // Assigners skip such members; rates do not count them as healers or damage dealers.
        bool controlled = false;
        bool charmed = false;       // mind controlled by an enemy: hostile to us, do not attack, dispel
        ObjectGuid charmer;         // who controls it (charmed)
        uint32 charmLeftMs = 0;     // the charm aura's time left (0 = unknown / permanent)
        bool silenced = false;      // can still melee, cannot cast
        // Set up to tank right now: warrior in Defensive Stance, druid in (Dire) Bear Form,
        // paladin with Righteous Fury. A state, not who the tanks are (that is `role`, Tanks.h).
        bool tankStance = false;
        std::vector<InterruptAbility> interrupts;   // bots only
        std::vector<DispelAbility> dispels;         // bots only
        std::vector<ControlAbility> controls;       // bots only
        std::vector<TauntAbility> taunts;           // tank-role bots only
        std::vector<MitigationAbility> mitigations; // tank-role bots only
        std::vector<ThreatDropAbility> threatDrops; // non-tank bots and bots' pets (Cower)
        std::vector<SurvivalAbility> survival;      // bots and bots' pets (Shell Shield)
        std::vector<ManaItemAbility> manaItems;     // bots with a mana pool only
        std::vector<BurstAbility> bursts;           // bots only
        std::vector<AllyBoostAbility> allyBoosts;   // bots only
        CombatResAbility combatRes;                 // bots only
        // Paladin bots: blessings and party auras it knows (BlessingAssigner).
        std::vector<PaladinBuffAbility> blessingSpells;
        std::vector<PaladinBuffAbility> auraSpells;
        // Paladin bots: seals it can judge for a debuff (kind = JudgementKind; JudgementAssigner).
        std::vector<PaladinBuffAbility> judgementSeals;
        // Warlock bots: the damage curses it knows (CurseAssigner).
        std::vector<CurseAbility> curseSpells;
        // Shaman bots: the party totems it knows (kind = TotemKind; TotemAssigner).
        std::vector<TotemAbility> totemSpells;
        // Every member (players, not pets): paladin blessings on it. Paladins: the party aura it
        // keeps up itself (PaladinAuraKind, 0 = none): a player paladin's is left to it.
        std::vector<BlessingOnFacts> blessingsOn;
        uint8 ownAura = 0;
        // Every member (BoostAssigner): its mana regeneration is boosted now
        // (SPELL_AURA_MOD_POWER_REGEN_PERCENT: Innervate), its spells are (Power Infusion,
        // Arcane Power: they do not stack).
        bool manaBoosted = false;
        bool powerBoosted = false;
        std::vector<FleeStopAbility> fleeStops;     // bots only
        // Every member (team rescue): absorbs still up (school absorb auras; Mana Shield as far
        // as the mana lasts), damage taken factor of its auras (Shield Wall 0.25), immunities,
        // and mechanics it is immune to as 1 << mechanic (Weakened Soul: MECHANIC_SHIELD,
        // Forbearance: MECHANIC_IMMUNE_SHIELD).
        float absorb = 0.0f;
        float damageTakenMult = 1.0f;
        bool immunePhysical = false;
        bool immuneMagic = false;
        uint32 immuneMechanics = 0;
        // Every member: periodic heals on it, anyone's (the heal module: what still heals it,
        // whether the healer's own HoT is up, what Swiftmend would eat).
        std::vector<PartyHot> hots;
        // Pets: casts a taunt by itself (Growl, Torment, Suffering on autocast). An enemy on it
        // is growled back, so the tank does not take it (Perception/Profiles/PetTaunt.h).
        bool tauntsItself = false;
        // Damage dealer bots with ClassKit::AreaDamage spells, on their victim (out of combat:
        // on itself): the best area
        // spell's damage per second on each target it hits, its radius, and the filler's
        // damage per second (what the area spell has to beat). 0 = none. For the area damage
        // a crowd control gives up before a pull (ControlValue::OfPull; in combat the area
        // spells are valued one by one, `areaSpells`).
        float areaDpsPerTarget = 0.0f;
        float areaRadius = 0.0f;
        float fillerDps = 0.0f;
        // Area phase (Perception/Values/AreaValue.h, Coordination/Doctrine/AreaPhase.h): the bot's area spells,
        // its own area channel running now, its bolt in the air (cast done, not landed: its
        // threat is not on the list yet) and the shield it can put up first.
        std::vector<AreaSpellFacts> areaSpells;
        AreaChannelFacts areaChannel;
        ObjectGuid boltTarget;
        float boltThreat = 0.0f;
        SelfShieldFacts selfShield;
        // Bots with Life Tap (warlocks): mana it gives per health spent and per cast. Their mana
        // is worth at most the health and time it takes to tap it again (ManaWorth.h).
        float tapManaPerHealth = 0.0f;
        float tapManaPerCast = 0.0f;
        // Bots whose filler (ClassKit::Filler) costs mana: mana per second while casting it
        // (cost / max(cast time, GCD)). What spent mana is worth to a damage dealer (ManaWorth.h).
        float fillerManaPerSec = 0.0f;
        // Damage per second this member deals on its own, estimated from its gear and spells
        // (WorldModelBuilder: weapon damage fields x Value::MELEE_ABILITY_FACTOR, a bot's
        // ClassKit::Filler x Value::CASTER_UPTIME, the larger). Value::DealtDps uses it before
        // a fight of this party was measured. 0 = unknown (pets, healer bots).
        float soloDps = 0.0f;
        // Bots: threat per point of physical damage (Unit::ApplyTotalThreatModifier: stance,
        // Defiance, Righteous Fury...). Perception/Forecast/ThreatGap.h (a tank's rate before it was measured).
        float threatMod = 1.0f;
        // Healer-role bots: threat their biggest direct heal adds, before it is split over the
        // enemies fighting the one healed (heal x 0.5, paladins 0.25, x its threat modifiers:
        // Spell::DoAllEffectOnTarget heal -> HostileRefManager::threatAssist). Full heal, no
        // overheal: the worst case. Perception/Forecast/ThreatGap.h.
        float healThreat = 0.0f;
        // Bots with a mana pool: mana per second their drink restores (TeamBotAI::DrinkManaPerSec),
        // on top of manaRegen. Perception/Values/RestValue.h.
        float drinkManaPerSec = 0.0f;
        // Bots: the mana share a new drink starts below, by what their last fights here cost
        // (TeamBotAI::DrinkStartShare, RestValue::DrinkStartShare).
        float drinkStartShare = 0.0f;
        float foodHealthPerSec = 0.0f;  // bots: health per second their food restores
        bool drinking = false;      // bots: the drink is on (TeamBotAI::IsDrinking)
        bool eating = false;        // bots: the food is on (TeamBotAI::IsEating)
        // Bots: Shoot is learned and a wand that is not broken is equipped (TeamBotAI::
        // CanShootWand). A healer's free attack (healer damage tiers doc 2026-11-21).
        bool canShootWand = false;
        // Bots: the healer damage tier the user set (`.partybot healdps`, TeamBotAI::
        // m_healerDamageTier), -1 = automatic (Coordination/Doctrine/HealerDamage.h).
        int8 healerDamageSet = -1;
        std::vector<DebuffFacts> debuffs;           // players only
        // Harmful auras of any caster on it (players only), what Stoneform removes: a poison,
        // a disease (SpellEntry::Dispel), a bleed (MECHANIC_BLEED on the spell or an effect).
        bool poisoned = false;
        bool diseased = false;
        bool bleeding = false;
        // Damage actually taken (Perception/Forecast/DamageIntake.h): per second over the last few
        // seconds, and the biggest spike within a short window. Value::TakenDps combines the
        // rate with the prediction from enemy melee.
        float takenDps = 0.0f;
        float takenBurst = 0.0f;
        // Players: mana regenerated per second outside / inside the five second rule (world
        // rate included), and ms left in the rule (0 = outside, spirit regeneration runs).
        float manaRegen = 0.0f;
        float manaRegenCasting = 0.0f;
        uint32 fiveSecondRuleMs = 0;
        // Healer-role bots: their direct heals with their real numbers (+healing gear,
        // talents, crit), valued on themselves (Perception/Forecast/HealCapacity.h). Empty otherwise.
        std::vector<HealRate> healRates;
        // The same for members that can heal but are not healer bots (RoleCapability::heal at
        // least MIN_SIDE_HEAL): hybrid bots in another role, players (their known spells). What they
        // could heal if they had to (FightLoad::Lineup, the contingency check). Kept apart so
        // nothing that counts the healers counts them.
        std::vector<HealRate> spareHealRates;
        // Every member (wipe protection, Coordination/Situation/Crisis/WipeAssigner.h):
        //   selfRes     it can rise by itself after dying: alive, Player::SelectResurrectionSpellId
        //               (a Soulstone Resurrection on it, Reincarnation ready with an Ankh); dead,
        //               the self resurrection kept on its body (PLAYER_SELF_RES_SPELL).
        //   resurrects  it can resurrect others out of combat (bots: their resurrection spell;
        //               players: priests from level 10, paladins and shamans from 12).
        // soulstoneItem: warlock bots, a Soulstone in the bags whose use is ready (item entry).
        bool selfRes = false;
        bool resurrects = false;
        uint32 soulstoneItem = 0;
        // Dead with selfRes: the health and mana it rises with (Spell::EffectSelfResurrect: the
        // Soulstone's flat amounts, Reincarnation's percent). A dead healer bot with selfRes has
        // its heals in spareHealRates (FightLoad::Lineup::risen).
        uint32 riseHealth = 0;
        uint32 riseMana = 0;
        // Bots: casts the arbiter tried and could not execute, recently (CastFailureFacts).
        std::vector<CastFailureFacts> castFailures;

        bool CanAct() const { return alive && !controlled; }
        // A bot that heals the team: a healer, or given the heal function (nobody else heals).
        // Players are not counted (their heals are a guess: spareHealRates).
        bool HealsTeam() const { return isBot && !isPet && (functions & FUNCTION_HEAL); }
        // Deals damage for the team: players always, bots with the damage function (every role
        // but healer; a healer when nobody else deals damage).
        bool DealsDamage() const { return !isPet && (!isBot || (functions & FUNCTION_DAMAGE)); }
        bool UsesMana() const { return maxMana > 0; }
        // Keeps failing to cast `spellId` on `target` (CAST_FAILING_MS, CAST_FAILING_TRIES).
        bool IsFailingCast(uint32 spellId, ObjectGuid target) const
        {
            for (CastFailureFacts const& f : castFailures)
                if (f.spellId == spellId && f.target == target)
                    return f.count >= CAST_FAILING_TRIES && f.failingMs >= CAST_FAILING_MS;
            return false;
        }
    };

    // One row of an enemy's threat list, plus party members not on it yet (onList = false).
    struct ThreatEntry
    {
        ObjectGuid guid;
        float threat = 0.0f;
        bool onList = false;
        bool partySide = false;     // party member or pet of this party
        bool inMeleeReach = false;  // enemy can hit this unit in melee -> 110% rule instead of 130%
        float threatPerSec = 0.0f;  // over the last few seconds (ThreatHistory); includes taunt jumps
    };

    // One creature_spells row of an enemy with its live timer (CreatureAI::GetSpellsListTimers),
    // EnemyFacts::spellList. Perception/Forecast/IncomingTimeline.h turns it into hits.
    struct ListSpellFacts
    {
        uint32 spellId = 0;
        uint32 cooldownMs = 0;      // left at the last list check (0 = ready, fires at a check)
        uint32 repeatMinMs = 0;
        uint32 repeatMaxMs = 0;
        uint8 probability = 100;
        uint8 castTarget = 0;       // ScriptTarget (TARGET_T_*)
        uint16 castFlags = 0;       // CF_*
    };

    // A low health boost an EventAI creature casts on itself (Enrage 8269 / 8599: a
    // CreatureProfile spell with healthPct whose auras on the caster raise its melee), not on it
    // yet (EnemyFacts::enrages). IncomingTimeline boosts its swings from the moment its health
    // reaches atHealthPct. Once up, the live attack time and damage fields carry it.
    struct EnrageFacts
    {
        uint32 spellId = 0;
        float atHealthPct = 0.0f;
        float periodMult = 1.0f;    // swing period x, SPELL_AURA_MOD_MELEE_HASTE: 1 / (1 + amount%)
        float damageMult = 1.0f;    // SPELL_AURA_MOD_DAMAGE_PERCENT_DONE (physical)
        float damageFlat = 0.0f;    // SPELL_AURA_MOD_DAMAGE_DONE (physical), per swing
    };

    // When an enemy last started casting a spell (EnemyFacts::castStarts).
    struct CastStart
    {
        uint32 spellId = 0;
        uint32 lastStartMs = 0;         // WorldTimer ms the cast began (seen elapsed time subtracted)
    };

    struct EnemyFacts
    {
        ObjectGuid guid;
        uint32 entry = 0;
        uint32 level = 0;
        bool elite = false;
        // CreatureProfile::boss (world boss rank, boss_ script, an EventAI encounter): the long
        // cooldowns kept for big fights may go here (cooldown doc .kiro/docs/2026-11-14-cooldowns.md).
        bool boss = false;
        float healthPct = 0.0f;
        uint32 health = 0;
        uint32 maxHealth = 0;
        float meleeHit = 0.0f;          // average main hand hit now (live unit fields, enrage included)
        float meleeDps = 0.0f;          // meleeHit / attack time, before the victim's armor
        // Swing timers (Unit::GetAttackTimer: ms to the next swing, 0 = swings as soon as it can)
        // and periods, weapon damage range now; off hand only with HaveOffhandWeapon (period 0
        // = none). A swing waits while a cast or channel runs (Unit::UpdateMeleeAttackingState).
        uint32 swingInMs = 0;
        uint32 swingPeriodMs = 0;
        float meleeMin = 0.0f;
        float meleeMax = 0.0f;
        uint32 offSwingInMs = 0;
        uint32 offSwingPeriodMs = 0;
        float offMin = 0.0f;
        float offMax = 0.0f;
        // Crushing blows (SpellCaster::RollMeleeOutcomeAgainst): possible at all (not a pet, no
        // CREATURE_STATIC_FLAG_2_NO_CRUSHING_BLOWS), every hit that is not avoided or a crit
        // (CREATURE_FLAG_EXTRA_ALWAYS_CRUSH).
        bool canCrush = false;
        bool alwaysCrush = false;
        // creature_spells rows with live timers, and ms until the list is checked next (every
        // 1.2 s, CreatureAI::UpdateSpellsList). Empty when the AI does not run the list (C++
        // boss scripts: their own timers are not readable).
        std::vector<ListSpellFacts> spellList;
        uint32 spellCheckInMs = 0;
        std::vector<EnrageFacts> enrages;   // low health melee boosts still to come (EventAI)
        Position3 pos;
        // UNIT_FIELD_BOUNDINGRADIUS: area spells and melee reach start at its edge, not its centre.
        float boundingRadius = 0.0f;
        float speed = 0.0f;             // run speed now, yards per second (slows included)
        bool slowed = false;            // a speed decrease is on it
        bool rooted = false;            // UNIT_STATE_ROOT (Frost Nova): stands, still hits what is in reach

        ObjectGuid victim;              // threat manager's current victim (who it is hitting)
        float victimThreat = 0.0f;
        uint32 victimSinceMs = 0;       // WorldTimer ms when it started hitting `victim`
        uint32 castingSpellId = 0;
        ObjectGuid castTarget;          // unit target of the current cast, empty = none / area
        bool castChanneled = false;
        bool castInterruptible = false;
        uint8 castCategory = 0;         // CastCategory (SpellTags.h)
        uint32 castRemainingMs = 0;
        uint32 castTotalMs = 0;
        uint32 lastCastSpellId = 0;     // most recent interruptible cast seen this fight (kept after it ends)
        bool crowdControlled = false;   // cannot act (stun, fear, polymorph, ...)
        ActiveControl control;          // the control effect on it, if any (also from players)
        uint32 creatureTypeMask = 0;    // Unit::GetCreatureTypeMask: 1 << (CreatureType - 1)
        // Longest periodic damage a party member put on it, ms left (0 = none): it wakes a
        // control that breaks on damage at the next tick.
        uint32 partyDotMs = 0;
        bool ignoresAssistThreat = false; // heal/buff threat is zeroed (fear, confuse, breakable stun)
        // False while it cannot be attacked and does nothing: feign death phases (not selectable),
        // total immunity (divine shield), evading. Such enemies are left out of damage rates,
        // time-to-die shares, taunts and heal-threat losses.
        bool acting = true;
        bool meleeEnabled = true;       // AI melee on (walking bombs, casters standing back turn it off)
        bool moving = false;            // Unit::IsMoving (a caster between two casts stands still)
        bool patrol = false;            // walks a waypoint path out of combat (default movement)
        // A temporary aura that hurts or turns back whoever hits it (damage shield: Inferno Shell,
        // Shadow Shield; spell reflection): its time left, 0 = none. Permanent ones are not counted.
        uint32 reflectMs = 0;
        uint32 reflectSpellId = 0;
        // A member is taming it (a taming channel, SPELL_ATTR_EX2_SPECIAL_TAMING_FLAG: the hunter's
        // class quest taming rods, Tame Beast): it must live until the channel ends.
        bool beingTamed = false;
        bool meleePhysical = true;      // false: melee is fire/frost/... (armor does not reduce it)
        // Melee attack power now and the creature_classlevelstats one: the weapon damage scales
        // by 0.7 + 0.3 x apNow / apDb (Creature::UpdateDamagePhysical). apDb 0 = attack power
        // does not change its damage. Perception/Values/MitigationValue.h.
        float apNow = 0.0f;
        float apDb = 0.0f;
        // The strongest attack power debuff on it, anyone's (Demoralizing Shout / Roar do not
        // stack: SPELL_AURA_MOD_ATTACK_POWER below 0): attack power it takes off, ms left.
        float apDebuff = 0.0f;
        uint32 apDebuffMs = 0;
        uint32 armor = 0;               // live armor (Curse of Recklessness: CurseValue.h)
        // The damage curses on it, anyone's (Perception/Profiles/WarlockCurses.h).
        std::vector<CurseOnFacts> curses;
        // Main ranged spell damage per second (CreatureProfile::mainRangedSpellId), counted
        // while it stands back casting (melee off). Before resistances.
        float rangedDps = 0.0f;
        bool silenced = false;          // UNIT_FLAG_SILENCED: no casts, no interrupter needed
        // Mana now and the creature's base mana (GetCreateMana, for ManaCostPercentage).
        // maxMana 0 = no mana pool. A caster out of mana casts nothing that costs mana
        // (strat-20261101-224637: a Skeletal Guardian at 23 mana kept "casting" Arcane Explosion).
        uint32 mana = 0;
        uint32 maxMana = 0;
        uint32 createMana = 0;
        bool summoned = false;          // temporary summon or guardian (adds)
        ObjectGuid summoner;            // who summoned it, if known
        uint32 firstSeenMs = 0;         // WorldTimer ms of the first snapshot it was in (ThreatHistory)
        // WorldTimer ms of the latest snapshot in which it had lost health (ThreatHistory), 0 =
        // not hurt since it was first seen: nobody hits it (Value::EnemyDyingSec).
        uint32 lastHurtMs = 0;
        // Party members and pets that hit it lately, each with its damage per second
        // (Perception/Forecast/DamageDealt.h). Who is on it now is PartyMemberFacts::victim.
        std::vector<DealtRate> dealtBy;
        // Every periodic damage effect of ours on it (partyDotMs is the longest one's time left).
        std::vector<PartyDot> partyDots;
        uint32 rank = 0;                // creature_template.rank (also correct for guardians)
        // No party member or pet is on its threat list: it is here because it fights one of
        // world.allies (combat scenario 9: adds hitting the Zul'Farrak prisoners). Its threat
        // list then only has ally rows (partySide = false) plus our not-listed rows.
        bool onAllyOnly = false;
        FleeFacts flee;

        // Every spell (interruptible or not) seen cast by it since it joined our lists, with the
        // start of its latest cast. Creature repeat delays run from the cast start, so
        // refreshedAtMs - lastStartMs vs the repeat delay tells how soon it can come again.
        std::vector<CastStart> castStarts;
        // Bots with a ranged interrupt that have no line of sight to it, filled only while it
        // casts an interruptible spell; and tank bots with a taunt that have none, filled only
        // while it hits a party member who is not a tank (team rescue). Empty otherwise: line
        // of sight not checked.
        std::vector<ObjectGuid> outOfSight;

        // 0 = not seen casting `spellId`.
        uint32 LastCastStartMs(uint32 spellId) const
        {
            for (CastStart const& c : castStarts)
                if (c.spellId == spellId)
                    return c.lastStartMs;
            return 0;
        }
        bool InSightOf(ObjectGuid member) const
        {
            return std::find(outOfSight.begin(), outOfSight.end(), member) == outOfSight.end();
        }
        // crowdControlled, but the party keeps hitting it (flee interception, todo 13): it runs
        // away at low health (the server counts the run as "cannot react"), or a stun that
        // damage does not break holds a creature that flees (kill it before the stun ends).
        bool HitWhileControlled() const
        {
            return flee.fleeing ||
                   (flee.atPct > 0.0f && control.spellId && !control.breaksOnDamage && !control.fear);
        }

        // Sorted by threat, highest first. Every row of the live threat list (party members,
        // pets, allies and anyone else on it, partySide tells them apart), plus every party
        // member and pet on this map that is not on it yet (onList = false).
        std::vector<ThreatEntry> threat;
    };

    // A friendly creature near the party that is not in the party and is fighting: escorted
    // NPCs, freed prisoners (Zul'Farrak pyramid), guards. Filled by WorldModelBuilder.
    struct AllyFacts
    {
        ObjectGuid guid;
        uint32 entry = 0;
        uint32 level = 0;
        uint32 rank = 0;                // creature_template.rank
        bool elite = false;
        float healthPct = 0.0f;
        uint32 health = 0;
        uint32 maxHealth = 0;
        uint32 armor = 0;
        Position3 pos;
        ObjectGuid victim;              // who it is attacking
        // Holds enemies like a tank: damage dealers leave room under its threat and the tank does
        // not take its enemies (ThreatBehavior). Builder rule: elite and max health at least
        // ALLY_TANK_HEALTH_MULT x the party tank's (Sergeant Bly 18500 vs a level 45 tank ~4000).
        // Boss modules may overwrite it in AddFacts (the Zul'Farrak module keeps it on Bly only).
        bool tankLike = false;
    };

    enum class PackSource : uint8
    {
        Database,   // creature_groups
        Distance,   // no group record, linked by distance
        Mixed,      // a creature_groups pack plus ungrouped creatures next to it
    };

    // Creatures near the party that are not in combat.
    struct PackCreature
    {
        ObjectGuid guid;
        ObjectGuid dbGroupLeader;   // creature_groups leader, empty if none
        uint32 entry = 0;
        uint32 level = 0;
        bool elite = false;
        bool aggressive = false;    // hostile (red); false = neutral (yellow)
        bool stealthed = false;     // the tank cannot target it now (still joins the fight)
        bool patrol = false;        // walks a waypoint path; its pack membership changes as it moves
        bool tappedByOther = false; // somebody outside the party has loot rights
        // A player outside the party (or his group's bot) has it targeted close by and nobody
        // hit it yet: his to take (Shared/PlayerClaims.h). Bot teams with no player leave it.
        ObjectGuid claimedBy;
        bool boss = false;          // CreatureProfile::boss: pull and clear leave it to the player
        bool noAggro = false;       // CREATURE_FLAG_EXTRA_NO_AGGRO: never starts a fight
        // A member's quest still needs it (Profiles/QuestNeeds.h: kills, or a quest item it
        // drops). Only in a team with a real player (the pull stance reads it).
        bool questNeeded = false;
        // It will start a fight on its own: hostile, starts fights, visible, and a live member
        // is within its aggro range + a margin (LeaderActivity annotator, every refresh).
        bool comesAnyway = false;
        uint32 creatureTypeMask = 0;    // 1 << (CreatureType - 1)
        Position3 pos;
        float reach = 0.0f;             // aggro range against the main tank while fighting, else this bot
        uint32 health = 0;
        uint32 maxHealth = 0;
        float meleeDps = 0.0f;          // estimate before armor (CreatureProfiles::EstimateMeleeDps)
        // Its main ranged spell while it stands back casting (WorldModelBuilder::RangedSpellDps,
        // from the profile's melee estimate), 0 = none. Before resistances.
        float rangedDps = 0.0f;
        // A harmful aura that takes it out (SpellTags::IsDisableEffect) while it is not in
        // combat: a Sap before the pull (PullControl). 0 = none.
        uint32 controlSpellId = 0;
    };

    // A creature walking a waypoint path, not in combat (WorldModel::patrols).
    struct PatrolFacts : PackCreature
    {
        // Where it will be every trackStepSec from the pack refresh (PatrolTracker::Predict,
        // one lap ahead within WorldModelBuilder PATROL_TRACK_MIN_SEC..PATROL_TRACK_MAX_SEC;
        // track[0] = now). Always filled (travel, pull and fight: the position field's patrol
        // layer, PatrolAssigner, the pull's checks).
        float trackStepSec = 0.0f;
        std::vector<Position3> track;
    };

    // How the leader would walk to a pack (Perception/Spatial/PackRoutes.h).
    enum class PackRouteState : uint8
    {
        Unknown,    // not calculated (fight going on, too many packs, no leader)
        Walk,       // navmesh path: walkYd is its length
        Straight,   // no navmesh here: walkYd is the straight distance, `blocker` is not known
        NoWay,      // no complete navmesh path to it
    };

    struct PackRoute
    {
        PackRouteState state = PackRouteState::Unknown;
        float walkYd = 0.0f;
        ObjectGuid target;          // the member the walk goes to (nearest to the leader)
        // The first idle creature of another pack (or a patrol) the walk comes into the aggro
        // range of (SafeRoute::PathIsSafe), empty = none. That creature would join on the way.
        ObjectGuid blocker;
        // The leader has line of sight to `target` (Walk / Straight only).
        bool leaderSees = false;
    };

    struct PackFacts
    {
        PackSource source = PackSource::Distance;
        ObjectGuid dbGroupLeader;
        std::vector<PackCreature> members;
        Position3 center;
        float distanceToLeader = 0.0f;  // party leader to pack center (straight line)
        uint32 eliteCount = 0;
        uint32 maxLevel = 0;
        PackRoute route;

        // How far the party walks to it: the walk when known, else the straight distance,
        // infinity when there is no way.
        float ApproachYd() const
        {
            switch (route.state)
            {
                case PackRouteState::Walk:
                case PackRouteState::Straight:
                    return route.walkYd;
                case PackRouteState::NoWay:
                    return std::numeric_limits<float>::infinity();
                default:
                    return distanceToLeader;
            }
        }
    };

    // The walk of a melee or tank bot to an enemy (Perception/Spatial/MeleeRoutes.h).
    struct RouteFact
    {
        ObjectGuid bot;
        ObjectGuid enemy;
        // The navmesh path gets there without coming into an idle creature's aggro range
        // (SafeRoute::PathIsSafe, wander circles included). False also when there is no path.
        bool safe = false;
        // The same with the hard line (SafeRoute::TIGHT_MARGIN): the walk a bot may take when
        // no other enemy is open to it (DamageTargets). Equal to `safe` when `safe` holds.
        bool safeTight = false;
        float pathYd = 0.0f;        // path length (straight distance without a navmesh)
        char const* why = "";       // not safe: SafeRoute::Verdict::why
        ObjectGuid blocker;         // "idle creature": the one the walk comes into
    };

    // Fight-wide rates for the common value unit (value-scoring doc section 3).
    // Filled by CombatRates (Perception/Forecast/CombatRates.h) on every refresh.
    struct RateFacts
    {
        float partyMaxHealth = 0.0f;    // sum over alive and dead members, pets excluded; 1 point = 1% of it
        // Melee of enemies hitting us after armor, plus the main ranged spell of casters standing
        // back (EnemyFacts::rangedDps). Other spells are not included yet.
        float incomingDps = 0.0f;
        // What the party's actions are worth per second (Value::ContributionPerSec): incomingDps,
        // or when no enemy hits us now because all are controlled (a stun, the controlled end),
        // the damage they deal once they wake (on their victim, else on the main tank). With
        // incomingDps alone every action was worth 0 then: the tank stood 25 yd off the enemy
        // it was sent to wake for 30 s (strat-20261114-132853), walks cost nothing.
        float worthDps = 0.0f;
        float dealtDps = 0.0f;          // measured drop of enemy health per second
        bool dealtMeasured = false;     // false = not enough samples yet, dealtDps is an estimate
        // This fight so far (CombatRates): enemy health lost per second per damage dealer that
        // could act, over the time something could be hit (not every enemy held by a control
        // that breaks on damage); 0 until that time reaches CombatRates::MIN_REMEMBERED_FIGHT_MS.
        // Value::CapacityDps uses it: dealtDps covers only the last few seconds, 45/s while
        // one small enemy was left and the rogue held fire, and the party waited 20 s for mana
        // before a 16k-health enemy it killed at 345/s (strat-20261114-174903).
        float fightDpsPerDealer = 0.0f;
        // The last fight's damage per second over its whole length (CombatRates), 0 = none
        // yet, and how many damage dealers the party had then. Value::DealtDps uses it
        // before the next fight is measured.
        float lastFightDps = 0.0f;
        uint32 lastFightDealers = 0;
        float enemyHealth = 0.0f;       // current health of all enemies
        // Members that can act (alive, not controlled) by function, not by role: a lone healer
        // counts in both (PartyMemberFacts::HealsTeam, DealsDamage).
        uint32 healers = 0;             // bots that heal the team
        uint32 damageDealers = 0;       // members that deal damage (tanks and players included)
    };

    // The kind of map (Map::IsRaid / IsDungeon). World: the open world and battlegrounds.
    enum class MapKind : uint8 { World, Dungeon, Raid };

    // A fight the leader started himself (Perception/Spatial/LeaderActivity.h).
    struct LeaderEngage
    {
        ObjectGuid creature;
        std::vector<uint32> entries;    // the kinds of its pack
        Position3 pos;                  // where it stood
    };

    // What the leader does outside fights (Perception/Spatial/LeaderActivity.h).
    struct LeaderActivityFacts
    {
        // He stays around here (also standing still), not on his way somewhere.
        bool wanders = false;
        // Fights he started himself since the last refresh.
        std::vector<LeaderEngage> engaged;
    };

    // The leader running away from the party's fight (Perception/Spatial/LeaderRun.h).
    struct LeaderRunFacts
    {
        bool onMap = true;          // the leader is on this map (false: zoned out, a dungeon door)
        bool leftInFight = false;   // ... and left it while the party fought
        bool running = false;       // moving away from the fight, not casting
        uint32 runMs = 0;           // for this long
        float gainYd = 0.0f;        // farther from the fight than where the run started
        float fromFightYd = 0.0f;   // from the fight's centre now
    };

    struct WorldModel
    {
        uint32 version = 0;          // +1 on every refresh
        uint32 refreshedAtMs = 0;
        uint32 packsRefreshedAtMs = 0;

        ObjectGuid leader;           // party leader (the player; a bot for a team of bots, Commander/Team.h)
        // A real player is in the team, on any map (a player's party). Without one (a population
        // unit) the modules about the player (his run away from the fight, notes to him) are off.
        bool humanInTeam = false;
        // Every bot of the team looks for fights of its own (TeamBotAI::SeeksFights; a road
        // traveller does not): HuntAssigner only for such a team.
        bool seeksFights = true;
        // A member's preferred creature (TeamBotAI::PreferredPrey; the first one set), 0: any.
        uint32 preferredPrey = 0;
        MapKind mapKind = MapKind::World;   // the map the team fights on (the refreshing bot's)
        LeaderRunFacts leaderRun;    // PartyCommander's LeaderRun annotator
        LeaderActivityFacts leaderActivity;     // PartyCommander's LeaderActivity annotator
        std::vector<PartyMemberFacts> party;
        // Dead members with their body or corpse on this map (a released one's ghost is not in
        // `party` when it waits on another map).
        std::vector<CorpseFacts> corpses;
        // Creatures with a party member or pet on their threat list, then (onAllyOnly = true)
        // creatures fighting one of `allies` only.
        std::vector<EnemyFacts> enemies;
        // Creatures a member (the player, a bot, a pet) is attacking that have none of us on
        // their threat list yet and are not in `enemies`: the player just pressed attack and
        // has not hit yet, a bot was sent at one. Not a fight yet (HasPartyEnemy ignores them);
        // TargetAssigner picks the first target of a fight from them.
        std::vector<EnemyFacts> targeted;
        // Friendly non-party creatures in combat near the leader (at most MAX_ALLIES, nearest first).
        std::vector<AllyFacts> allies;
        // Builder bookkeeping: friendly creatures in combat found by the last grid scan
        // (RefreshPacks, once a second), re-read by every RefreshCombat. Not a fact to act on.
        std::vector<ObjectGuid> allyCandidates;
        // Builder bookkeeping: cast starts per (enemy, spell), kept across refreshes (enemies
        // are rebuilt each time) and copied into EnemyFacts::castStarts. Entries of enemies
        // that left the lists are dropped. Read EnemyFacts::castStarts instead.
        struct CastStartRecord
        {
            ObjectGuid enemy;
            CastStart cast;
        };
        std::vector<CastStartRecord> castStartLog;
        std::vector<PackFacts> packs;       // nearby creatures not in combat
        // Out-of-combat creatures walking a waypoint path within PATROL_SCAN_RADIUS of the
        // leader (WorldModelBuilder.h), also those already in `packs`. Long loops reach the
        // party from beyond the pack scan (Patchwork Horror, strat-20261101-225111).
        std::vector<PatrolFacts> patrols;
        RateFacts rates;
        // Walks of melee and tank bots to the enemies they could be sent to, along the navmesh
        // path (Perception/Spatial/MeleeRoutes.h). A pair missing here was not checked yet.
        std::vector<RouteFact> routes;
        // Grid over the fight area with ground, links, idle creatures and patrols
        // (Perception/Spatial/PositionField.h). Null while the party neither fights nor pulls.
        std::shared_ptr<PositionField const> field;
        // The planned pull's creatures that run for help at low health (FleePredictor::
        // AnnotatePull), while a pull is planned and before its fight; empty otherwise.
        std::vector<PullRunnerFacts> pullRunners;

        PartyMemberFacts const* FindMember(ObjectGuid guid) const;
        RouteFact const* FindRoute(ObjectGuid bot, ObjectGuid enemy) const
        {
            for (RouteFact const& r : routes)
                if (r.bot == bot && r.enemy == enemy)
                    return &r;
            return nullptr;
        }
        EnemyFacts const* FindEnemy(ObjectGuid guid) const;
        EnemyFacts const* FindTargeted(ObjectGuid guid) const
        {
            for (EnemyFacts const& e : targeted)
                if (e.guid == guid)
                    return &e;
            return nullptr;
        }
        AllyFacts const* FindAlly(ObjectGuid guid) const
        {
            for (AllyFacts const& a : allies)
                if (a.guid == guid)
                    return &a;
            return nullptr;
        }
        // At least one enemy has a party member or pet on its threat list (a fight of ours,
        // not only an ally's).
        bool HasPartyEnemy() const
        {
            for (EnemyFacts const& e : enemies)
                if (!e.onAllyOnly)
                    return true;
            return false;
        }
        // A bot takes its place in the fight (stance, position modules) while this is true, not
        // only once it is in combat itself: a healer still following the player, out of
        // combat, must go to the tank fighting 30 yd away. `inCombat`: the bot's own flag (an
        // ally's fight it joined counts too).
        bool InFight(bool inCombat) const { return inCombat || HasPartyEnemy(); }
    };
}

#endif
