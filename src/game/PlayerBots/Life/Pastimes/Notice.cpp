#include "Life/Pastimes/Notice.h"
#include "Shared/BotChat.h"
#include "Map.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Utilities/Random.h"
#include <mutex>
#include <unordered_map>

namespace Life
{
namespace Pastimes
{
namespace Notice
{
    namespace
    {
        // Bots only greet after turning to him.
        uint32 const GREET_DELAY_MIN_MS = 600, GREET_DELAY_MAX_MS = 1200;
        size_t const NOTICED_KEEP = 8;

        uint32 const g_gestures[] = { EMOTE_ONESHOT_WAVE, EMOTE_ONESHOT_WAVE, EMOTE_ONESHOT_WAVE, EMOTE_ONESHOT_BOW, EMOTE_ONESHOT_SALUTE };
        uint32 const g_textGreetings[] = { TEXTEMOTE_WAVE, TEXTEMOTE_WAVE, TEXTEMOTE_HELLO, TEXTEMOTE_GREET, TEXTEMOTE_BOW, TEXTEMOTE_SALUTE };

        template <size_t N>
        uint32 Pick(uint32 const (&list)[N])
        {
            return list[urand(0, uint32(N) - 1)];
        }

        // Player guid -> when someone last greeted him (all map threads).
        std::mutex g_greetLock;
        std::unordered_map<uint64, uint32> g_lastGreet;

        // True: this bot may greet him now (and the next greeting for him waits).
        bool ClaimGreeting(ObjectGuid const& player, uint32 now)
        {
            std::lock_guard<std::mutex> lock(g_greetLock);
            if (g_lastGreet.size() > 64)
                for (auto it = g_lastGreet.begin(); it != g_lastGreet.end();)
                    it = now - it->second > GREET_GAP_MS ? g_lastGreet.erase(it) : std::next(it);
            auto it = g_lastGreet.find(player.GetRawValue());
            if (it != g_lastGreet.end() && now - it->second < GREET_GAP_MS)
                return false;
            g_lastGreet[player.GetRawValue()] = now;
            return true;
        }

        bool NoticedLately(NoticeState const& state, ObjectGuid const& guid, uint32 now)
        {
            for (auto const& n : state.noticed)
                if (n.first == guid && now - n.second < SAME_PLAYER_MS)
                    return true;
            return false;
        }

        Player* Candidate(Player* me, NoticeState const& state, uint32 now)
        {
            Player* best = nullptr;
            float bestDist = NOTICE_YD;
            for (auto const& ref : me->GetMap()->GetPlayers())
            {
                Player* p = ref.getSource();
                if (p == me || p->IsBot() || !p->IsInWorld() || !p->IsAlive() ||
                    p->GetVisibility() == VISIBILITY_OFF || p->IsBeingTeleported())
                    continue;
                float const d = me->GetDistance(p);
                if (d >= bestDist || NoticedLately(state, p->GetObjectGuid(), now) || !me->IsWithinLOSInMap(p))
                    continue;
                best = p;
                bestDist = d;
            }
            return best;
        }
    }

    void Init(NoticeState& state, uint64 seed)
    {
        state = NoticeState();
        // (splitmix-style mix: neighbouring guids do not all fall the same way)
        uint64 x = seed + 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        x ^= x >> 31;
        state.attentive = x % 100 < ATTENTIVE_PCT;
    }

    void Stop(NoticeState& state)
    {
        state.looking.Clear();
        state.lookUntilMs = 0;
        state.greetAtMs = 0;
    }

    bool Update(Player* me, NoticeState& state, bool free, uint32 now)
    {
        if (!state.attentive)
            return false;

        if (!state.looking.IsEmpty())
        {
            Player* p = free ? me->GetMap()->GetPlayer(state.looking) : nullptr;
            if (!p || now >= state.lookUntilMs)
            {
                Stop(state);
                return false;
            }
            me->SetFacingToObject(p);
            if (state.greetAtMs && now >= state.greetAtMs)
            {
                state.greetAtMs = 0;
                if (urand(0, 99) < GREET_TEXT_PCT)
                    BotShared::BotChat::TextEmote(me, Pick(g_textGreetings), p);
                else
                    me->HandleEmoteCommand(Pick(g_gestures));
            }
            return true;
        }

        if (!free || now < state.nextCheckMs)
            return false;
        state.nextCheckMs = now + CHECK_MS;

        Player* p = Candidate(me, state, now);
        if (!p)
            return false;

        state.noticed.emplace_back(p->GetObjectGuid(), now);
        if (state.noticed.size() > NOTICED_KEEP)
            state.noticed.pop_front();
        state.looking = p->GetObjectGuid();
        state.lookUntilMs = now + urand(LOOK_MIN_MS, LOOK_MAX_MS);
        state.greetAtMs = urand(0, 99) < GREET_PCT && ClaimGreeting(p->GetObjectGuid(), now)
            ? now + urand(GREET_DELAY_MIN_MS, GREET_DELAY_MAX_MS) : 0;
        me->SetFacingToObject(p);
        return true;
    }
}
}
}
