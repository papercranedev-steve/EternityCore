#include "Life/Pastimes/EmoteReply.h"
#include "Shared/BotChat.h"
#include "Map.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Utilities/Random.h"

namespace Life
{
namespace Pastimes
{
namespace EmoteReply
{
    namespace
    {
        enum class Kind : uint8 { None, Greet, Bye, Thanks, Praise, Affection, Smile, Comfort, Insult, Dance, ShowOff, Sad, Puzzle };

        Kind KindOf(uint32 emote)
        {
            switch (emote)
            {
                case TEXTEMOTE_WAVE: case TEXTEMOTE_HELLO: case TEXTEMOTE_GREET: case TEXTEMOTE_HAIL:
                case TEXTEMOTE_WELCOME: case TEXTEMOTE_BOW: case TEXTEMOTE_SALUTE: case TEXTEMOTE_CURTSEY:
                case TEXTEMOTE_INTRODUCE:
                    return Kind::Greet;
                case TEXTEMOTE_BYE:
                    return Kind::Bye;
                case TEXTEMOTE_THANK:
                    return Kind::Thanks;
                case TEXTEMOTE_CHEER: case TEXTEMOTE_APPLAUD: case TEXTEMOTE_CLAP: case TEXTEMOTE_CONGRATULATE:
                case TEXTEMOTE_PRAISE: case TEXTEMOTE_AMAZE: case TEXTEMOTE_AGREE:
                    return Kind::Praise;
                case TEXTEMOTE_HUG: case TEXTEMOTE_CUDDLE: case TEXTEMOTE_KISS: case TEXTEMOTE_LOVE:
                case TEXTEMOTE_SEXY: case TEXTEMOTE_FLIRT: case TEXTEMOTE_WINK:
                    return Kind::Affection;
                case TEXTEMOTE_SMILE: case TEXTEMOTE_GRIN: case TEXTEMOTE_HAPPY: case TEXTEMOTE_GIGGLE:
                case TEXTEMOTE_CHUCKLE:
                    return Kind::Smile;
                case TEXTEMOTE_COMFORT: case TEXTEMOTE_SOOTHE: case TEXTEMOTE_PAT:
                    return Kind::Comfort;
                case TEXTEMOTE_RUDE: case TEXTEMOTE_SPIT: case TEXTEMOTE_CHICKEN: case TEXTEMOTE_SLAP:
                case TEXTEMOTE_INSULT: case TEXTEMOTE_TAUNT: case TEXTEMOTE_MOCK: case TEXTEMOTE_THREATEN:
                case TEXTEMOTE_MOON: case TEXTEMOTE_FART: case TEXTEMOTE_STINK: case TEXTEMOTE_SNUB:
                case TEXTEMOTE_SHOO: case TEXTEMOTE_GLARE: case TEXTEMOTE_BONK: case TEXTEMOTE_TEASE:
                case TEXTEMOTE_GLOAT: case TEXTEMOTE_CACKLE: case TEXTEMOTE_SNICKER:
                    return Kind::Insult;
                case TEXTEMOTE_DANCE: case TEXTEMOTE_SHIMMY:
                    return Kind::Dance;
                case TEXTEMOTE_FLEX: case TEXTEMOTE_ROAR: case TEXTEMOTE_VICTORY: case TEXTEMOTE_BRANDISH:
                    return Kind::ShowOff;
                case TEXTEMOTE_CRY: case TEXTEMOTE_MOURN: case TEXTEMOTE_WHINE: case TEXTEMOTE_SIGH:
                case TEXTEMOTE_TIRED:
                    return Kind::Sad;
                case TEXTEMOTE_POINT: case TEXTEMOTE_CURIOUS: case TEXTEMOTE_STARE: case TEXTEMOTE_PEER:
                case TEXTEMOTE_GAZE: case TEXTEMOTE_EYE: case TEXTEMOTE_BECKON: case TEXTEMOTE_POKE:
                case TEXTEMOTE_TICKLE: case TEXTEMOTE_PONDER:
                    return Kind::Puzzle;
                default:
                    return Kind::None;
            }
        }

        template <size_t N>
        uint32 Pick(uint32 const (&list)[N])
        {
            return list[urand(0, uint32(N) - 1)];
        }

