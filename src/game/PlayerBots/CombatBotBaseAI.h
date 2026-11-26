#ifndef MANGOS_COMBAT_BOT_BASE_H
#define MANGOS_COMBAT_BOT_BASE_H

#include "PlayerBotAI.h"
#include "SpellEntry.h"
#include "Player.h"
#include "HealSpells.h"

struct PlayerPremadeSpecTemplate;
class SpellCastTargets;

// CombatBotBaseAI::EquipRandomGearInEmptySlots limits. The defaults keep the PartyBot behaviour,
// except for rules every bot follows (no tools, fishing or arena gear; trinkets as below).
struct RandomGearOptions
{
    // Players get their first real trinket late in levelling: below this level the trinket slots
    // stay empty, and even then never a crafted one (engineering gadgets and the like).
    uint32 minTrinketLevel = 55;
    bool allowPvP = true;           // false: no honor rank items, no PvP preference
    bool allowCosmetic = true;      // false: no armor-less clothes in armor slots (dresses, tuxedos)
    uint32 maxItemLevel = 0;        // 0 = no cap
    uint32 qualityWeights[MAX_ITEM_QUALITY] = {};  // per slot, pick a quality first; all 0 = any item
    uint32 skipSlotMask = 0;        // 1 << EQUIPMENT_SLOT_*: leave these empty
    uint32 seed = 0;                // != 0: same seed, same picks; 0 = random
};

// CombatBotBaseAI::ChooseHeal result.
struct HealChoice
{
    SpellEntry const* spell = nullptr;
    int chosen = -1;                            // index in options
    bool emergency = false;
    HealSpells::Situation situation;
    std::vector<HealSpells::Option> options;    // biggest direct heal first, then HoTs
    HealSpells::Option const& Chosen() const { return options[chosen]; }
};

struct HealSpellCompare
{
    bool operator() (SpellEntry const* const lhs, SpellEntry const* const rhs) const
    {
        uint32 spell1dmg = 0;
        uint32 spell2dmg = 0;

        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; i++)
        {
            switch (lhs->Effect[i])
            {
                case SPELL_EFFECT_HEAL:
                    spell1dmg += lhs->EffectBasePoints[i];
                    break;
            }
        }
        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; i++)
        {
            switch (rhs->Effect[i])
            {
                case SPELL_EFFECT_HEAL:
                    spell2dmg += rhs->EffectBasePoints[i];
                    break;
            }
        }

        return spell1dmg > spell2dmg;
    }
};

struct HealAuraCompare
{
    bool operator() (SpellEntry const* const lhs, SpellEntry const* const rhs) const
    {
        uint32 spell1dmg = 0;
        uint32 spell2dmg = 0;

        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; i++)
        {
            switch (lhs->Effect[i])
            {
                case SPELL_EFFECT_APPLY_AURA:
                case SPELL_EFFECT_PERSISTENT_AREA_AURA:
                case SPELL_EFFECT_APPLY_AREA_AURA_PARTY:
                    if (lhs->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_HEAL)
                        spell1dmg += lhs->EffectBasePoints[i];
                    break;
            }
        }
        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; i++)
        {
            switch (rhs->Effect[i])
            {
                case SPELL_EFFECT_APPLY_AURA:
                case SPELL_EFFECT_PERSISTENT_AREA_AURA:
                case SPELL_EFFECT_APPLY_AREA_AURA_PARTY:
                    if (rhs->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_HEAL)
                        spell2dmg += rhs->EffectBasePoints[i];
                    break;
            }
        }

        return spell1dmg > spell2dmg;
    }
};

class CombatBotBaseAI : public PlayerBotAI
{
public:

    CombatBotBaseAI() : PlayerBotAI(nullptr)
    {
        for (auto& ptr : m_spells.raw.spells)
            ptr = nullptr;
    }

    virtual void OnPacketReceived(WorldPacket const* packet) override;

