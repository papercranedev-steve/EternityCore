/*
 * PartyBot class rotations ("cerebellum", Routine layer): how a class casts, one file per class.
 *
 * The rotation is the bot's default behaviour each tick: buffs, ability order, class
 * mechanics. Whether and which (taunt, interrupt, heal target, area spells, mana restore) is
 * decided by Commander modules, which veto or replace rotation steps (Commander/VetoChecks.h,
 * Behaviors/Arbiter.h); "which spell does job X" for those modules is the ClassKit
 * (Commander/Classes/ClassKit.h). Keep decisions out of here.
 *
 * Every class has its rotation here since 2026-11-14 (before: moved out of PartyBotAI.cpp one
 * at a time). A class may have one rotation per role when the roles do different jobs
 * (warrior: WarriorTankRotation for the tank role, WarriorRotation for the damage roles).
 *
 * Injection: PartyBotAI creates its rotation with its class kit (UpdateAI init) and holds it
 * (PartyBotAI::Rotation(), made again when the role changes). The rotation keeps a reference to
 * its bot and reads bot.m_spells on every call, so rebuilt spell data (ResetSpellData) is seen
 * at once. Map thread of the bot.
 */

#ifndef MANGOS_PARTYBOT_CLASS_ROTATION_H
#define MANGOS_PARTYBOT_CLASS_ROTATION_H

#include "Common.h"
#include <memory>

class PartyBotAI;
class SpellEntry;
class Unit;

class ClassRotation
{
public:
    explicit ClassRotation(PartyBotAI& bot) : m_bot(bot) {}
    virtual ~ClassRotation() = default;

    // PartyBotAI::UpdateInCombatAI, after the shared steps (tank taunts, marks, dispels).
    virtual void UpdateInCombat() = 0;
    // PartyBotAI::UpdateOutOfCombatAI, after resurrection, marks and dispels.
    virtual void UpdateOutOfCombat() = 0;
    // While an auto-repeat spell runs (Auto Shot, wand) with a victim; the bot update does not
    // reach UpdateInCombat then. Default: keep shooting (a wand user's rotation waits for it).
    virtual void UpdateWhileAutoRepeating() {}

protected:
    // CanTryToCastSpell + DoCastSpell; true = cast.
    bool Cast(Unit* pTarget, SpellEntry const* pSpell);
    // Cast, then marks the bot as buffing (UpdateAI waits for the buff before moving on).
    bool Buff(Unit* pTarget, SpellEntry const* pSpell);
    // Ends m_isBuffing once the global cooldown of the buff that started it is over.
    void EndBuffing(SpellEntry const* pBuffSpell);
    // Rogue Stealth, cat Prowl: in when there is a victim, the bot is almost dead, or the leader
    // is dead / feigning / stealthed; cancelled otherwise. Stands down while the Commander keeps
    // the Sap controller of a pull stealthed (PartyBotAI::IsStealthControlled). True = cast.
    bool KeepStealth(SpellEntry const* pStealthSpell);
    // Wand when out of mana (below `manaPct`), standing, not already shooting.
    void ShootWandBelowMana(Unit* pVictim, float manaPct);
    // A caster standing idle more than 30 yd from its victim walks closer (BeginChasing honours
    // the Commander's movement hold and stance takeover).
    void ChaseWhenOutOfRange(Unit* pVictim);
    // An idle pet (hunter, warlock) attacks `pVictim`; not a pet kept aside (KeepPetAside).
    void SendPetAt(Unit* pVictim);
    // The pet knows the kit's ClassKit::PetStandAside spell (the Imp's Phase Shift): passive,
    // called back, every autocast off but its party auras (Blood Pact), and the spell cast once
    // those are up. Costs the bot nothing (the pet casts). True = the pet is kept aside.
    bool KeepPetAside();

    PartyBotAI& m_bot;

private:
    bool ShouldBeStealthed() const;
};

namespace ClassRotations
{
    // Spells every rotation may use.
    uint32 const SPELL_SHOOT_WAND = 5019;
    uint32 const SPELL_AUTO_SHOT = 75;

    // The rotation of the bot's class and role; nullptr for a class without one.
    std::unique_ptr<ClassRotation> Create(PartyBotAI& bot);

    // One per file (Rotations/<Class>[<Role>]Rotation.cpp).
    std::unique_ptr<ClassRotation> MakeDruid(PartyBotAI& bot);
    std::unique_ptr<ClassRotation> MakeHunter(PartyBotAI& bot);
    std::unique_ptr<ClassRotation> MakeMage(PartyBotAI& bot);
    std::unique_ptr<ClassRotation> MakePaladin(PartyBotAI& bot);
    std::unique_ptr<ClassRotation> MakePriest(PartyBotAI& bot);
    std::unique_ptr<ClassRotation> MakeRogue(PartyBotAI& bot);
    std::unique_ptr<ClassRotation> MakeShaman(PartyBotAI& bot);
    std::unique_ptr<ClassRotation> MakeWarlock(PartyBotAI& bot);
    std::unique_ptr<ClassRotation> MakeWarrior(PartyBotAI& bot);
    std::unique_ptr<ClassRotation> MakeWarriorTank(PartyBotAI& bot);
}

#endif
