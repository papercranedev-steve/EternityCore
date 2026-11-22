/*
 * Shared bot hands (PlayerBots/Shared): use a gameobject the way a client does.
 *
 *  - no lock: a chest is opened for loot (Player::SendLoot), anything else is used through the
 *    use handler (WorldSession::HandleGameObjectUseOpcode: distance, PlayerCanUse, scripts);
 *  - a lock (LockEntry): a key in the bags whose spell opens it (a quest item used on a quest
 *    object is such a key: its spell has SPELL_EFFECT_OPEN_LOCK_ITEM), or a known spell for the
 *    lock's type (Opening, Pick Lock, Herb Gathering, Mining) with enough skill. The spell is
 *    cast normally (cast time, interruptions, skill-ups) and the server opens the object
 *    (Spell::EffectOpenLock: a chest shows its loot, a door or a goober is used).
 *  - a door with a key: the use handler (GameObject::PlayerCanUse checks the key).
 *
 * Map thread of the player.
 */

#ifndef MANGOS_BOTSHARED_OBJECT_USE_H
#define MANGOS_BOTSHARED_OBJECT_USE_H

#include "Common.h"
#include "ObjectGuid.h"

class GameObject;
class Player;
class SpellEntry;

namespace BotShared
{
    namespace ObjectUse
    {
        enum class Way : uint8
        {
            Cannot,     // locked for this player (no key, skill too low, no spell), or not usable
            Use,        // the use handler
            Loot,       // a chest without a lock: open its loot
            Spell,      // cast `spell` (with the key `item` if set) on the object
        };

        struct Plan
        {
            Way way = Way::Cannot;
            SpellEntry const* spell = nullptr;
            ObjectGuid item;
        };

        Plan HowToUse(Player* player, GameObject* go);

        // In interaction distance (GameObject::IsAtInteractDistance). Use / Loot are done when it
        // returns; Spell starts a cast (the object opens when it ends). False: nothing started.
        bool Start(Player* player, GameObject* go, Plan const& plan);
    }
}

#endif