    // Virtual: PartyBotAI picks from RoleCapability (talents, gear); population bots keep this.
    virtual void AutoAssignRole();
    void PopulateSpellData();
    void ResetSpellData();
    void AddAllSpellReagents();
    void SummonPetIfNeeded();
    // The warlock's demon knows every ability its level allows (a player's Grimoires) and its
    // party aura (Blood Pact) is on autocast. Once per demon.
    void LearnDemonSpells(Pet* pPet);
    // The hunter's beast as a player's long-kept pet: best friend loyalty, kept happy (fed), its
    // training points spent on the abilities a pet that holds enemies wants (Growl, its damage
    // abilities on autocast, Natural Armor, Great Stamina). Once per pet and level.
    void TrainHunterPet(Pet* pPet);
    // Out of combat, the pet below happy: Feed Pet (known) on the best food it eats in the bags
    // (Shared/PetFood), once its last meal is over. True: cast.
    bool FeedHunterPet(Pet* pPet);
    void LearnArmorProficiencies();
    void LearnPremadeSpecForClass();
    // Spends the free talent points on `spec`'s talents, top row first, as far as they reach.
    void LearnTalentsTowards(PlayerPremadeSpecTemplate const& spec);
    void EquipPremadeGearTemplate();
    void EquipRandomGearInEmptySlots() { EquipRandomGearInEmptySlots(RandomGearOptions()); }
    void EquipRandomGearInEmptySlots(RandomGearOptions const& options);
    void AutoEquipGear(uint32 option);
    void LearnRandomTalents();
    
    uint8 GetAttackersInRangeCount(float range) const;
    Unit* SelectAttackerDifferentFrom(Unit const* pExcept) const;
    Unit* SelectHealTarget(float selfHealPercent = 100.0f, float groupHealPercent = 100.0f) const;
    // Buff targets: the players the bot buffs (BuffCandidates) in reach and sight, without the
    // buff, nobody else casting it on them; a buff that only gives mana (BuffNeedsMana) only on
    // those with mana. With a group version: that version when two or more of one subgroup miss it.
    Player* SelectBuffTarget(SpellEntry const* pSpellEntry) const;
    Player* SelectBuffTarget(SpellEntry const* pSingleSpellEntry, SpellEntry const* pGroupSpellEntry, SpellEntry const*& pSelectedSpellEntry) const;
    // The players the bot buffs, itself included: its WoW group, else itself (TeamBotAI: its
    // Commander team, a population unit too). Same map as the bot only.
    virtual std::vector<Player*> BuffCandidates() const;
    // Intellect or mana regeneration only (Arcane Intellect / Brilliance): wasted on a warrior or a rogue.
    static bool BuffNeedsMana(SpellEntry const* pSpellEntry);
    Player* SelectDispelTarget(SpellEntry const* pSpellEntry) const;
    bool IsValidBuffTarget(Unit const* pTarget, SpellEntry const* pSpellEntry) const;
    // Another group member is already casting this buff (any rank, or the more powerful version)
    // on pMember, or the group version on pMember's subgroup. An instant buff is only cast on
    // the caster's next update, so until then the target shows no aura and a second buffer of
    // the same class would pick the same target. pMember may be a pet (no subgroup check).
    bool IsBuffPendingFromOther(Unit const* pMember, SpellEntry const* pSingleSpellEntry, SpellEntry const* pGroupSpellEntry) const;
    bool IsValidHealTarget(Unit const* pTarget, float healthPercent = 100.0f) const;
    bool IsValidHostileTarget(Unit const* pTarget) const;
    // IsValidHostileTarget without "not under a control that breaks on damage" (PartyBotAI:
    // the bot sent to wake the last controlled enemy).
    bool IsValidHostileTargetIgnoringControl(Unit const* pTarget) const;
    bool IsValidDispelTarget(Unit const* pTarget, SpellEntry const* pSpellEntry) const;
    bool FindAndPreHealTarget();
    bool FindAndHealInjuredAlly(float selfHealPercent = 100.0f, float groupHealPercent = 100.0f);
    // Casts the heal ChooseHeal picks on the target.
    bool HealInjuredTarget(Unit* pTarget);
    // Values every castable single-target heal on the target and picks one (HealSpells.h).
    // `out.options` holds every candidate, biggest direct heal first; `out.spell` the choice.
    // `directOnly`: leave out pure HoTs. `extraNeed`: damage expected on top of the missing
    // health (pre-heal). False when nothing heals.
    bool ChooseHeal(Unit* pTarget, HealChoice& out, bool directOnly = false, float extraNeed = 0.0f) const;
    // Damage per second the target is taking now. Base: melee of its attackers in reach
    // (average weapon damage / attack time, before armor). PartyBotAI uses the Commander's facts.
    virtual float GetIncomingDps(Unit const* pTarget) const;
    // The target may die before a heal lands (HealSpells::IsInDanger): absorb shields.
    bool IsInHealDanger(Unit* pTarget) const;
    int32 GetIncomingdamage(Unit const* pTarget) const;
    bool AreOthersOnSameTarget(ObjectGuid guid, bool checkMelee = true, bool checkSpells = true) const;

