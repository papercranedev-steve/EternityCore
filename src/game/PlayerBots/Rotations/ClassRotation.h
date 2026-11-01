/*
 * PartyBot class rotations ("cerebellum"): how a class casts, one file per class.
 *
 * The rotation is the bot's default behaviour each tick: buffs, ability order, class
 * mechanics. Whether and which (taunt, interrupt, heal target, area spells, mana restore) is
 * decided by Commander modules, which veto or replace rotation steps (Commander/VetoChecks.h,
 * Behaviors/Arbiter.h); "which spell does job X" for those modules is the ClassKit
 * (Commander/Classes/ClassKit.h). Keep decisions out of here.
 *
 * Classes move out of PartyBotAI.cpp one at a time, when their rotation needs changes
 * (user, 2026-11-01). ClassRotations::Create returns nullptr for a class still in
 * PartyBotAI.cpp; PartyBotAI's UpdateInCombatAI_<Class> of a moved class only delegates.
 *
 * Injection: PartyBotAI creates its rotation with its class kit (UpdateAI init) and holds it
 * (PartyBotAI::Rotation()). The rotation keeps a reference to its bot and reads bot.m_spells
 * on every call, so rebuilt spell data (ResetSpellData) is seen at once. Map thread of the bot.
 */

#ifndef MANGOS_PARTYBOT_CLASS_ROTATION_H
#define MANGOS_PARTYBOT_CLASS_ROTATION_H

#include "Common.h"
#include <memory>

class PartyBotAI;

class ClassRotation
{
public:
    explicit ClassRotation(PartyBotAI& bot) : m_bot(bot) {}
    virtual ~ClassRotation() = default;

    // PartyBotAI::UpdateInCombatAI, after the shared steps (tank taunts, marks, dispels).
    virtual void UpdateInCombat() = 0;
    // PartyBotAI::UpdateOutOfCombatAI, after resurrection, marks and dispels.
    virtual void UpdateOutOfCombat() = 0;

protected:
    PartyBotAI& m_bot;
};

namespace ClassRotations
{
    // Spells every rotation may use (same ids as PartyBotAI.cpp PartyBotSpells).
    uint32 const SPELL_SHOOT_WAND = 5019;

    // The rotation of the bot's class, nullptr while that class is still in PartyBotAI.cpp.
    std::unique_ptr<ClassRotation> Create(PartyBotAI& bot);

    // One per class file (Rotations/<Class>Rotation.cpp).
    std::unique_ptr<ClassRotation> MakeMage(PartyBotAI& bot);
}

#endif