        uint32 const g_greet[] = { TEXTEMOTE_WAVE, TEXTEMOTE_WAVE, TEXTEMOTE_HELLO, TEXTEMOTE_BOW, TEXTEMOTE_SALUTE };
        uint32 const g_bye[] = { TEXTEMOTE_BYE, TEXTEMOTE_WAVE };
        uint32 const g_thanks[] = { TEXTEMOTE_WELCOME, TEXTEMOTE_NOD, TEXTEMOTE_BOW, TEXTEMOTE_SMILE };
        uint32 const g_praise[] = { TEXTEMOTE_BOW, TEXTEMOTE_NOD, TEXTEMOTE_SMILE, TEXTEMOTE_THANK };
        uint32 const g_affection[] = { TEXTEMOTE_BLUSH, TEXTEMOTE_BASHFUL, TEXTEMOTE_GIGGLE, TEXTEMOTE_SHY };
        uint32 const g_affectionHot[] = { TEXTEMOTE_GLARE, TEXTEMOTE_NO, TEXTEMOTE_BLUSH };
        uint32 const g_smile[] = { TEXTEMOTE_SMILE, TEXTEMOTE_GRIN, TEXTEMOTE_WAVE };
        uint32 const g_comfort[] = { TEXTEMOTE_THANK, TEXTEMOTE_SMILE, TEXTEMOTE_NOD };
        uint32 const g_insultHot[] = { TEXTEMOTE_ANGRY, TEXTEMOTE_RUDE, TEXTEMOTE_THREATEN, TEXTEMOTE_GLARE };
        uint32 const g_insultTimid[] = { TEXTEMOTE_COWER, TEXTEMOTE_CRY, TEXTEMOTE_SCARED };
        uint32 const g_insultCalm[] = { TEXTEMOTE_SIGH, TEXTEMOTE_SHRUG, TEXTEMOTE_FROWN, TEXTEMOTE_NO };
        uint32 const g_dance[] = { TEXTEMOTE_CHEER, TEXTEMOTE_CLAP, TEXTEMOTE_LAUGH };
        uint32 const g_showOff[] = { TEXTEMOTE_LAUGH, TEXTEMOTE_APPLAUD, TEXTEMOTE_FLEX };
        uint32 const g_sad[] = { TEXTEMOTE_COMFORT, TEXTEMOTE_SOOTHE, TEXTEMOTE_HUG };
        uint32 const g_puzzle[] = { TEXTEMOTE_CONFUSED, TEXTEMOTE_SHRUG, TEXTEMOTE_CURIOUS };
        uint32 const g_other[] = { TEXTEMOTE_SHRUG, TEXTEMOTE_CONFUSED, TEXTEMOTE_NOD };

        uint64 Mix(uint64 x)
        {
            x += 0x9E3779B97F4A7C15ull;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
            return x ^ (x >> 31);
        }

        // How often `from` emoted at it lately (this one included).
        uint32 CountRecent(EmoteReplyState& state, ObjectGuid const& from, uint32 now)
        {
            while (!state.heard.empty() && now - state.heard.front().second > SPAM_MS)
                state.heard.pop_front();
            uint32 n = 0;
            for (auto const& h : state.heard)
                if (h.first == from)
                    ++n;
            return n;
        }

