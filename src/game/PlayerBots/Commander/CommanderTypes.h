/*
 * PartyBot Commander: shared plain-data types.
 * Nothing in here may hold a game object pointer except SpellEntry (static DBC data).
 */

#ifndef MANGOS_COMMANDER_TYPES_H
#define MANGOS_COMMANDER_TYPES_H

#include "Common.h"
#include "ObjectGuid.h"

class SpellEntry;

namespace Commander
{
    // Lower value wins. A higher tier with any proposal hides all lower tiers.
    enum class ProposalTier : uint8
    {
        // A party-wide order of the player that every other module must yield to (retreat).
        // Above Urgent: a taunt or an interrupt at Urgent would hit an enemy and extend its
        // leash, so it never gives up the chase (combat scenario 11, 1.5).
        Override = 0,
        Urgent   = 1,   // someone about to die, aggro already lost, standing in a hazard
        Assigned = 2,   // task given to this bot by team assignment
        Routine  = 3,   // nothing more pressing
    };

    enum class BotActionType : uint8
    {
        Cast,       // cast spell on target (empty target = self)
        Attack,     // switch melee/ranged attack to target
        MoveTo,     // move to x/y/z and keep original movement suppressed for holdMs
        Hold,       // do nothing this tick and skip the original rotation
        HoldFire,   // like Hold, and also stop melee swings / auto shot (keeps the target)
        // Drop the target (AttackStop), stop auto shot, call the pet back; then, if holdMs > 0,
        // move to x/y/z like MoveTo. Always succeeds (also when rooted), so the original
        // rotation never runs and never picks a new target while the party retreats.
        Retreat,
        // Use the gameobject `target` (a gameobject guid, not a unit): walk to it with movement
        // held for holdMs while out of INTERACTION_DISTANCE, then GameObject::Use(me) (button,
        // lever). Same use path as `.partybot usegobject` (PlayerBotMgr.cpp).
        UseObject,
    };

    struct BotAction
    {
        BotActionType type = BotActionType::Hold;
        ObjectGuid target;          // unit; gameobject for UseObject
        SpellEntry const* spell = nullptr;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        uint32 holdMs = 0;
    };

    // Replace: do this action this tick instead of the original rotation.
    // Veto: the original rotation still runs, but must not do one thing (value-scoring doc 6.1).
    enum class ProposalKind : uint8
    {
        Replace,
        Veto,
    };

    // What a veto forbids. Every type has a check in official PartyBot code, listed in
    // .kiro/docs/2026-11-01-ai-teammate-code-structure.md section 6.2.
    enum class VetoType : uint8
    {
        None,
        Dispel,     // do not dispel aura `spellId` from `target` (PartyBotAI::IsValidDispelTarget)
        Interrupt,  // do not interrupt / stun `target`'s cast (PartyBotAI::IsInterruptVetoed, 6 class rotations)
        Heal,       // do not heal `target` this tick (PartyBotAI::FindAndHealInjuredAlly)
    };

    struct Veto
    {
        VetoType type = VetoType::None;
        ObjectGuid target;
        uint32 spellId = 0;     // 0 = any
    };

    struct Proposal
    {
        ProposalKind kind = ProposalKind::Replace;
        ProposalTier tier = ProposalTier::Routine;
        float score = 0.0f;           // compared only within the same tier
        char const* source = "";      // behavior id, for logs and .partybot ai bot
        BotAction action;             // Replace
        Veto veto;                    // Veto

        // Value-scored proposals (value-scoring doc section 2): score = benefit - cost, in points.
        // The arbiter drops them when benefit <= cost. Older modules leave valued = false.
        bool valued = false;
        float benefit = 0.0f;
        float cost = 0.0f;
        char const* reason = "";      // short note for the candidate log

        void SetValue(float b, float c)
        {
            valued = true;
            benefit = b;
            cost = c;
            score = b - c;
        }
    };
}

#endif
