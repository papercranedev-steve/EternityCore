#include "Shared/Gaze.h"
#include "Shared/AddonChannel.h"
#include "Player.h"
#include "Timer.h"
#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace BotShared
{
namespace Gaze
{
    namespace
    {
        // What a player's client last said is on his screen (all threads).
        struct Screen
        {
            uint32 atMs = 0;
            std::vector<uint32> lows;   // sorted
        };

        std::mutex g_lock;
        std::unordered_map<uint32, Screen> g_screens;   // viewer guid low -> his screen

        bool OnScreen(Player const* viewer, WorldObject const* target, uint32 now)
        {
            std::lock_guard<std::mutex> lock(g_lock);
            auto it = g_screens.find(viewer->GetGUIDLow());
            // (written on the world thread: may be a hair after `now`)
            if (it == g_screens.end() || (now > it->second.atMs && now - it->second.atMs > SCREEN_FRESH_MS))
                return false;
            return std::binary_search(it->second.lows.begin(), it->second.lows.end(), target->GetGUIDLow());
        }

        // PBSEE: "<hex guid low>,<hex guid low>,..." ("" = nobody).
        void OnReport(Player* viewer, std::string const& body)
        {
            Screen screen;
            screen.atMs = WorldTimer::getMSTime();
            char const* p = body.c_str();
            while (*p && screen.lows.size() < MAX_REPORTED)
            {
                char* end = nullptr;
                unsigned long const low = std::strtoul(p, &end, 16);
                if (end == p)
                    break;
                if (low)
                    screen.lows.push_back(uint32(low));
                p = *end == ',' ? end + 1 : end;
            }
            std::sort(screen.lows.begin(), screen.lows.end());

            std::lock_guard<std::mutex> lock(g_lock);
            // Players gone long ago (their reports stale): let them go.
            if (g_screens.size() > 64)
                for (auto it = g_screens.begin(); it != g_screens.end();)
                    it = screen.atMs - it->second.atMs > SCREEN_FRESH_MS ? g_screens.erase(it) : std::next(it);
            g_screens[viewer->GetGUIDLow()] = std::move(screen);
        }
    }

    void Load()
    {
        AddonChannel::Register(REPORT_PREFIX, &OnReport);
    }

    bool Sees(Player const* viewer, WorldObject const* target)
    {
        if (!viewer || !target || !viewer->IsInWorld() || viewer->IsBeingTeleported() ||
            !viewer->IsWithinDistInMap(target, SEE_YD))
            return false;
        if (viewer->GetSelectionGuid() == target->GetObjectGuid())
            return true;
        bool const inView = OnScreen(viewer, target, WorldTimer::getMSTime()) || viewer->HasInArc(target, SEE_ARC);
        return inView && viewer->IsWithinLOSInMap(target);
    }
}
}
