/*
 * Companions on the bench go out on their own and mail the player what they brought back:
 * one parcel every EVERY_SECS of the player's active time (online, not away), at most
 * MAX_UNOPENED of his not yet opened at a time (user 2026-11-21).
 * Design: .kiro/docs/2026-11-21-companion-bench.md §3 (workspace root).
 *
 * The parcel is PARCEL_ITEM, a lootable item the client knows that has no loot of its own.
 * What is in it is decided when it is sent and kept in `custom_companion_parcel` by item guid:
 * the core forgets the loot of items still in the mailbox at login (Player::_LoadItemLoot), so
 * FillParcel puts it in when it is opened (LootMgr.h AddItemLootFiller) and drops the row.
 * Other items of that entry (no row) open as usual.
 *
 * Pack, Send, Unopened: world thread (CompanionMgr::UpdateBench). FillParcel: map threads.
 */

#ifndef MANGOS_COMPANIONS_EXPEDITION_H
#define MANGOS_COMPANIONS_EXPEDITION_H

#include "Common.h"
#include <string>
#include <utility>
#include <vector>

class Item;
class Player;
struct Loot;

namespace Companions
{
    namespace Expedition
    {
        // Small Brown-wrapped Package (棕色小包).
        uint32 const PARCEL_ITEM = 15699;
        uint32 const EVERY_SECS = 2 * HOUR;
        uint32 const MAX_UNOPENED = 2;

        // Money: what MOBS_WORTH normal mobs of its level drop, give or take MONEY_SPREAD. A
        // normal mob of level L drops about MONEY_PER_LEVEL_SQ * L² copper (world database
        // average of creature_template gold: 12c at 10, 108c at 30, 381c at 58).
        float const MOBS_WORTH = 40.0f;
        float const MONEY_PER_LEVEL_SQ = 0.11f;
        float const MONEY_SPREAD = 0.3f;

        struct Parcel
        {
            uint32 money = 0;
            std::vector<std::pair<uint32, uint32>> items;   // item entry, count
        };

        // What a companion of `level` brings back (bench doc §3.3; stage B2: money only).
        Parcel Pack(uint32 level);

        // Mails `parcel` from the companion to `owner` (online), arriving at once. False: not sent.
        bool Send(Player* owner, uint32 companionGuid, Parcel const& parcel, std::string const& subject, std::string const& body);

        // Its parcels `ownerGuid` still has unopened (in his mailbox or bags).
        uint32 Unopened(uint32 companionGuid, uint32 ownerGuid);

        // custom_companion_parcel; forgets rows whose parcel is gone. Registers FillParcel once.
        void Load();

        bool FillParcel(Item* item, Player* opener, Loot& loot);
    }
}

#endif
