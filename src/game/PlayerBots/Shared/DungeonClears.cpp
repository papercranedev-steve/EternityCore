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

            std::atomic<Listener> g_listener{ nullptr };
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
            Info const* info = OfLastBoss(victim->GetMapId(), victim->GetEntry());
            if (!info)
                return;
            sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[DungeonClears] %s cleared (instance %u).", info->name, victim->GetInstanceId());
            if (Listener const listener = g_listener.load())
                listener(victim->GetMap(), *info);
        }
    }
}
