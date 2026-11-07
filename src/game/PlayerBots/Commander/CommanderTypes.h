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
        // Stop the walk of the proposing module's own MoveTo (BotDirectives::GetMovementOwner
        // == Proposal::source) and release the hold; fails when the walk is somebody else's.
        // The rotation still runs in the same tick (casts right where it stopped).
        StopMove,
        // Walk backwards to x/y/z facing the way it came (Behaviors/Backpedal.h), order valid
        // for holdMs (propose it again every tick to keep it); `target` is faced at the end.
        // The original rotation still runs in the same tick (instant abilities while walking).
        Backpedal,
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
        // Pet actions command the bot's pet, not the bot. The arbiter executes them besides the
        // bot's own action (the rotation still runs): every autocast toggle, and the best pet
        // command (PetAttack / PetFollow). Pet commands are skipped during a retreat (Override).
        PetAttack,      // pet attacks `target` (the pet bar's Attack button)
        // Pet stops attacking and runs back to the bot (Follow button). While it runs back, the
        // rotation's "send the pet in" is refused (PetAI::CanAttack: returning after Follow).
        PetFollow,
        PetAutocastOn,  // turn autocast of the pet's `spell` on
        PetAutocastOff, // turn autocast of the pet's `spell` off
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
    // HealOrder: the original rotation still runs (dispels in their usual order;
    // non-control dispels stand down while the order is Urgent, VetoChecks::IsDispelVetoed);
    // at its heal step it casts `action` (a Cast) instead of choosing a heal itself
    // (PartyBotAI::FindAndHealInjuredAlly). The best order of the tick is kept for one tick and
    // outranks Replace proposals of a lower tier / score (Arbiter.h).
    enum class ProposalKind : uint8
    {
        Replace,
        Veto,
        HealOrder,
    };

    // A decision a Commander module makes instead of the class rotation (takeover instead of
    // veto: .kiro/docs/2026-11-03-discussion/2026-11-03-architecture-refinement.md 5.2). While a
    // module takes it over (IBehavior::TakesOver -> BotDirectives::TakeOver) the rotation's own
    // version stands down; the check in official code is listed in the code-structure doc 6.2.
    enum class Takeover : uint8
    {
        Taunt,      // tank auto-taunt and "protect a member" (ThreatBehavior)
        Escape,     // Frost Nova / Blink / Psychic Scream / run away from melee (EscapeBehavior)
        Interrupt,  // reflex interrupts of the class rotations (InterruptBehavior)
        Healing,    // heal target, rank and Power Word: Shield (HealPlanBehavior), in combat
        ThreatDrop, // Fade (ThreatBehavior)
        Count
    };

    // What a veto forbids. Every type has a check in official PartyBot code, listed in
    // .kiro/docs/2026-11-01-ai-teammate-code-structure.md section 6.2.
    enum class VetoType : uint8
    {
        None,
        Dispel,     // do not dispel aura `spellId` from `target` (PartyBotAI::IsValidDispelTarget)
        Heal,       // do not heal `target` this tick (PartyBotAI::FindAndHealInjuredAlly)
        // Do not cast a spell that charges at `target` (empty = any): Charge, Intercept, Feral
        // Charge (PartyBotAI::PassesPartyCastChecks). The pull brings the pack back.
        GapCloser,
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
