#include "Companions/Recruit/FirstCompanion.h"
#include "Companions/CompanionMgr.h"
#include "Population/PopulationMgr.h"
#include "Population/PopulationIdentity.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Log.h"
#include "Utilities/Random.h"
#include <mutex>
#include <string>
#include <vector>

namespace Companions
{
namespace FirstCompanion
{
    namespace
    {
        struct Accepted
        {
            ObjectGuid player;
            std::string giverName;
        };
        std::mutex g_lock;
        std::vector<Accepted> g_accepted;

        bool Heals(uint8 classId)
        {
            return classId == CLASS_PRIEST || classId == CLASS_PALADIN || classId == CLASS_DRUID || classId == CLASS_SHAMAN;
        }
    }

    void OnQuestAccepted(Player* player, Quest const* /*quest*/, Object* giver)
    {
        if (!player || player->GetLevel() > GIFT_MAX_LEVEL || !sCompanionMgr.IsEnabled())
            return;
        Accepted a;
        a.player = player->GetObjectGuid();
        if (giver && giver->IsType(TYPEMASK_UNIT))
            a.giverName = static_cast<Unit*>(giver)->GetName();
        std::lock_guard<std::mutex> guard(g_lock);
        for (Accepted const& other : g_accepted)
            if (other.player == a.player)
                return;
        g_accepted.push_back(std::move(a));
    }

    uint8 ClassFor(Player const* player)
    {
        // A fighter gets a healer (by the race's own healers), a healer a warrior.
        std::vector<uint8> candidates;
        if (Heals(player->GetClass()))
            candidates = { CLASS_WARRIOR };
        else
            for (uint8 classId : { CLASS_PRIEST, CLASS_PALADIN, CLASS_DRUID, CLASS_SHAMAN })
                if (Population::Identity::IsValidCombo(player->GetRace(), classId))
                    candidates.push_back(classId);
        if (candidates.empty())
            return 0;
        return candidates[urand(0, uint32(candidates.size()) - 1)];
    }

    void Update()
    {
        std::vector<Accepted> accepted;
        {
            std::lock_guard<std::mutex> guard(g_lock);
            if (g_accepted.empty())
                return;
            accepted.swap(g_accepted);
        }
        for (Accepted const& a : accepted)
        {
            Player* player = sObjectAccessor.FindPlayer(a.player);
            if (!player || !player->IsInWorld() || player->IsBot() || player->GetLevel() > GIFT_MAX_LEVEL)
                continue;
            uint32 const guid = player->GetGUIDLow();
            if (sCompanionMgr.FirstOffered(guid) || !sCompanionMgr.OfRecruiter(guid).empty())
                continue;
            uint8 const classId = ClassFor(player);
            // (no hidden spot right now, population off ...: his next quest tries again)
            if (!classId || !sPopulationMgr.SpawnHerald(player, classId, a.giverName))
                continue;
            sCompanionMgr.MarkFirstOffered(guid);
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] %s took a quest: his first companion (class %u) comes to him.",
                player->GetName(), uint32(classId));
        }
    }
}
}
