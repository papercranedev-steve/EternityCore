#include "Shared/TalentSpread.h"
#include "Player.h"
#include "DBCStores.h"
#include <functional>

namespace BotShared
{
    namespace TalentSpread
    {
        namespace
        {
            Points Count(uint32 classMask, std::function<bool(uint32)> const& hasSpell)
            {
                Points points = { 0, 0, 0 };
                for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
                {
                    TalentEntry const* talent = sTalentStore.LookupEntry(i);
                    if (!talent)
                        continue;
                    TalentTabEntry const* tab = sTalentTabStore.LookupEntry(talent->TalentTab);
                    if (!tab || !(classMask & tab->ClassMask) || tab->tabpage >= points.size())
                        continue;
                    // the highest learned rank is the points spent on it
                    for (int32 k = MAX_TALENT_RANK - 1; k >= 0; --k)
                        if (talent->RankID[k] && hasSpell(talent->RankID[k]))
                        {
                            points[tab->tabpage] += uint32(k + 1);
                            break;
                        }
                }
                return points;
            }
        }

        Points Of(Player const* player)
        {
            if (!player)
                return { 0, 0, 0 };
            return Count(player->GetClassMask(), [player](uint32 spellId) { return player->HasSpell(spellId); });
        }

        Points Of(uint8 classId, std::unordered_set<uint32> const& spells)
        {
            if (!classId)
                return { 0, 0, 0 };
            return Count(1u << (classId - 1), [&spells](uint32 spellId) { return spells.count(spellId) != 0; });
        }
    }
}
