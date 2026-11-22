#include "Shared/QuestRules.h"
#include "Player.h"
#include "QuestDef.h"
#include <cstring>

namespace BotShared
{
namespace QuestRules
{
    char const* WhyNot(Player const* player, Quest const* quest)
    {
        if (!quest->IsRepeatable() && player->GetQuestRewardStatus(quest->GetQuestId()))
            return "done";
        if (!player->SatisfyQuestStatus(quest, false))
            return "have";
        if ((quest->GetMaxLevel() && quest->GetMaxLevel() < player->GetLevel()) || !player->SatisfyQuestLevel(quest, false))
            return "level";
        if (!player->SatisfyQuestClass(quest, false))
            return "class";
        if (!player->SatisfyQuestRace(quest, false))
            return "race";
        if (!player->SatisfyQuestPreviousQuest(quest, false) || !player->SatisfyQuestPrevChain(quest, false) ||
            !player->SatisfyQuestNextChain(quest, false) || !player->SatisfyQuestExclusiveGroup(quest, false) ||
            !player->SatisfyQuestBreadcrumbQuest(quest, false) || !player->SatisfyQuestDependentBreadcrumbQuests(quest, false))
            return "prev";
        if (!player->SatisfyQuestSkill(quest, false))
            return "skill";
        if (!player->SatisfyQuestReputation(quest, false))
            return "rep";
        if (!player->CanTakeQuest(quest, false))
            return "other";
        if (!player->SatisfyQuestLog(false))
            return "logfull";
        if (!player->CanAddQuest(quest, false))
            return "bagsfull";
        return nullptr;
    }

    bool Barred(char const* whyNot)
    {
        return whyNot && std::strcmp(whyNot, "have") != 0 && std::strcmp(whyNot, "logfull") != 0 &&
               std::strcmp(whyNot, "bagsfull") != 0;
    }
}
}
