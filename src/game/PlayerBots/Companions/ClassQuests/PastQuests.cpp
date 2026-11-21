#include "Companions/ClassQuests/PastQuests.h"
#include "Companions/PastRoll.h"
#include "Shared/ClassQuestAbilities.h"
#include "Shared/ClassQuestChains.h"
#include "Shared/QuestGrant.h"
#include "Player.h"
#include "QuestDef.h"
#include "Formulas.h"
#include <algorithm>

namespace Companions
{
    namespace
    {
        // Levels above the quest from which it counts as done whatever the roll.
        int32 const ALWAYS_DONE_ABOVE = 10;
        float const CHANCE_PER_LEVEL = 0.05f;    // up to 5 levels
        float const CHANCE_PER_LOST_XP = 0.5f;   // times the share of the quest's XP no longer given

        // XPValue's falloff as a share, for quests without XP too.
        float XpShare(uint32 level, Quest const* quest, int32 above)
        {
            if (quest->GetRewXP() > 0)
                return float(quest->XPValue(level)) / float(quest->GetRewXP());
            if (above <= 5)
                return 1.0f;
            if (above < ALWAYS_DONE_ABOVE)
                return 1.0f - 0.2f * float(above - 5);
            return 0.1f;
        }

        bool HasAnyOf(Player const* bot, BotShared::ClassQuestAbilities::Ability const& ability)
        {
            for (uint32 spellId : ability.spells)
                if (bot->HasSpell(spellId))
                    return true;
            for (uint32 tool : ability.tools)
                if (bot->GetItemCount(tool, true))
                    return true;
            return false;
        }
    }

    namespace PastQuests
    {
        float ChanceDone(uint32 level, Quest const* quest)
        {
            if (!quest)
                return 0.0f;
            // QuestLevel 0 or -1 (stored unsigned): the quest takes the player's level; its minimum
            // level stands in.
            uint32 const rawLevel = quest->GetQuestLevel();
            int32 const questLevel = rawLevel > 0 && rawLevel <= PLAYER_MAX_LEVEL ? int32(rawLevel) : int32(quest->GetMinLevel());
            int32 const above = int32(level) - questLevel;
            if (above <= 0)
                return 0.0f;
            if (above >= ALWAYS_DONE_ABOVE || questLevel <= int32(MaNGOS::XP::GetGrayLevel(level)))
                return 1.0f;
            float const chance = CHANCE_PER_LEVEL * float(std::min(above, 5)) +
                CHANCE_PER_LOST_XP * (1.0f - XpShare(level, quest, above));
            return std::min(1.0f, std::max(0.0f, chance));
        }

        Result Decide(Player* bot)
        {
            Result result;
            if (!bot)
                return result;

            namespace Chains = BotShared::ClassQuestChains;
            namespace Abilities = BotShared::ClassQuestAbilities;

            // Each chain as a whole, rolled on its first quest.
            for (Chains::Chain const* chain : Chains::ChainsFor(bot))
            {
                if (Chains::IsDone(bot, *chain))
                {
                    ++result.done;
                    continue;
                }
                std::vector<Quest const*> const steps = Chains::StepsFor(bot, *chain);
                Quest const* first = steps.front();
                if (bot->GetLevel() < first->GetMinLevel())
                    continue;
                // After an ability chain (decided before it, All() order): only if that one is done.
                if (chain->afterAbility)
                {
                    std::vector<Quest const*> const before = BotShared::QuestGrant::ChainUpTo(bot, first);
                    if (before.size() > 1 && !bot->GetQuestRewardStatus(before[before.size() - 2]->GetQuestId()))
                        continue;
                }
                if (PastRoll(bot->GetGUIDLow(), chain->id) >= ChanceDone(bot->GetLevel(), first))
                {
                    ++result.notDone;
                    continue;
                }

                // Done: in its history. Abilities with what they teach and give (totems); gear
                // chains without their items (long since replaced or sold).
                Chains::GrantDone(bot, *chain);
                ++result.done;
            }

            // What it has without having done the quest for it (premade spec templates).
            for (Abilities::Ability const& ability : Abilities::All())
            {
                if (!(ability.classMask & bot->GetClassMask()) || Abilities::IsEarned(bot, ability) || !HasAnyOf(bot, ability))
                    continue;
                Abilities::Revoke(bot, ability);
                ++result.revoked;
            }
            return result;
        }
    }
}
