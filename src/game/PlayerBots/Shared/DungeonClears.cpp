#include "Shared/DungeonClears.h"
#include "Creature.h"
#include "Map.h"
#include "Log.h"
#include <atomic>

namespace BotShared
{
    namespace DungeonClears
    {
        namespace
        {
            std::vector<Info> const g_segments = {
                { Segment::StratholmeLive,   329, 10813, false, "Stratholme (Live)" },        // Balnazzar
                { Segment::StratholmeUndead, 329, 10440, false, "Stratholme (Undead)" },      // Baron Rivendare
                { Segment::Scholomance,      289,  1853, false, "Scholomance" },              // Darkmaster Gandling
                { Segment::LowerBlackrock,   229,  9568, false, "Lower Blackrock Spire" },    // Overlord Wyrmthalak
                { Segment::UpperBlackrock,   229, 10363, false, "Upper Blackrock Spire" },    // General Drakkisath
                { Segment::DireMaulEast,     429, 11492, false, "Dire Maul (East)" },         // Alzzin the Wildshaper
                { Segment::DireMaulWest,     429, 11486, false, "Dire Maul (West)" },         // Prince Tortheldrin
                { Segment::DireMaulNorth,    429, 11501, false, "Dire Maul (North)" },        // King Gordok
                { Segment::MoltenCore,       409, 11502, true,  "Molten Core" },              // Ragnaros
                { Segment::Onyxia,           249, 10184, true,  "Onyxia's Lair" },
                { Segment::ZulGurub,         309, 14834, true,  "Zul'Gurub" },                // Hakkar
                { Segment::BlackwingLair,    469, 11583, true,  "Blackwing Lair" },           // Nefarian
                { Segment::RuinsOfAhnQiraj,  509, 15339, true,  "Ruins of Ahn'Qiraj" },       // Ossirian the Unscarred
                { Segment::TempleOfAhnQiraj, 531, 15727, true,  "Temple of Ahn'Qiraj" },      // C'Thun
                { Segment::Naxxramas,        533, 15990, true,  "Naxxramas" },                // Kel'Thuzad
            };

            // Levelling dungeons (and Blackrock Depths): map, last boss (entries checked in the
            // mangos DB, 2026-11-21). A dungeon of wings or without a fixed order has several.
            struct End
            {
                uint32 mapId;
                uint32 lastBoss;
            };
            End const g_levellingEnds[] = {
                { 389, 11520 },     // Ragefire Chasm: Taragaman the Hungerer
                { 389, 11519 },     //                 Bazzalan
                {  43,  3654 },     // Wailing Caverns: Mutanus the Devourer
                {  36,   639 },     // The Deadmines: Edwin VanCleef
                {  33,  4275 },     // Shadowfang Keep: Archmage Arugal
                {  34,  1716 },     // The Stockade: Bazil Thredd
                {  48,  4829 },     // Blackfathom Deeps: Aku'mai
                {  90,  7800 },     // Gnomeregan: Mekgineer Thermaplugg
                {  47,  4421 },     // Razorfen Kraul: Charlga Razorflank
                { 189,  4543 },     // Scarlet Monastery, Graveyard: Bloodmage Thalnos
                { 189,  6487 },     //                    Library: Arcanist Doan
                { 189,  3975 },     //                    Armory: Herod
                { 189,  3977 },     //                    Cathedral: High Inquisitor Whitemane
                { 129,  7358 },     // Razorfen Downs: Amnennar the Coldbringer
                {  70,  2748 },     // Uldaman: Archaedas
                { 209,  7267 },     // Zul'Farrak: Chief Ukorz Sandscalp
                { 349, 12201 },     // Maraudon: Princess Theradras
                { 109,  5709 },     // The Temple of Atal'Hakkar: Shade of Eranikus
                { 230,  9019 },     // Blackrock Depths: Emperor Dagran Thaurissan
            };

            std::atomic<Listener> g_listener{ nullptr };
            std::atomic<EndListener> g_endListener{ nullptr };
        }

        bool IsDungeonEnd(uint32 mapId, uint32 creatureEntry)
        {
            for (End const& end : g_levellingEnds)
                if (end.lastBoss == creatureEntry && end.mapId == mapId)
                    return true;
            return OfLastBoss(mapId, creatureEntry) != nullptr;
        }

        void SetEndListener(EndListener listener)
        {
            g_endListener.store(listener);
        }

        std::vector<Info> const& All()
        {
            return g_segments;
        }

        Info const* OfLastBoss(uint32 mapId, uint32 creatureEntry)
        {
            for (Info const& info : g_segments)
                if (info.lastBoss == creatureEntry && info.mapId == mapId)
                    return &info;
            return nullptr;
        }

        void SetListener(Listener listener)
        {
            g_listener.store(listener);
        }

        void OnCreatureDeath(Creature* victim)
        {
            if (!victim || !victim->IsInWorld())
                return;
            if (IsDungeonEnd(victim->GetMapId(), victim->GetEntry()))
                if (EndListener const endListener = g_endListener.load())
                    endListener(victim->GetMap());
            Info const* info = OfLastBoss(victim->GetMapId(), victim->GetEntry());
            if (!info)
                return;
            sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[DungeonClears] %s cleared (instance %u).", info->name, victim->GetInstanceId());
            if (Listener const listener = g_listener.load())
                listener(victim->GetMap(), *info);
        }
    }
}
