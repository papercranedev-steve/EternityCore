#include "Companions/PastProfessions.h"
#include "Companions/PastRoll.h"
#include "Shared/Professions.h"
#include "Shared/Gathering.h"
#include "Player.h"
#include "Item.h"
#include "Log.h"
#include <algorithm>

namespace Companions
{
    namespace PastProfessions
    {
        namespace
        {
            namespace P = BotShared::Professions;

            uint32 const NATURAL_PER_LEVEL = 5;
            uint32 const NATURAL_MAX = 300;
            int32 const BEHIND = 75;            // one tier
            int32 const SPREAD = 15;
            uint32 const RECRUIT_MAX = 225;     // never Artisan: the player takes it there
            uint32 const RECENT_RECIPE = 25;    // recipes this close to its skill may be missing
            float const RECENT_MISSING = 0.5f;
            uint32 const BASIC_RECIPE = 1;      // the Apprentice basics are always known (user 2026-11-21)
            uint32 const GATHERER_FACTOR = 3;

            // Roll keys: the high bit (class quest chains use quest ids).
            uint32 const KEY_PAIR = 0x80000001;
            uint32 const KEY_VALUE = 0x80001000;    // + skill line
            uint32 const KEY_RECIPE = 0x80100000;   // + recipe spell

            enum Armor { CLOTH, LEATHER, MAIL, PLATE, ARMOR_COUNT };

            struct Pair
            {
                uint32 first;
                uint32 second;
                uint32 weight[ARMOR_COUNT];
            };

            // Design §2.1.
            Pair const g_pairs[] = {
                { P::TAILORING,  P::ENCHANTING,     { 4, 1, 0, 0 } },
                { P::HERBALISM,  P::ALCHEMY,        { 3, 2, 2, 1 } },
                { P::SKINNING,   P::LEATHERWORKING, { 0, 4, 4, 0 } },
                { P::MINING,     P::BLACKSMITHING,  { 0, 0, 0, 4 } },
                { P::MINING,     P::ENGINEERING,    { 1, 1, 2, 2 } },
                { P::HERBALISM,  P::MINING,         { 1, 1, 1, 1 } },
                { P::HERBALISM,  P::SKINNING,       { 1, 2, 1, 0 } },
                { P::MINING,     P::SKINNING,       { 0, 0, 1, 1 } },
            };

            Armor ArmorOf(uint8 classId)
            {
                switch (classId)
                {
                    case CLASS_ROGUE: case CLASS_DRUID: return LEATHER;
                    case CLASS_HUNTER: case CLASS_SHAMAN: return MAIL;
                    case CLASS_WARRIOR: case CLASS_PALADIN: return PLATE;
                    default: return CLOTH;
                }
            }

            bool BothGathering(Pair const& pair)
            {
                return P::KindOf(pair.first) == P::Kind::Gathering && P::KindOf(pair.second) == P::Kind::Gathering;
            }

            Pair const& PickPair(Player const* bot, std::vector<std::string> const& tags)
            {
                bool const gatherer = std::find(tags.begin(), tags.end(), "gatherer") != tags.end();
                Armor const armor = ArmorOf(bot->GetClass());
                auto weightOf = [&](Pair const& pair)
                {
                    uint32 const w = pair.weight[armor];
                    return gatherer && BothGathering(pair) ? w * GATHERER_FACTOR : w;
                };

                uint32 total = 0;
                for (Pair const& pair : g_pairs)
                    total += weightOf(pair);
                float pick = PastRoll(bot->GetGUIDLow(), KEY_PAIR) * float(total);
                for (Pair const& pair : g_pairs)
                {
                    pick -= float(weightOf(pair));
                    if (pick < 0.0f)
                        return pair;
                }
                return g_pairs[0];
            }

            // One profession: ranks, value, recipes. Returns the skill value it got (0 = none).
            uint32 GiveOne(Player* bot, uint32 skill, uint32& recipes)
            {
                uint32 const guid = bot->GetGUIDLow();
                uint32 const value = RecruitValue(bot->GetLevel(), PastRoll(guid, KEY_VALUE + skill));
                if (!P::Grant(bot, skill, value))
                    return 0;
                uint32 const has = bot->GetSkillValuePure(uint16(skill));
                recipes += P::LearnRecipes(bot, skill, [guid, has](uint32 reqSkillValue, uint32 spellId)
                {
                    return reqSkillValue <= BASIC_RECIPE || reqSkillValue + RECENT_RECIPE <= has ||
                           PastRoll(guid, KEY_RECIPE + spellId) >= RECENT_MISSING;
                });
                return has;
            }

            // The basic tool of a gathering profession (Mining Pick, Skinning Knife), when it has none.
            void GiveTool(Player* bot, uint32 skill)
            {
                uint32 const tool = BotShared::Gathering::ToolOf(skill);
                if (!tool || BotShared::Gathering::HasTool(bot, skill))
                    return;
                ItemPosCountVec dest;
                if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, tool, 1) != EQUIP_ERR_OK)
                {
                    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Companion] '%s': no room for the tool %u of skill %u.", bot->GetName(), tool, skill);
                    return;
                }
                bot->StoreNewItem(dest, tool, true, Item::GenerateItemRandomPropertyId(tool));
            }
        }

        uint32 RecruitValue(uint32 level, float roll)
        {
            int32 const natural = int32(std::min(level * NATURAL_PER_LEVEL, NATURAL_MAX));
            int32 const spread = int32(roll * float(2 * SPREAD + 1)) - SPREAD;     // -15..+15
            int32 const value = natural - BEHIND + std::min(spread, SPREAD);
            return uint32(std::max(1, std::min(value, int32(RECRUIT_MAX))));
        }

        Result Decide(Player* bot, std::vector<std::string> const& tags)
        {
            Result result;
            if (!bot)
                return result;

            P::ForgetAll(bot);
            Pair const& pair = PickPair(bot, tags);
            result.first = pair.first;
            result.second = pair.second;
            result.firstValue = GiveOne(bot, pair.first, result.recipes);
            result.secondValue = GiveOne(bot, pair.second, result.recipes);
            for (uint32 skill : { pair.first, pair.second })
                if (P::KindOf(skill) == P::Kind::Gathering && bot->HasSkill(uint16(skill)))
                    GiveTool(bot, skill);
            for (uint32 skill : P::Secondaries())
                GiveOne(bot, skill, result.recipes);
            return result;
        }
    }
}
