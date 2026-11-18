#include "Shared/Mailbox.h"
#include "Player.h"
#include "GameObject.h"
#include "ObjectMgr.h"
#include "MasterPlayer.h"
#include "Mail.h"
#include "WorldSession.h"
#include <algorithm>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>

namespace BotShared
{
namespace Mailbox
{
    namespace
    {
        std::mutex s_lock;
        std::map<uint32, std::shared_ptr<std::vector<Spawn> const>> s_byMap;

        std::shared_ptr<std::vector<Spawn> const> SpawnsOf(uint32 mapId)
        {
            std::lock_guard<std::mutex> guard(s_lock);
            auto& slot = s_byMap[mapId];
            if (slot)
                return slot;

            auto spawns = std::make_shared<std::vector<Spawn>>();
            auto collect = [mapId, &spawns](GameObjectDataPair const& pair)
            {
                GameObjectData const& d = pair.second;
                if (d.position.mapId != mapId || (d.spawn_flags & SPAWN_FLAG_DISABLED))
                    return false;
                GameObjectInfo const* info = sObjectMgr.GetGameObjectTemplate(d.id);
                if (!info || info->type != GAMEOBJECT_TYPE_MAILBOX)
                    return false;
                Spawn s;
                s.dbGuid = pair.first;
                s.entry = d.id;
                s.x = d.position.x;
                s.y = d.position.y;
                s.z = d.position.z;
                spawns->push_back(s);
                return false;
            };
            sObjectMgr.DoGOData(collect);
            slot = spawns;
            return slot;
        }

        bool Takeable(Mail const* m, time_t now)
        {
            return m && m->state != MAIL_STATE_DELETED && m->deliver_time <= now && !m->COD &&
                   (m->HasItems() || m->money);
        }
    }

    std::vector<Spawn> Near(uint32 mapId, float x, float y, float range)
    {
        std::vector<std::pair<float, Spawn>> found;
        for (Spawn const& s : *SpawnsOf(mapId))
        {
            float const dx = s.x - x;
            float const dy = s.y - y;
            float const d = dx * dx + dy * dy;
            if (d <= range * range)
                found.emplace_back(d, s);
        }
        std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        std::vector<Spawn> out;
        for (auto const& f : found)
            out.push_back(f.second);
        return out;
    }

    ObjectGuid GuidOf(Spawn const& spawn)
    {
        return ObjectGuid(HIGHGUID_GAMEOBJECT, spawn.entry, spawn.dbGuid);
    }

    bool HasMailToTake(Player* player)
    {
        MasterPlayer* master = player->GetSession() ? player->GetSession()->GetMasterPlayer() : nullptr;
        if (!master)
            return false;
        time_t const now = time(nullptr);
        for (auto itr = master->GetMailBegin(); itr != master->GetMailEnd(); ++itr)
            if (Takeable(*itr, now))
                return true;
        return false;
    }

    uint32 TakeAll(Player* player, GameObject* mailbox)
    {
        MasterPlayer* master = player->GetSession() ? player->GetSession()->GetMasterPlayer() : nullptr;
        if (!master || !mailbox)
            return 0;
        time_t const now = time(nullptr);
        uint32 queued = 0;
        for (auto itr = master->GetMailBegin(); itr != master->GetMailEnd(); ++itr)
        {
            Mail const* m = *itr;
            if (!Takeable(m, now))
                continue;
            // (the handler takes the first item each time)
            for (size_t i = 0; i < m->items.size(); ++i)
            {
                auto packet = std::make_unique<WorldPackets::Mail::MailTakeItem>();
                packet->mailboxGuid = mailbox->GetObjectGuid();
                packet->mailId = m->messageID;
                player->GetSession()->QueuePacket(std::move(packet));
                ++queued;
            }
            if (m->money)
            {
                auto packet = std::make_unique<WorldPackets::Mail::MailTakeMoney>();
                packet->mailboxGuid = mailbox->GetObjectGuid();
                packet->mailId = m->messageID;
                player->GetSession()->QueuePacket(std::move(packet));
                ++queued;
            }
        }
        return queued;
    }
}
}