        void Answer(Player* me, EmoteReplyState& state, Player* from, bool canStand, uint32 now)
        {
            Kind const kind = KindOf(state.emote);
            uint32 reply = 0;
            switch (kind)
            {
                case Kind::Greet:
                    // Often the same greeting back.
                    reply = urand(0, 99) < 40 && state.emote != TEXTEMOTE_INTRODUCE ? state.emote : Pick(g_greet);
                    if (reply == TEXTEMOTE_CURTSEY && me->GetGender() != GENDER_FEMALE)
                        reply = TEXTEMOTE_BOW;
                    break;
                case Kind::Bye:       reply = Pick(g_bye); break;
                case Kind::Thanks:    reply = Pick(g_thanks); break;
                case Kind::Praise:    reply = Pick(g_praise); break;
                case Kind::Affection: reply = state.temper == Temper::Hot ? Pick(g_affectionHot) : Pick(g_affection); break;
                case Kind::Smile:     reply = Pick(g_smile); break;
                case Kind::Comfort:   reply = Pick(g_comfort); break;
                case Kind::Insult:
                    switch (state.temper)
                    {
                        case Temper::Hot:
                            reply = Pick(g_insultHot);
                            break;
                        case Temper::Timid:
                            reply = Pick(g_insultTimid);
                            state.walkOff = true;
                            break;
                        case Temper::Calm:
                            reply = Pick(g_insultCalm);
                            state.walkOff = urand(0, 99) < CALM_WALK_OFF_PCT;
                            break;
                    }
                    break;
                case Kind::Dance:
                    if (canStand && urand(0, 99) < DANCE_PCT)
                    {
                        if (me->IsSittingDown())
                            me->SetStandState(UNIT_STAND_STATE_STAND);
                        me->HandleEmoteState(EMOTE_STATE_DANCE);
                        state.danceUntilMs = now + urand(DANCE_MIN_MS, DANCE_MAX_MS);
                        return;
                    }
                    reply = Pick(g_dance);
                    break;
                case Kind::ShowOff:   reply = Pick(g_showOff); break;
                case Kind::Sad:       reply = Pick(g_sad); break;
                case Kind::Puzzle:    reply = Pick(g_puzzle); break;
                case Kind::None:
                    if (urand(0, 1))
                        reply = Pick(g_other);
                    break;
            }
            if (reply)
                BotShared::BotChat::TextEmote(me, reply, from);
        }
    }

    void Init(EmoteReplyState& state, uint64 seed)
    {
        state = EmoteReplyState();
        uint64 const roll = Mix(seed ^ 0x454D4F54ull) % 100;
        state.temper = roll < HOT_PCT ? Temper::Hot : roll < HOT_PCT + TIMID_PCT ? Temper::Timid : Temper::Calm;
    }

    void Heard(Player* me, EmoteReplyState& state, Player* from, uint32 textEmote, uint32 now)
    {
        if (!from || from->IsBot() || !me->IsAlive() || me->IsInCombat() || !me->IsWithinDist(from, HEAR_YD, false))
            return;
        ObjectGuid const guid = from->GetObjectGuid();
        uint32 const recent = CountRecent(state, guid, now);
        state.heard.emplace_back(guid, now);
        if (recent >= SPAM_COUNT)
        {
            // Enough: one sigh, then nothing until he stops.
            if (state.sighed)
                return;
            state.sighed = true;
            state.from = guid;
            state.emote = 0;
            state.answerAtMs = now + urand(ANSWER_MIN_MS, ANSWER_MAX_MS);
            return;
        }
        if (recent == 0)
            state.sighed = false;
        state.from = guid;
        state.emote = textEmote;
        state.answerAtMs = now + urand(ANSWER_MIN_MS, ANSWER_MAX_MS);
    }

    bool Update(Player* me, EmoteReplyState& state, bool canStand, uint32 now)
    {
        if (state.danceUntilMs)
        {
            if (now < state.danceUntilMs && canStand && !me->IsInCombat())
                return true;
            Stop(me, state);
        }
        if (state.from.IsEmpty() || now < state.answerAtMs)
            return false;

        ObjectGuid const guid = state.from;
        state.from.Clear();
        if (me->IsInCombat())
            return false;
        Player* from = me->GetMap()->GetPlayer(guid);
        if (!from || !from->IsInWorld() || !me->IsWithinDist(from, HEAR_YD, false))
            return false;
        if (!me->IsMoving())
            me->SetFacingToObject(from);
        if (!state.emote)
        {
            BotShared::BotChat::TextEmote(me, TEXTEMOTE_SIGH, from);
            return false;
        }
        Answer(me, state, from, canStand, now);
        return state.danceUntilMs != 0;
    }

    bool TakeWalkOff(EmoteReplyState& state)
    {
        bool const walk = state.walkOff;
        state.walkOff = false;
        return walk;
    }

    void Stop(Player* me, EmoteReplyState& state)
    {
        if (state.danceUntilMs)
        {
            state.danceUntilMs = 0;
            me->HandleEmoteState(0);
        }
    }
}
}
}
