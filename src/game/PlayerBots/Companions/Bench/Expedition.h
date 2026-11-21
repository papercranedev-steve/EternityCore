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

        // What a parcel is worth (bench doc §3.5), by the owner's level when it is sent, at rate 1:
        //  - training: the companion's own class training for the levels it grows on the bench
        //    in that time (it levels with 40% of the owner's experience; simulated over a
        //    leveling run), so the owner pays its trainer without losing money. Times the
        //    experience rate (faster levels: fewer parcels per level). Not diminished.
        //  - profit: 10% of the owner's income over EVERY_SECS (5% at the max level), times
        //    (money rate + 4) / 6 (the money rate scales mob coin and quest money, about a
        //    third of what a player makes), give or take PROFIT_SPREAD, times BENCH_SHARE of
        //    the companion's place among the owner's benched ones (by recruit time).
        struct Worth
        {
            uint32 fromLevel;
            uint32 training;    // copper
            uint32 profit;      // copper
        };
        Worth const WORTH[] = {
            {  1,     0,  1000 },
            { 10,   500,  2500 },
            { 20,  2700,  6700 },
            { 30,  7200, 10000 },
            { 40,  7500, 16700 },
            { 50, 10500, 26700 },
            { 60,     0, 25000 },
        };
        float const BENCH_SHARE[] = { 1.0f, 0.8f, 0.6f, 0.4f };    // the last one for the rest
        float const PROFIT_SPREAD = 0.2f;

        struct Parcel
        {
            uint32 money = 0;
            std::vector<std::pair<uint32, uint32>> items;   // item entry, count
        };

        // What a benched companion sends an owner of `ownerLevel`; `benchRank` 0 = his first
        // benched companion (bench doc §3.5; stage B5: money only, materials come with B3).
        Parcel Pack(uint32 ownerLevel, uint32 benchRank);

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