    // Facing, one way for every bot action. The server checks the caster's orientation at once:
    // spells with combat range or SPELL_CUSTOM_FACE_TARGET (Taunt, Sunder, Frostbolt, Auto Shot,
    // Shoot: Spell::CheckRange, at the cast start, its end and before every auto-repeat shot),
    // melee swings within a 120 degree arc (Unit::CanAutoAttackTarget). Unit::SetFacingTo turns
    // through a facing spline only on the next movement update, SetFacingToObject not at all
    // while moving. FaceTo sets the orientation now and tells the observers, as a player turns;
    // a running spline would turn it back (FaceForSpell stops first).
    void FaceTo(float orientation);
    void FaceTarget(Unit const* pTarget);
    // `pSpellEntry` checks the caster's facing (SpellEntry::IsNeedFaceTarget) and the bot does
    // not face `pTarget`: stop moving and turn to it.
    void FaceForSpell(Unit const* pTarget, SpellEntry const* pSpellEntry);

    SpellCastResult DoCastSpell(Unit* pTarget, SpellEntry const* pSpellEntry);
    // A ground area spell (Blizzard, Rain of Fire) at the point x/y/z, as a player clicks the
    // ground: turns to it when standing, stops for a cast time or a channel.
    SpellCastResult DoCastSpellAt(float x, float y, float z, SpellEntry const* pSpellEntry);
    // A spell on itself that goes the way it faces (Blink): stops, turns to `orientation` at
    // once (the server reads the caster's orientation when it picks the destination), casts.
    SpellCastResult DoCastFacing(float orientation, SpellEntry const* pSpellEntry);
    virtual bool CanTryToCastSpell(Unit const* pTarget, SpellEntry const* pSpellEntry) const;
    // The checks of CanTryToCastSpell as if the bot were in shapeshift form `form` with `power`
    // of the spell's power type (warrior: right after a stance swap, PartyBotAI::CanCastWithStance).
    bool CanTryToCastSpellAs(Unit const* pTarget, SpellEntry const* pSpellEntry, ShapeshiftForm form, uint32 power) const;
    // CanTryToCastSpellAs without its cooldown and global cooldown checks (PartyBotAI::SpellReadyInSec).
    bool CanTryToCastSpellBesidesTimers(Unit const* pTarget, SpellEntry const* pSpellEntry, ShapeshiftForm form, uint32 power) const;
    // CanTryToCastSpell skips a spell whose aura the target already has (no reapplying DoTs and
    // debuffs). True = cast it anyway: the aura is a side effect of a spell cast for its damage
    // (Frostbolt's slow, Fireball's burn), PartyBotAI::IsSpammedOverOwnAura.
    virtual bool IsSpammedOverOwnAura(SpellEntry const* /*pSpellEntry*/) const { return false; }
    bool IsWearingShield(Player* pPlayer) const;
    CombatBotRoles GetRole() const;

