/*
 * Shared bot facts and hands (PlayerBots/Shared): a mage's portals (Portal: Stormwind ...). A
 * portal spell (SPELL_EFFECT_TRANS_DOOR) puts up a portal gameobject (GAMEOBJECT_TYPE_SPELLCASTER,
 * party only) whose spell teleports whoever of the caster's party clicks it to a fixed spot
 * (spell_target_position). Found by these effects, not by spell ids.
 * Design: .kiro/docs/2026-11-21-companion-talk.md §2 (T4).
 *
 * Map thread of the player.
 */

#ifndef MANGOS_BOTSHARED_PORTALS_H
#define MANGOS_BOTSHARED_PORTALS_H

#include "Common.h"
#include <vector>

class GameObject;
class Player;
class SpellEntry;

namespace BotShared
{
    namespace Portals
    {
        // The spell puts up a party portal.
        bool IsPortalSpell(SpellEntry const* spell);
        // Portal spells the player knows, by spell id.
        std::vector<SpellEntry const*> Known(Player const* player);
        // The reagent one cast takes (Rune of Portals), 0 = none.
        uint32 RuneOf(SpellEntry const* spell);
        // Casts the runes in its bags allow (at least one portal spell known; else 0).
        uint32 CastsFromBags(Player const* player);

        // A portal of a member of `bot`'s group, within `range` of the bot, that leads to where
        // `leader` stands now (he went through it). Null: none.
        GameObject* LeaderWentThrough(Player* bot, Player const* leader, float range);
    }
}

#endif
