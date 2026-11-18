/*
 * Shared bot facts and hands (PlayerBots/Shared): mailboxes and the bot's mail. Soul companions
 * doc §4.4 (auction house buys come by mail; a player may send the companion things too).
 *
 * Facts: the mailbox spawns of a map (built once per map on first use, thread safe), what the
 * bot could take now (delivered, not cash on delivery: items and money).
 * Hands: TakeAll queues the client's take packets for every such mail (CMSG_MAIL_TAKE_ITEM per
 * item, CMSG_MAIL_TAKE_MONEY; handled on the world thread with all their checks: the mailbox
 * in reach, bag space). The bot must stand at the mailbox.
 */

#ifndef MANGOS_BOTSHARED_MAILBOX_H
#define MANGOS_BOTSHARED_MAILBOX_H

#include "Common.h"
#include "ObjectGuid.h"
#include <vector>

class GameObject;
class Player;

namespace BotShared
{
    namespace Mailbox
    {
        struct Spawn
        {
            uint32 dbGuid = 0;
            uint32 entry = 0;
            float x = 0.0f, y = 0.0f, z = 0.0f;
        };

        // Mailbox spawns on `mapId` within `range` (2d) of x, y; nearest first.
        std::vector<Spawn> Near(uint32 mapId, float x, float y, float range);
        ObjectGuid GuidOf(Spawn const& spawn);

        // Mail the player could take now: delivered, no cash on delivery, items or money in it.
        bool HasMailToTake(Player* player);
        // Queues taking all of it at `mailbox`. Returns how many take packets were queued.
        uint32 TakeAll(Player* player, GameObject* mailbox);
    }
}

#endif