    void EquipOrUseNewItem();
    void CollectBagItemGuids(std::set<ObjectGuid>& guids) const;
    // Group loot roll for `itemId` (a RollVote: ROLL_PASS / ROLL_NEED / ROLL_GREED). Bots pass,
    // so the player gets the item; PartyBotAI lets companions roll like players.
    virtual uint8 ChooseLootRoll(uint32 itemId) const;
    // Reagents (AddAllSpellReagents, the refill on a failed cast) and hunter ammo (AddHunterAmmo)
    // are created for free. PartyBotAI: not for companions, they buy them.
    virtual bool GetsFreeSupplies() const { return true; }
    // What a class quest gives a player is given for free: totem tools (AddAllSpellReagents),
    // a summoned and tamed hunter pet, Call Pet and Revive Pet without the spells
    // (SummonPetIfNeeded), the warlock's Imp summoned at once and for free. PartyBotAI: not for
    // companions, they do the quests (Shared/ClassQuestAbilities), have no hunter pet until they
    // own one and cast Summon Imp.
    virtual bool GetsFreeAbilities() const { return true; }
    void AddItemToInventory(uint32 itemId, uint32 count = 1);
    void AddHunterAmmo();
    // Projectile subclass (ITEM_SUBCLASS_ARROW / ITEM_SUBCLASS_BULLET) the equipped ranged weapon
    // shoots, 0 = no ranged weapon or one without ammo (thrown, wand).
    static uint32 GetRequiredAmmoSubClass(Player const* player);
    // The ammo slot holds ammo that is still in the bags (needed by Shoot Bow/Gun/Crossbow, Auto Shot).
    static bool HasAmmoEquipped(Player const* player);
    // The spell's DBC max range with the caster's range modifiers (Arctic Reach, Flame
    // Throwing, Hawk Eye...), as Spell::CheckRange applies them; without the server's leeway.
    static float SpellMaxRange(Unit const* caster, SpellEntry const* spell);
    // Out of combat: ranged weapon needs ammo and none is equipped -> put the best usable ammo
    // already in the bags into the ammo slot. Never creates items (bots are the user's own
    // characters; AddHunterAmmo is the hunter-only fallback in combat). True = slot set.
    bool EquipAmmoFromBags();
    uint8 GetHighestHonorRankFromEquippedItems() const;
    void UpdateVisualHonorRankBasedOnItems();
    void BeginChasing(Unit* pVictim) const;
    // BeginChasing keeps a caster distance (25 yd) instead of running into melee: ranged or
    // healer role, ranged damage class, not in a form that overrides attack speed, and mana
    // above 10% or a ranged weapon.
    bool UsesCasterChase() const;
    // Keeps `spell` up in `slot` (one cast, true): drops it when the slot is empty, its totem
    // is farther than SHAMAN_TOTEM_RANGE_YD, or it holds another totem without a cooldown (one
    // with a cooldown, Mana Tide, was put down on purpose and stays until it ends).
    static constexpr float SHAMAN_TOTEM_RANGE_YD = 20.0f;
    bool KeepShamanTotem(TotemSlot slot, SpellEntry const* spell);
    SpellCastResult CastWeaponBuff(SpellEntry const* pSpellEntry, EquipmentSlots slot);
    bool UseTrinketEffects(bool onlyToBreakCC = false);
    bool UseItemEffect(Item* pItem, bool onlyToBreakCC = false);
    // Uses the first stack of `itemId` in the backpack or the bags on `pTarget` (null = itself)
    // through the client's path (WorldSession::HandleUseItemOpcode: a player's checks, shared
    // item cooldowns), like `.partybot useslot`. True when it went through (the item was
    // consumed or its cooldown started).
    bool UseBagItem(uint32 itemId, Unit* pTarget = nullptr);
    // The same on an item (a poison vial on an equipped weapon).
    bool UseBagItemOnItem(uint32 itemId, Item* pTarget);
    bool UseBagItemWithTargets(uint32 itemId, SpellCastTargets const& targets);
    // `emergencyCooldowns`: Divine Shield and Ice Block may be spent on it. PartyBotAI passes
    // false: its team rescue decides them (PartyBotAI::BreakControl).
    void BreakCrowdControlEffects(bool emergencyCooldowns = true);

    virtual void UpdateInCombatAI() = 0;
    virtual void UpdateOutOfCombatAI() = 0;

    static bool IsPhysicalDamageClass(uint8 playerClass)
    {
        switch (playerClass)
        {
            case CLASS_WARRIOR:
            case CLASS_PALADIN:
            case CLASS_ROGUE:
            case CLASS_HUNTER:
            case CLASS_SHAMAN:
            case CLASS_DRUID:
                return true;
        }
        return false;
    }
    static bool IsRangedDamageClass(uint8 playerClass)
    {
        switch (playerClass)
        {
            case CLASS_HUNTER:
            case CLASS_PRIEST:
            case CLASS_SHAMAN:
            case CLASS_MAGE:
            case CLASS_WARLOCK:
            case CLASS_DRUID:
                return true;
        }
        return false;
    }
    static bool IsMeleeDamageClass(uint8 playerClass)
    {
        switch (playerClass)
        {
            case CLASS_WARRIOR:
            case CLASS_PALADIN:
            case CLASS_ROGUE:
            case CLASS_SHAMAN:
            case CLASS_DRUID:
                return true;
        }
        return false;
    }
    static bool IsMeleeWeaponClass(uint8 playerClass)
    {
        switch (playerClass)
        {
            case CLASS_WARRIOR:
            case CLASS_PALADIN:
            case CLASS_ROGUE:
            case CLASS_SHAMAN:
                return true;
        }
        return false;
    }
    static bool IsShieldClass(uint8 playerClass)
    {
        switch (playerClass)
        {
            case CLASS_WARRIOR:
            case CLASS_PALADIN:
            case CLASS_SHAMAN:
                return true;
        }
        return false;
    }
    static bool IsTankClass(uint8 playerClass)
    {
        switch (playerClass)
        {
            case CLASS_WARRIOR:
            case CLASS_PALADIN:
            case CLASS_DRUID:
                return true;
        }
        return false;
    }
    static bool IsHealerClass(uint8 playerClass)
    {
        switch (playerClass)
        {
            case CLASS_PALADIN:
            case CLASS_PRIEST:
            case CLASS_SHAMAN:
            case CLASS_DRUID:
                return true;
        }
        return false;
    }
    static bool IsStealthClass(uint8 playerClass)
    {
        switch (playerClass)
        {
            case CLASS_ROGUE:
            case CLASS_DRUID:
                return true;
        }
        return false;
    }

