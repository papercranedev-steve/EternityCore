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
 * Injection: TeamBotAI creates its rotation with its class kit (UpdateAI init) and holds it
 * (TeamBotAI::Rotation(), made again when the role changes). The rotation keeps a reference to
 * its bot and reads bot.m_spells on every call, so rebuilt spell data (ResetSpellData) is seen
 * at once. Map thread of the bot.
 */

#ifndef MANGOS_PARTYBOT_CLASS_ROTATION_H
#define MANGOS_PARTYBOT_CLASS_ROTATION_H

#include "Common.h"
#include <initializer_list>
#include <memory>

class TeamBotAI;
class SpellEntry;
class Unit;

class ClassRotation
{
public:
    explicit ClassRotation(TeamBotAI& bot) : m_bot(bot) {}
    virtual ~ClassRotation() = default;

    // TeamBotAI::UpdateInCombatAI, after the shared steps (tank taunts, marks, dispels).
    virtual void UpdateInCombat() = 0;
    // TeamBotAI::UpdateOutOfCombatAI, after resurrection, marks and dispels.
    virtual void UpdateOutOfCombat() = 0;
    // While an auto-repeat spell runs (Auto Shot, wand) with a victim; the bot update does not
    // reach UpdateInCombat then. Default: keep shooting (a wand user's rotation waits for it).
    virtual void UpdateWhileAutoRepeating() {}

protected:
    // Not held from the rotation (TeamBotAI::IsHeldFromRotation: flee stop, kept interrupt),
    // CanTryToCastSpell + DoCastSpell; true = cast.
    bool Cast(Unit* pTarget, SpellEntry const* pSpell);
    // Cast, then marks the bot as buffing (UpdateAI waits for the buff before moving on).
    bool Buff(Unit* pTarget, SpellEntry const* pSpell);
    // Ends m_isBuffing once the global cooldown of the buff that started it is over.
    void EndBuffing(SpellEntry const* pBuffSpell);
    // Rogue Stealth, cat Prowl: only the Commander goes into it (ClassKit::Stealth). Cancelled
    // without a victim to open on, unless a module keeps it (TeamBotAI::IsStealthControlled:
    // the Sap controller of a pull). Until 2026-11-26 the rotation went into stealth itself
    // whenever it had a victim out of combat (every pull: a slow walk to a mob already on the
    // tank), below 10% health, or with the leader dead, feigning or stealthed.
    void DropUnwantedStealth(SpellEntry const* pStealthSpell);
    // One single target damage spell of the rotation; `consumes`: a periodic effect of the bot's
    // it ends (Conflagrate: Immolate).
    struct DamageOption
    {
        SpellEntry const* spell = nullptr;
        SpellEntry const* consumes = nullptr;
    };
    // Of the options it can cast now on `pVictim`, the one worth the most per second it is busy
    // (Commander/Perception/Values/DamageSpellValue.h: damage landed before the victim dies,
    // less the mana's worth), the next when a cast is refused; worth nothing (the mana is worth
    // more than the damage it lands): not cast. Without a Commander or facts on the victim:
    // the first in the list that casts. True = cast.
    bool CastBestDamage(Unit* pVictim, std::initializer_list<DamageOption> options);
    // Wand when out of mana (below `manaPct`): StartWand.
    void ShootWandBelowMana(Unit* pVictim, float manaPct);
    // Starts the wand on `pVictim`: learned with a wand, standing, not already shooting, no
    // global cooldown or cast running (each shot fired starts one, Spell::cast). True = started.
    // (A healer's shot that would pull aggro: ShootWandBelowMana checks it first.)
    bool StartWand(Unit* pVictim);
    // Wand at any mana (a healer's free attack, Commander HEALER_DAMAGE_FREE).
    void ShootWand(Unit* pVictim) { ShootWandBelowMana(pVictim, 101.0f); }
    // A caster standing idle beyond its filler's range (30 yd without one) walks closer (BeginChasing honours
    // the Commander's movement hold and stance takeover).
    void ChaseWhenOutOfRange(Unit* pVictim);
    // An idle pet (hunter, warlock) attacks `pVictim`; not a pet kept aside (KeepPetAside).
    void SendPetAt(Unit* pVictim);
    // The pet knows the kit's ClassKit::PetStandAside spell (the Imp's Phase Shift): passive,
    // called back, every autocast off but its party auras (Blood Pact), and the spell cast once
    // those are up. Costs the bot nothing (the pet casts). True = the pet is kept aside.
    bool KeepPetAside();

    TeamBotAI& m_bot;

private:
    bool ShouldBeStealthed() const;
};

namespace ClassRotations
{
    // Spells every rotation may use.
    uint32 const SPELL_SHOOT_WAND = 5019;
    uint32 const SPELL_AUTO_SHOT = 75;

    // The rotation of the bot's class and role; nullptr for a class without one.
    std::unique_ptr<ClassRotation> Create(TeamBotAI& bot);

    // One per file (Rotations/<Class>[<Role>]Rotation.cpp).
    std::unique_ptr<ClassRotation> MakeDruid(TeamBotAI& bot);
    std::unique_ptr<ClassRotation> MakeHunter(TeamBotAI& bot);
    std::unique_ptr<ClassRotation> MakeMage(TeamBotAI& bot);
    std::unique_ptr<ClassRotation> MakePaladin(TeamBotAI& bot);
    std::unique_ptr<ClassRotation> MakePriest(TeamBotAI& bot);
    std::unique_ptr<ClassRotation> MakeRogue(TeamBotAI& bot);
    std::unique_ptr<ClassRotation> MakeShaman(TeamBotAI& bot);
    std::unique_ptr<ClassRotation> MakeWarlock(TeamBotAI& bot);
    std::unique_ptr<ClassRotation> MakeWarrior(TeamBotAI& bot);
    std::unique_ptr<ClassRotation> MakeWarriorTank(TeamBotAI& bot);
}

#endif
