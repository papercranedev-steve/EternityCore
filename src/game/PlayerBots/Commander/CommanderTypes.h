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
        // Walk backwards to x/y/z facing the way it came (Behaviors/Movement/Backpedal.h), order valid
        // for holdMs (propose it again every tick to keep it); `target` is faced at the end.
        // The original rotation still runs in the same tick (instant abilities while walking).
        Backpedal,
        // Melee stance (MeleeStanceBehavior): attack `target` if it is not the victim yet, then
        // chase it at angle `x` (radians from its facing, PartyBotAI::OrderChase). The chase
        // keeps running after the tick; the original rotation still runs in the same tick.
        Chase,
        // Stop the chase of the victim (keeps the victim): the walk there passes an idle pack.
        // The original rotation still runs in the same tick.
        StopChase,
        Hold,       // do nothing this tick and skip the original rotation
        // Out of combat: eat and / or drink what is missing, or keep sitting (PartyBotAI::
        // StartResting); fails in combat or with nothing missing. Every other action stands a
        // resting bot up first.
        Rest,
        HoldFire,   // like Hold, and also stop melee swings / auto shot (keeps the target)
        // Drop the target (AttackStop), stop auto shot, call the pet back; then, if holdMs > 0,
        // move to x/y/z like MoveTo. Always succeeds (also when rooted), so the original
        // rotation never runs and never picks a new target while the party retreats.
        Retreat,
        // Use the gameobject `target` (a gameobject guid, not a unit): walk to it with movement
        // held for holdMs while out of INTERACTION_DISTANCE, then GameObject::Use(me) (button,
        // lever). Same use path as `.partybot usegobject` (PlayerBotMgr.cpp).
        UseObject,
        // Use the item `itemId` from the bags on `target` (empty = self): a potion, healthstone,
        // mana gem (Commander/Classes/ItemKit.h). `spell` is its on-use spell, for the logs and
        // the walk check. Same use path as `.partybot useslot` (CombatBotBaseAI::UseBagItem).
        // The consumables used are off the global cooldown: the original rotation still runs
        // in the same tick.
        UseItem,
        // Cast `spell` (a resurrection: Rebirth in combat) on the dead member `target`: its body
        // here, or its corpse here when it released (PartyBotAI::CastResurrection).
        Resurrect,
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
        // Cast: after a successful cast, stop attacking `target` if it is the bot's victim
        // (a control that breaks on damage: the bot's own next swing or shot would end it).
        bool dropTarget = false;
        // Cast: at the ground point x/y/z (a ground area spell: Blizzard, Rain of Fire), not at
        // a unit; `target` is not used (doc .kiro/docs/2026-11-12-mage-hold.md 3.2).
        bool atPoint = false;
        // Cast: turn to this orientation (radians) first, the spell goes the way the bot faces
        // (Blink); < 0 = no turn.
        float facing = -1.0f;
        // Cast with `facing`, an instant spell: while the bot runs a point walk, jump and turn in
        // the air (JumpCast.h: no stop); otherwise stop and turn.
        bool jumpTurn = false;
        // Cast: put this on itself first, in the same tick (PartyBotAI::CastPrepSpell): an instant
        // off the global cooldown that changes `spell` (Inner Focus, Divine Favor, Nature's
        // Swiftness before a heal; Cold Blood, Presence of Mind, Elemental Mastery before a hit;
        // cooldown doc .kiro/docs/2026-11-14-cooldowns.md 7). Not cast when `spell` cannot be.
        SpellEntry const* prepSpell = nullptr;
        // `prepSpell` ends `spell`'s cooldown (Cold Snap before Ice Block, Preparation before
        // Vanish / Evasion; ClassKit::CooldownReset): `spell` is checked after it, not before.
        bool prepResets = false;
        // UseItem: the item entry.
        uint32 itemId = 0;
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
    // Only decisions the rotation still makes in other situations are leased here. Decisions the
    // Commander owns completely have no lease: the rotation's version is deleted (taunts,
    // escapes, interrupts, threat drops, dispels in combat, crowd control, survival spells,
    // damage cooldowns, party boosts: 2026-11-14 hands layer cleanup).
    enum class Takeover : uint8
    {
        // Heal target, rank, Power Word: Shield and heal boosts of a healer while the party
        // fights (HealPlanBehavior); out of a fight the rotation still heals.
        Healing,
        // A rogue's own stealth while it is the Sap controller of a pull (CrowdControlBehavior,
        // Sap doc .kiro/docs/2026-11-08-sap.md): the rotation's "go into / cancel stealth"
        // (ClassRotation::KeepStealth) stands down.
        Stealth,
        // While the party fights (WorldModel::InFight), where a bot stands: following the
        // leader and the original chase (CombatBotBaseAI::BeginChasing: 25 yd for casters and
        // ranged weapon users, melee behind its victim). The stance module of the bot's role
        // places it instead (PartyBotAI::IsStanceControlled): HealerStanceBehavior (healers),
        // RangedStanceBehavior (ranged damage dealers), MeleeStanceBehavior (melee damage
        // dealers), TankStanceBehavior (tanks, since 2026-11-14). Was CasterChase (healers and
        // ranged only) until 2026-11-07.
        Stance,
        Count
    };

    // What a veto forbids. Every type has a check in official PartyBot code, listed in
    // .kiro/docs/2026-11-01-ai-teammate-code-structure.md section 6.2.
    enum class VetoType : uint8
    {
        None,

        Heal,       // do not heal `target` this tick (PartyBotAI::FindAndHealInjuredAlly)
        // (GapCloser, "no charging while the pull brings the pack back", became the plan
        // constraint TeamPlan::ForbidsGapCloser on 2026-11-07, directive types doc.)
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
        // Part of `cost` the arbiter added: what executing it ends of the bot's running cast,
        // channel or walk (Behaviors/CurrentAction.h). 0 = ends nothing.
        float interruptCost = 0.0f;
        char const* interrupts = "";  // what it ends (static text), "" = nothing

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
