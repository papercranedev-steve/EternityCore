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
        Urgent   = 1,   // someone about to die, aggro already lost, standing in a hazard
        Assigned = 2,   // task given to this bot by team assignment
        Routine  = 3,   // nothing more pressing
    };

    enum class BotActionType : uint8
    {
        Cast,       // cast spell on target (empty target = self)
        Attack,     // switch melee/ranged attack to target
        MoveTo,     // move to x/y/z and keep original movement suppressed for holdMs
        Hold,       // do nothing this tick and skip the original rotation (e.g. stop dps near aggro)
    };

    struct BotAction
    {
        BotActionType type = BotActionType::Hold;
        ObjectGuid target;
        SpellEntry const* spell = nullptr;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        uint32 holdMs = 0;
    };

    struct Proposal
    {
        ProposalTier tier = ProposalTier::Routine;
        float score = 0.0f;           // compared only within the same tier
        char const* source = "";      // behavior id, for logs and .partybot ai bot
        BotAction action;
    };
}

#endif
