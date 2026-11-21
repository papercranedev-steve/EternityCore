/*
 * Shared bot facts (PlayerBots/Shared): where the Artisan rank of each primary profession is
 * taught (the trainers whose lessons have it, Shared/Professions::Teaches), across the world,
 * and the trip quest the player gets when he takes a companion there (Shared/QuestMirror trip
 * quest TripId(skill): "take your companion to an Artisan <skill> trainer").
 * Design: .kiro/docs/2026-11-21-companion-professions.md stage 5 (workspace root).
 */

#ifndef MANGOS_BOTSHARED_ARTISAN_TRAINERS_H
#define MANGOS_BOTSHARED_ARTISAN_TRAINERS_H

#include "Common.h"
#include <string>
#include <vector>

class Player;

namespace BotShared
{
    namespace ArtisanTrainers
    {
        struct Spawn
        {
            uint32 dbGuid = 0;
            uint32 entry = 0;
            uint32 mapId = 0;
            float x = 0.0f, y = 0.0f, z = 0.0f;
        };

        // The Artisan rank of `skill` (Professions::RanksOf, maximum 300): its spell (0 = none),
        // the trainer's price and requirements.
        uint32 RankSpell(uint32 skill);
        uint32 RankCost(uint32 skill);
        uint32 RankReqSkill(uint32 skill);
        uint32 RankReqLevel(uint32 skill);

        // Its trainers not hostile to `player`; on `mapId` first (nearest to x, y), then the rest.
        std::vector<Spawn> For(Player const* player, uint32 skill, uint32 mapId, float x, float y);

        // The trainer's name in a client language (DB locale index, -1 = base).
        std::string NameOf(uint32 entry, int localeIdx);

        // The trip quest of `skill`: QuestMirror::TripId(skill).
        uint32 QuestOf(uint32 skill);

        // Startup, after QuestMirror::BuildAll: the trip quest of every primary profession with
        // an Artisan rank. Returns how many it made.
        uint32 BuildQuests();
    }
}

#endif
