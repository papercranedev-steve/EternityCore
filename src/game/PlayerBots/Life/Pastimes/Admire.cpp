#include "Life/Pastimes/Admire.h"
#include "Shared/BotChat.h"
#include "Shared/Gaze.h"
#include "Shared/I18n.h"
#include "Shared/Phrases.h"
#include "Map.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Utilities/Random.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace Life
{
namespace Pastimes
{
namespace Admire
{
    namespace
    {
        namespace SG = BotShared::ShowyGear;

        // One player's attention budget and what he was admired with (all map threads).
        struct Attention
        {
            float tokens = TOKENS_MAX;
            uint32 refilledMs = 0;
            std::unordered_map<uint32, uint32> seen;    // thing key -> times admired with it on
        };

        std::mutex g_lock;
        std::unordered_map<uint64, Attention> g_attention;

        // Rolls for `p` (things on him now): true = admire him now (the budget paid, the things
        // counted as seen once more).
        bool Roll(Player const* p, std::vector<SG::Thing> const& things, uint32 now)
        {
            std::lock_guard<std::mutex> lock(g_lock);
            Attention& a = g_attention[p->GetObjectGuid().GetRawValue()];
            if (a.refilledMs)
                a.tokens = std::min(TOKENS_MAX, a.tokens + float(now - a.refilledMs) / float(TOKEN_REFILL_MS));
            a.refilledMs = now;
            if (a.tokens < 1.0f)
                return false;

            float score = 0.0f;
            for (SG::Thing const& t : things)
            {
                auto it = a.seen.find(t.key);
                uint32 const times = it == a.seen.end() ? 0 : std::min<uint32>(it->second, 16);
                score += float(t.points) / float(1u << times);
            }
            float const chance = std::min(CHANCE_MAX, CHANCE_PER_POINT * score);
            if (frand(0.0f, 1.0f) >= chance)
                return false;

            a.tokens -= 1.0f;
            for (SG::Thing const& t : things)
                ++a.seen[t.key];
            return true;
        }

        bool JudgedLately(AdmireState const& state, ObjectGuid const& guid, uint32 now)
        {
            for (auto const& j : state.judged)
                if (j.first == guid && now - j.second < SAME_PLAYER_MS)
                    return true;
            return false;
        }

        Player* Candidate(Player* me, AdmireState const& state, uint32 now)
        {
            Player* best = nullptr;
            float bestDist = ADMIRE_YD;
            for (auto const& ref : me->GetMap()->GetPlayers())
            {
                Player* p = ref.getSource();
                if (p == me || p->IsBot() || !p->IsInWorld() || !p->IsAlive() ||
                    p->GetVisibility() == VISIBILITY_OFF || p->IsBeingTeleported())
                    continue;
                float const d = me->GetDistance(p);
                if (d >= bestDist || JudgedLately(state, p->GetObjectGuid(), now) || !me->IsWithinLOSInMap(p))
                    continue;
                best = p;
                bestDist = d;
            }
            return best;
        }

        AdmireStance StanceOf(Player const* me, Player const* p)
        {
            uint32 const mine = me->GetLevel();
            uint32 const his = p->GetLevel();
            if (mine > his)
                return AdmireStance::Senior;
            return his >= mine + NOVICE_BELOW ? AdmireStance::Novice : AdmireStance::Peer;
        }

        template <size_t N>
        uint32 Pick(uint32 const (&list)[N])
        {
            return list[urand(0, uint32(N) - 1)];
        }

        uint32 const g_novice[] = { TEXTEMOTE_GASP, TEXTEMOTE_GASP, TEXTEMOTE_DROOL, TEXTEMOTE_BOW, TEXTEMOTE_APPLAUD };
        uint32 const g_peer[] = { TEXTEMOTE_WHISTLE, TEXTEMOTE_WHISTLE, TEXTEMOTE_APPLAUD, TEXTEMOTE_CLAP, TEXTEMOTE_POINT };
        uint32 const g_senior[] = { TEXTEMOTE_NOD, TEXTEMOTE_NOD, TEXTEMOTE_CLAP };

        char const* StanceKey(AdmireStance s)
        {
            switch (s)
            {
                case AdmireStance::Novice: return "admire.say.novice";
                case AdmireStance::Senior: return "admire.say.senior";
                default:                   return "admire.say.peer";
            }
        }

        char const* LookKey(SG::Look look)
        {
            switch (look)
            {
                case SG::Look::Head:      return "admire.thing.head";
                case SG::Look::Shoulders: return "admire.thing.shoulders";
                case SG::Look::Chest:     return "admire.thing.chest";
                case SG::Look::Back:      return "admire.thing.back";
                case SG::Look::Mount:     return "admire.thing.mount";
                default:                  return "admire.thing.weapon";
            }
        }

        bool Has(std::vector<std::string> const& tags, char const* tag)
        {
            return std::find(tags.begin(), tags.end(), tag) != tags.end();
        }
    }

    void Stop(AdmireState& state)
    {
        state.looking.Clear();
        state.untilMs = 0;
        state.actAtMs = 0;
        state.sayAtMs = 0;
    }

    bool Update(Player* me, AdmireState& state, bool free, uint32 now, std::vector<std::string> const& tags)
    {
        if (!state.looking.IsEmpty())
        {
            Player* p = me->GetMap()->GetPlayer(state.looking);
            if (!p || !p->IsInWorld() || now >= state.untilMs || me->IsInCombat())
            {
                Stop(state);
                return false;
            }
            me->SetFacingToObject(p);
            // The emote once he looks (not behind his back); the line, if any, after it.
            if (state.actAtMs && now >= state.actAtMs && BotShared::Gaze::Sees(p, me))
            {
                state.actAtMs = 0;
                uint32 const emote = state.stance == AdmireStance::Novice ? Pick(g_novice)
                    : state.stance == AdmireStance::Senior ? Pick(g_senior) : Pick(g_peer);
                BotShared::BotChat::TextEmote(me, emote, p);
                if (state.sayAtMs)
                    state.sayAtMs = now + urand(SAY_AFTER_MIN_MS, SAY_AFTER_MAX_MS);
                state.untilMs = std::max(state.untilMs, (state.sayAtMs ? state.sayAtMs : now) + HOLD_AFTER_MS);
            }
            if (!state.actAtMs && state.sayAtMs && now >= state.sayAtMs)
            {
                state.sayAtMs = 0;
                BotShared::I18n::Lang const lang = BotShared::I18n::Of(p);
                std::string const thing = BotShared::Phrases::Word(lang, LookKey(state.look));
                BotShared::BotChat::Say(me, BotShared::Phrases::Say(lang, StanceKey(state.stance), tags, { { "thing", thing } }));
            }
            return true;
        }

        if (!free || now < state.nextCheckMs || me->IsInCombat())
            return false;
        state.nextCheckMs = now + CHECK_MS;

        Player* p = Candidate(me, state, now);
        if (!p)
            return false;

        // One roll per player per SAME_PLAYER_MS for this bot.
        while (!state.judged.empty() && now - state.judged.front().second > SAME_PLAYER_MS)
            state.judged.pop_front();
        state.judged.emplace_back(p->GetObjectGuid(), now);

        std::vector<SG::Thing> const things = SG::Of(p);
        if (things.empty() || !Roll(p, things, now))
            return false;

        SG::Thing const* best = &things.front();
        for (SG::Thing const& t : things)
            if (t.points > best->points)
                best = &t;

        state.looking = p->GetObjectGuid();
        state.stance = StanceOf(me, p);
        state.look = best->look;
        state.untilMs = now + urand(LOOK_MIN_MS, LOOK_MAX_MS);
        state.actAtMs = now + urand(ACT_MIN_MS, ACT_MAX_MS);
        uint32 const sayPct = Has(tags, "chatty") ? SAY_CHATTY_PCT : Has(tags, "quiet") ? SAY_QUIET_PCT : SAY_PCT;
        state.sayAtMs = urand(0, 99) < sayPct ? state.actAtMs + urand(SAY_AFTER_MIN_MS, SAY_AFTER_MAX_MS) : 0;
        me->SetFacingToObject(p);
        return true;
    }
}
}
}