    // items in bags before the current trade, used to find what was received
    std::set<ObjectGuid> m_itemsBeforeTrade;
    bool m_hasTradeSnapshot = false;

    SpellEntry const* m_resurrectionSpell = nullptr;
    std::vector<SpellEntry const*> m_spellListTaunt;
    std::set<SpellEntry const*, HealAuraCompare> m_spellListPeriodicHeal;
    std::set<SpellEntry const*, HealSpellCompare> m_spellListDirectHeal;
    // Single-target heals cast through a dummy or script effect that triggers the heal (Holy
    // Shock, Flash of Light, all ranks; HealSpells::HealingPart). Not in m_spellListDirectHeal:
    // its order sums heal effects.
    std::vector<SpellEntry const*> m_spellListIndirectHeal;
    union
    {
        struct
        {
            SpellEntry const* spells[50];   // at least the largest class struct below (static_assert after the union)
        } raw;
        struct
        {
            // Its own seal for its melee hits: PaladinRotation::OwnSeal picks one each time
            // (role and weapon).
            SpellEntry const* pSealOfRighteousness;
            SpellEntry const* pSealOfCommand;
            SpellEntry const* pSealOfFury;  // pre 1.9 (Righteous Fury after)
            // Seals judged for their debuff on the victim (the brain says which:
            // Commander/Coordination/Duties/JudgementAssigner.h).
            SpellEntry const* pSealOfWisdom;
            SpellEntry const* pSealOfLight;
            // Blessings and auras (best rank). Who gives which: the brain
            // (Commander/Coordination/Duties/BlessingAssigner.h), PaladinRotation casts.
            SpellEntry const* pBlessingOfWisdom;
            SpellEntry const* pBlessingOfMight;
            SpellEntry const* pBlessingOfKings;
            SpellEntry const* pBlessingOfSalvation;
            SpellEntry const* pBlessingOfLight;
            SpellEntry const* pBlessingOfSanctuary;
            // Greater Blessings: the same on every member of the target's class (15 min).
            SpellEntry const* pGreaterBlessingOfWisdom;
            SpellEntry const* pGreaterBlessingOfMight;
            SpellEntry const* pGreaterBlessingOfKings;
            SpellEntry const* pGreaterBlessingOfSalvation;
            SpellEntry const* pGreaterBlessingOfLight;
            SpellEntry const* pGreaterBlessingOfSanctuary;
            SpellEntry const* pDevotionAura;
            SpellEntry const* pConcentrationAura;
            SpellEntry const* pRetributionAura;
            SpellEntry const* pSanctityAura;
            SpellEntry const* pShadowResistanceAura;
            SpellEntry const* pFrostResistanceAura;
            SpellEntry const* pFireResistanceAura;
            SpellEntry const* pBlessingOfProtection;
            SpellEntry const* pBlessingOfFreedom;
            SpellEntry const* pHammerOfJustice;
            SpellEntry const* pJudgement;
            SpellEntry const* pExorcism;
            SpellEntry const* pConsecration;
            SpellEntry const* pHammerOfWrath;
            SpellEntry const* pCleanse;
            SpellEntry const* pDivineShield;
            SpellEntry const* pLayOnHands;
            SpellEntry const* pRighteousFury;
            SpellEntry const* pHolyShock;
            SpellEntry const* pDivineFavor;
            SpellEntry const* pHolyWrath;
            SpellEntry const* pTurnUndead;  // Turn Evil from 2.0; crowd control (ClassKit::Controls)
            SpellEntry const* pHolyShield;
            SpellEntry const* pPurify; // fallback dispel when Cleanse is not learned
            SpellEntry const* pDivineProtection;
        } paladin;
        struct
        {
            SpellEntry const* pLightningBolt;
            SpellEntry const* pChainLightning;
            SpellEntry const* pEarthShock;
            SpellEntry const* pFlameShock;
            SpellEntry const* pFrostShock;
            SpellEntry const* pPurge;
            SpellEntry const* pStormstrike;
            SpellEntry const* pElementalMastery;
            SpellEntry const* pLightningShield;
            SpellEntry const* pGhostWolf;
            SpellEntry const* pCureDisease;
            SpellEntry const* pCurePoison;
            // Party totems (best rank). Which one each shaman keeps up: the brain
            // (Commander/Coordination/Duties/TotemAssigner.h), ShamanRotation drops them.
            // Until 2026-11-15 one random totem per element (Earthbind, Fire Nova, Searing...).
            SpellEntry const* pManaSpringTotem;
            SpellEntry const* pHealingStreamTotem;
            SpellEntry const* pStrengthOfEarthTotem;
            SpellEntry const* pStoneskinTotem;
            SpellEntry const* pWindfuryTotem;
            SpellEntry const* pGraceOfAirTotem;
            SpellEntry const* pTranquilAirTotem;
            SpellEntry const* pFlametongueTotem;
            SpellEntry const* pManaTideTotem;
            SpellEntry const* pWeaponBuff;
            SpellEntry const* pNaturesSwiftness;
        } shaman;
        struct
        {
            SpellEntry const* pAspectOfTheCheetah;
            SpellEntry const* pAspectOfTheHawk;
            SpellEntry const* pSerpentSting;
            SpellEntry const* pAimedShot;
            SpellEntry const* pAimedShotRank1;  // lowest rank known: the cheap one when mana runs low
            SpellEntry const* pMultiShotRank1;  // lowest rank known: most of its damage is the weapon's
            SpellEntry const* pConcussiveShot;
            SpellEntry const* pWingClip;
            SpellEntry const* pHuntersMark;
            SpellEntry const* pMongooseBite;
            SpellEntry const* pRaptorStrike;
            SpellEntry const* pFeignDeath;
            SpellEntry const* pRapidFire;
            SpellEntry const* pBestialWrath;
            SpellEntry const* pDeterrence;
            SpellEntry const* pIntimidation;
            SpellEntry const* pMendPet;
        } hunter;
        struct
        {
            SpellEntry const* pIceArmor;
            SpellEntry const* pArcaneIntellect;
            SpellEntry const* pArcaneBrilliance;
            SpellEntry const* pIceBarrier;
            SpellEntry const* pManaShield;
            SpellEntry const* pPolymorph;
            SpellEntry const* pFrostbolt;
            SpellEntry const* pFireBlast;
            SpellEntry const* pFireball;
            SpellEntry const* pArcaneExplosion;
            SpellEntry const* pFrostNova;
            SpellEntry const* pConeofCold;
            SpellEntry const* pBlink;
            SpellEntry const* pCounterspell;
            SpellEntry const* pPresenceOfMind;
            SpellEntry const* pArcanePower;
            SpellEntry const* pRemoveLesserCurse;
            SpellEntry const* pScorch;
            SpellEntry const* pPyroblast;
            SpellEntry const* pEvocation;
            SpellEntry const* pIceBlock;
            SpellEntry const* pBlizzard;
            SpellEntry const* pBlastWave;
            SpellEntry const* pCombustion;
            SpellEntry const* pMageArmor;
            SpellEntry const* pColdSnap;
            SpellEntry const* pConjureManaGem;  // the best Conjure Mana Agate / Jade / Citrine / Ruby
            SpellEntry const* pArcaneMissiles;  // only for a victim immune to fire and frost (MageKit::Fillers)
            // Frostbolt, or Fireball for a fire build (more talent points in fire than in
            // frost); set in PopulateSpellData (MageKit::Filler)
            SpellEntry const* pFiller;
        } mage;
        struct
        {
            SpellEntry const* pPowerWordFortitude;
            SpellEntry const* pDivineSpirit;
            SpellEntry const* pPrayerofSpirit;
            SpellEntry const* pPrayerofFortitude;
            SpellEntry const* pPrayerofShadowProtection;
            SpellEntry const* pInnerFire;
            SpellEntry const* pShadowProtection;
            SpellEntry const* pPowerWordShield;
            SpellEntry const* pHolyNova;
            SpellEntry const* pHolyFire;
            SpellEntry const* pMindBlast;
            SpellEntry const* pMindFlay;
            SpellEntry const* pShadowWordPain;
            SpellEntry const* pInnerFocus;
            SpellEntry const* pAbolishDisease;
            SpellEntry const* pDispelMagic;
            SpellEntry const* pManaBurn;
            SpellEntry const* pDevouringPlague;
            SpellEntry const* pPsychicScream;
            SpellEntry const* pShadowform;
            SpellEntry const* pVampiricEmbrace;
            SpellEntry const* pSilence;
            SpellEntry const* pFade;
            SpellEntry const* pShackleUndead;
            SpellEntry const* pSmite;
            SpellEntry const* pCureDisease; // fallback dispel when Abolish Disease is not learned
            SpellEntry const* pDesperatePrayer;
            SpellEntry const* pPowerInfusion;
        } priest;
        struct
        {
            SpellEntry const* pDemonArmor;
            SpellEntry const* pDeathCoil;
            SpellEntry const* pDetectInvisibility;
            SpellEntry const* pShadowWard;
            SpellEntry const* pShadowBolt;
            SpellEntry const* pCorruption;
            SpellEntry const* pConflagrate;
            SpellEntry const* pShadowburn;
            SpellEntry const* pSearingPain;
            SpellEntry const* pImmolate;
            SpellEntry const* pRainOfFire;
            SpellEntry const* pDemonicSacrifice;
            SpellEntry const* pDrainLife;
            SpellEntry const* pDrainSoul;
            SpellEntry const* pSiphonLife;
            SpellEntry const* pBanish;
            SpellEntry const* pFear;
            SpellEntry const* pHowlofTerror;
            SpellEntry const* pCurseofAgony;
            SpellEntry const* pCurseofDoom;
            SpellEntry const* pCurseoftheElements;
            SpellEntry const* pCurseofShadow;
            SpellEntry const* pCurseofRecklessness;
            SpellEntry const* pCurseofTongues;
            SpellEntry const* pCurseofExhaustion;
            SpellEntry const* pLifeTap;
            SpellEntry const* pAmplifyCurse;
            SpellEntry const* pCreateSoulstone;     // the best Create Soulstone (Minor .. Major)
        } warlock;
        struct
        {
            SpellEntry const* pBattleStance;
            SpellEntry const* pBerserkerStance;
            SpellEntry const* pDefensiveStance;
            SpellEntry const* pCharge;
            SpellEntry const* pIntercept;
            SpellEntry const* pOverpower;
            SpellEntry const* pHeroicStrike;
            SpellEntry const* pCleave;
            SpellEntry const* pExecute;
            SpellEntry const* pMortalStrike;
            SpellEntry const* pBloodthirst;
            SpellEntry const* pBloodrage;
            SpellEntry const* pBerserkerRage;
            SpellEntry const* pRecklessness;
            SpellEntry const* pRetaliation;
            SpellEntry const* pDeathWish;
            SpellEntry const* pIntimidatingShout;
            SpellEntry const* pPummel;
            SpellEntry const* pRend;
            SpellEntry const* pDisarm;
            SpellEntry const* pWhirlwind;
            SpellEntry const* pBattleShout;
            SpellEntry const* pDemoralizingShout;
            SpellEntry const* pHamstring;
            SpellEntry const* pThunderClap;
            SpellEntry const* pSweepingStrikes;
            SpellEntry const* pLastStand;
            SpellEntry const* pShieldBlock;
            SpellEntry const* pShieldWall;
            SpellEntry const* pShieldBash;
            SpellEntry const* pShieldSlam;
            SpellEntry const* pSunderArmor;
            SpellEntry const* pConcussionBlow;
            SpellEntry const* pPiercingHowl;
            SpellEntry const* pRevenge;
            SpellEntry const* pChallengingShout;
        } warrior;
        struct
        {
            SpellEntry const* pSliceAndDice;
            SpellEntry const* pSinisterStrike;
            SpellEntry const* pAdrenalineRush;
            SpellEntry const* pEviscerate;
            SpellEntry const* pStealth;
            SpellEntry const* pAmbush;
            SpellEntry const* pCheapShot;
            SpellEntry const* pPremeditation;
            SpellEntry const* pBackstab;
            SpellEntry const* pHemorrhage;
            SpellEntry const* pGhostlyStrike;
            SpellEntry const* pGouge;
            SpellEntry const* pKidneyShot;
            SpellEntry const* pColdBlood;
            SpellEntry const* pBladeFlurry;
            SpellEntry const* pVanish;
            SpellEntry const* pFeint;   // only the Commander casts it (team rescue: drop threat)
            SpellEntry const* pBlind;
            SpellEntry const* pSap;     // only the Commander casts it (crowd control, before a pull)
            SpellEntry const* pPreparation;
            SpellEntry const* pEvasion;
            SpellEntry const* pKick;
            SpellEntry const* pSprint;
            SpellEntry const* pMainHandPoison;
            SpellEntry const* pOffHandPoison;
        } rogue;
        struct
        {
            SpellEntry const* pBearForm;
            SpellEntry const* pCatForm;
            SpellEntry const* pTravelForm;
            SpellEntry const* pAquaticForm;
            SpellEntry const* pMoonkinForm;
            SpellEntry const* pWrath;
            SpellEntry const* pMoonfire;
            SpellEntry const* pStarfire;
            SpellEntry const* pHurricane;
            SpellEntry const* pInsectSwarm;
            SpellEntry const* pBarkskin;
            SpellEntry const* pNaturesGrasp;
            SpellEntry const* pMarkoftheWild;
            SpellEntry const* pGiftoftheWild;
            SpellEntry const* pThorns;
            SpellEntry const* pRemoveCurse;
            SpellEntry const* pCurePoison;
            SpellEntry const* pAbolishPoison;
            SpellEntry const* pRebirth;
            SpellEntry const* pFaerieFire;
            SpellEntry const* pInnervate;
            SpellEntry const* pNaturesSwiftness;
            SpellEntry const* pEntanglingRoots;
            SpellEntry const* pHibernate;
            // Cat
            SpellEntry const* pProwl;
            SpellEntry const* pPounce;
            SpellEntry const* pRavage;
            SpellEntry const* pClaw;
            SpellEntry const* pShred;
            SpellEntry const* pRake;
            SpellEntry const* pRip;
            SpellEntry const* pFerociousBite;
            SpellEntry const* pTigersFury;
            SpellEntry const* pDash;
            SpellEntry const* pFaerieFireFeral;
            SpellEntry const* pCower;
            // Bear
            SpellEntry const* pGrowl;
            SpellEntry const* pChallengingRoar;
            SpellEntry const* pDemoralizingRoar;
            SpellEntry const* pEnrage;
            SpellEntry const* pFrenziedRegeneration;
            SpellEntry const* pSwipe;
            SpellEntry const* pMaul;
            SpellEntry const* pBash;
            SpellEntry const* pFeralCharge;
            SpellEntry const* pTranquility;
            SpellEntry const* pSwiftmend;
        } druid;
    } m_spells;
    static_assert(sizeof(m_spells) == sizeof(m_spells.raw), "a class spell struct is larger than raw.spells");

    bool m_initialized = false;
    bool m_isBuffing = false;
    bool m_preventCasting = false;
    uint8 m_visualHonorRank = 0;
    CombatBotRoles m_role = ROLE_INVALID;

    // Fix (2026-11-14, stockade-20261114-224312: a mage's Frostbolt failed on line of sight
    // every 200 ms for 2 s): a spell on `m_losFailTarget` failed SPELL_FAILED_LINE_OF_SIGHT;
    // CanTryToCastSpell skips that target for LOS_RETRY_MS, or until the bot moved
    // LOS_RETRY_MOVED_YD from where it failed, so the rotation goes on to what it can cast.
    static constexpr uint32 LOS_RETRY_MS = 600;
    static constexpr float LOS_RETRY_MOVED_YD = 1.0f;
    ObjectGuid m_losFailTarget;
    uint32 m_losFailMs = 0;
    float m_losFailX = 0.0f;
    float m_losFailY = 0.0f;
    bool LosFailedRecently(Unit const* pTarget) const;

    ObjectGuid m_demonSpellsLearnedFor;     // LearnDemonSpells
    ObjectGuid m_hunterPetTrainedFor;       // TrainHunterPet
    uint32 m_hunterPetTrainedLevel = 0;
};

#endif
