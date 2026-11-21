/*
 * Shared bot facts and hands (PlayerBots/Shared): professions by the profession trainers' own
 * data. Ranks (Apprentice .. Artisan: the spells that set a skill line's maximum) and recipes
 * (the other lessons of a skill), learning them for free, and forgetting every profession.
 *
 * Who gets which profession and at what skill is the caller's (Companions/PastProfessions).
 * Design: .kiro/docs/2026-11-21-companion-professions.md (workspace root).
 */

#ifndef MANGOS_BOTSHARED_PROFESSIONS_H
#define MANGOS_BOTSHARED_PROFESSIONS_H

#include "Common.h"
#include <functional>
#include <vector>

class Player;

namespace BotShared
{
    namespace Professions
    {
        // Skill lines (SkillLine.dbc).
        uint32 const ALCHEMY = 171;
        uint32 const BLACKSMITHING = 164;
        uint32 const ENCHANTING = 333;
        uint32 const ENGINEERING = 202;
        uint32 const HERBALISM = 182;
        uint32 const LEATHERWORKING = 165;
        uint32 const MINING = 186;
        uint32 const SKINNING = 393;
        uint32 const TAILORING = 197;
        uint32 const FIRST_AID = 129;
        uint32 const COOKING = 185;
        uint32 const FISHING = 356;

        enum class Kind
        {
            None,
            Crafting,       // makes items (primary)
            Gathering,      // herbs, ore, skins (primary)
            Secondary,      // first aid, cooking, fishing
        };

        Kind KindOf(uint32 skill);
        bool IsProfession(uint32 skill);
        std::vector<uint32> const& Secondaries();

        // A rank a profession trainer teaches: the spell that sets the skill line's maximum.
        struct Rank
        {
            uint32 spellId = 0;
            uint32 maxValue = 0;        // 75 / 150 / 225 / 300
            uint32 reqSkillValue = 0;   // the trainer's requirement
            uint32 reqLevel = 0;
            uint32 cost = 0;            // copper
        };
        // Lowest first; empty for a skill no trainer teaches.
        std::vector<Rank> const& RanksOf(uint32 skill);

        // Free: learns the ranks of `skill` up to the lowest one whose maximum holds `value` (the
        // highest rank there is when none does), whatever the player's level, and sets the skill
        // to `value` (at most that maximum). Returns the maximum, 0 = no trainer teaches it.
        uint32 Grant(Player* player, uint32 skill, uint32 value);

        // Free: every recipe lesson of `skill` the trainers would give now (green in the trainer
        // window: skill value, level, previous rank) and `keep` agrees to, also those that become
        // available on the way. keep(reqSkillValue, taught spell). Returns the recipes learned.
        uint32 LearnRecipes(Player* player, uint32 skill, std::function<bool(uint32 reqSkillValue, uint32 spellId)> const& keep);

        // Forgets every profession: its spells (ranks, recipes, Find Herbs ...) and skill line,
        // and gives the primary profession slots back.
        void ForgetAll(Player* player);
    }
}

#endif
