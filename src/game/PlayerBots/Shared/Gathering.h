/*
 * Shared bot facts (PlayerBots/Shared): gathering, by the server's own rules. Herb and mining
 * nodes are chests with a skill lock of the herbalism or mining lock type; a corpse is
 * skinnable when it has UNIT_FLAG_SKINNABLE, all its loot is gone and the tap rules allow
 * (Spell::CheckCast SPELL_EFFECT_SKINNING). The tool is the gathering spell's Totem (Mining
 * Pick, Skinning Knife; Herb Gathering needs none), from the spell data.
 * Design: .kiro/docs/2026-11-21-companion-professions.md stage 4 (workspace root).
 */

#ifndef MANGOS_BOTSHARED_GATHERING_H
#define MANGOS_BOTSHARED_GATHERING_H

#include "Common.h"

class Creature;
class GameObject;
class Player;
class SpellEntry;

namespace BotShared
{
    namespace Gathering
    {
        // The item a gathering skill's spell needs in the bags, 0 = none (or not a gathering skill).
        uint32 ToolOf(uint32 skill);
        // Has the tool, or needs none.
        bool HasTool(Player const* player, uint32 skill);

        struct Node
        {
            uint32 skill = 0;       // 0 = not a gathering node
            uint32 reqValue = 0;    // the lock's skill requirement
        };
        // A herb or ore node (spawned, ready to open); skill 0 otherwise (a plain chest too).
        Node NodeOf(GameObject const* go);

        // The skill Skinning `corpse` needs (Spell::CheckCast's formula, by the skinner's skill).
        uint32 SkinningReq(Creature const* corpse, uint32 skinnerSkill);
        // `player` may skin it now: skinnable, looted, its turn, skill and tool.
        bool CanSkin(Player const* player, Creature const* corpse);

        // The gathering spell of the skill the player knows (any rank: the skill decides), null = none.
        SpellEntry const* SpellOf(Player const* player, uint32 skill);
    }
}

#endif
