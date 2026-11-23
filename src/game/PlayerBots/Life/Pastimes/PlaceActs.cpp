#include "Life/Pastimes/PlaceActs.h"
#include "GameObject.h"
#include "Map.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Life
{
namespace Pastimes
{
    namespace
    {
        enum class Posture : uint8 { Stand, Chair, Ground };

        struct Beat
        {
            uint32 emote;
            uint32 weight;
        };

        // One kind of place. Times in seconds, chances in percent.
        struct Row
        {
            Place place;
            uint32 stayMin, stayMax;
            uint32 greet, greetPct;         // one-shot on arrival
            uint32 loopState, loopPct;      // kept for the whole stay
            Posture posture;
            uint32 beatMin, beatMax, beatPct;
            std::vector<Beat> beats;
        };

        // Free seat on a chair: nobody within this of the seat the bot would get.
        float const SEAT_TAKEN_YD = 0.8f;
        // A chair that is taken: stand by it this long, then go on.
        uint32 const TAKEN_STAY_MIN = 4, TAKEN_STAY_MAX = 9;
        // Getting up from a chair: back to where it stood, if still about there.
        float const STEP_BACK_MAX_YD = 6.0f;
        // At a fire: kneel instead of sitting (percent).
        uint32 const KNEEL_PCT = 25;

        std::vector<Row> const& Table()
        {
            static std::vector<Row> const rows = {
                { Place::Spot,    25, 70,  0, 0,                          0, 0,                                Posture::Stand,  6, 14, 70,
                  { { EMOTE_ONESHOT_TALK, 5 }, { EMOTE_ONESHOT_LAUGH, 2 }, { EMOTE_ONESHOT_QUESTION, 2 }, { EMOTE_ONESHOT_EXCLAMATION, 1 },
                    { EMOTE_ONESHOT_YES, 2 }, { EMOTE_ONESHOT_NO, 1 }, { EMOTE_ONESHOT_POINT, 1 }, { EMOTE_ONESHOT_CHEER, 1 } } },
                { Place::Vendor,  12, 30,  EMOTE_ONESHOT_TALK, 50,        EMOTE_STATE_USESTANDING_NOSHEATHE, 40, Posture::Stand,  6, 12, 50,
                  { { EMOTE_ONESHOT_TALK, 3 }, { EMOTE_ONESHOT_QUESTION, 2 }, { EMOTE_ONESHOT_YES, 1 }, { EMOTE_ONESHOT_NO, 1 }, { EMOTE_ONESHOT_POINT, 1 } } },
                { Place::Trainer, 12, 25,  EMOTE_ONESHOT_BOW, 70,         0, 0,                                Posture::Stand,  6, 12, 50,
                  { { EMOTE_ONESHOT_TALK, 3 }, { EMOTE_ONESHOT_YES, 2 }, { EMOTE_ONESHOT_QUESTION, 1 } } },
                { Place::Smith,   12, 30,  EMOTE_ONESHOT_SALUTE, 30,      0, 0,                                Posture::Stand,  6, 12, 50,
                  { { EMOTE_ONESHOT_TALK, 3 }, { EMOTE_ONESHOT_QUESTION, 1 }, { EMOTE_ONESHOT_POINT, 1 }, { EMOTE_ONESHOT_YES, 1 } } },
                { Place::Anvil,   25, 60,  0, 0,                          EMOTE_STATE_WORK, 70,                Posture::Stand, 10, 20, 30,
                  { { EMOTE_ONESHOT_TALK, 1 }, { EMOTE_ONESHOT_LAUGH, 1 } } },
                { Place::Quest,   12, 35,  EMOTE_ONESHOT_BOW, 30,         0, 0,                                Posture::Stand,  5, 12, 60,
                  { { EMOTE_ONESHOT_TALK, 3 }, { EMOTE_ONESHOT_QUESTION, 2 }, { EMOTE_ONESHOT_YES, 2 }, { EMOTE_ONESHOT_EXCLAMATION, 1 },
                    { EMOTE_ONESHOT_POINT, 1 } } },
                { Place::Inn,     12, 30,  EMOTE_ONESHOT_WAVE, 30,        0, 0,                                Posture::Stand,  6, 14, 50,
                  { { EMOTE_ONESHOT_TALK, 3 }, { EMOTE_ONESHOT_LAUGH, 2 }, { EMOTE_ONESHOT_YES, 1 } } },
                { Place::Mail,     8, 18,  0, 0,                          EMOTE_STATE_USESTANDING_NOSHEATHE, 80, Posture::Stand, 0, 0, 0, {} },
                { Place::Flight,   8, 18,  EMOTE_ONESHOT_TALK, 30,        0, 0,                                Posture::Stand,  6, 12, 40,
                  { { EMOTE_ONESHOT_TALK, 2 }, { EMOTE_ONESHOT_POINT, 1 } } },
                { Place::Bank,    10, 25,  0, 0,                          EMOTE_STATE_USESTANDING_NOSHEATHE, 50, Posture::Stand,  8, 15, 30,
                  { { EMOTE_ONESHOT_TALK, 1 }, { EMOTE_ONESHOT_YES, 1 } } },
                { Place::Auction, 20, 50,  0, 0,                          EMOTE_STATE_USESTANDING_NOSHEATHE, 60, Posture::Stand,  8, 15, 40,
                  { { EMOTE_ONESHOT_QUESTION, 2 }, { EMOTE_ONESHOT_NO, 2 }, { EMOTE_ONESHOT_YES, 1 }, { EMOTE_ONESHOT_TALK, 1 } } },
                { Place::Chair,   60, 200, 0, 0,                          0, 0,                                Posture::Chair,  0, 0, 0, {} },
                { Place::Fire,    40, 140, 0, 0,                          0, 0,                                Posture::Ground, 0, 0, 0, {} },
            };
            return rows;
        }

        Row const& RowOf(Place place)
        {
            for (Row const& r : Table())
                if (r.place == place)
                    return r;
            return Table().front();
        }

        uint32 Seconds(uint32 lo, uint32 hi)
        {
            return urand(lo, std::max(lo, hi)) * IN_MILLISECONDS;
        }

        uint32 PickBeat(std::vector<Beat> const& beats)
        {
            uint32 total = 0;
            for (Beat const& b : beats)
                total += b.weight;
            if (!total)
                return 0;
            uint32 roll = urand(0, total - 1);
            for (Beat const& b : beats)
            {
                if (roll < b.weight)
                    return b.emote;
                roll -= b.weight;
            }
            return 0;
        }

        bool SeatTaken(Player* me, GameObject* chair)
        {
            float seatX, seatY;
            chair->GetClosestChairSlotPosition(me->GetPositionX(), me->GetPositionY(), seatX, seatY);
            for (auto const& ref : me->GetMap()->GetPlayers())
            {
                Player* p = ref.getSource();
                if (p != me && p->IsInWorld() &&
                    p->GetDistance2d(seatX, seatY, SizeFactor::None) < SEAT_TAKEN_YD)
                    return true;
            }
            return false;
        }

        void ScheduleBeat(ActState& state, Row const& row, uint32 now)
        {
            state.nextBeatMs = row.beats.empty() ? 0 : now + Seconds(row.beatMin, row.beatMax);
        }
    }

    Place PlaceOfAnchor(std::string const& type, bool object)
    {
        if (type == "ah")       return Place::Auction;
        if (type == "bank")     return Place::Bank;
        if (type == "mail")     return Place::Mail;
        if (type == "chair")    return Place::Chair;
        if (type == "fire")     return Place::Fire;
        if (type == "inn")      return Place::Inn;
        if (type == "smith")    return object ? Place::Anvil : Place::Smith;
        if (type == "vendor")   return Place::Vendor;
        if (type == "trainer")  return Place::Trainer;
        if (type == "quest")    return Place::Quest;
        if (type == "flight")   return Place::Flight;
        return Place::Spot;     // idle, showcase, route
    }

    namespace PlaceActs
    {
        void Begin(Player* me, ActState& state, Place place, float faceX, float faceY, bool hasFace,
                   GameObject* chair, uint32 now)
        {
            Row const& row = RowOf(place);
            state = ActState();
            state.place = place;
            state.active = true;
            state.faceX = faceX;
            state.faceY = faceY;
            state.hasFace = hasFace;
            state.untilMs = now + Seconds(row.stayMin, row.stayMax);
            FaceAgain(me, state);

            switch (row.posture)
            {
                case Posture::Chair:
                    if (chair && !SeatTaken(me, chair))
                    {
                        float const x = me->GetPositionX(), y = me->GetPositionY(), z = me->GetPositionZ();
                        chair->Use(me);     // onto its nearest seat, sitting
                        // (Use gives up without line of sight: it stands, a short stay)
                        if (me->IsSittingDown())
                        {
                            state.seated = true;
                            state.onChair = true;
                            state.stepX = x;
                            state.stepY = y;
                            state.stepZ = z;
                            return;
                        }
                    }
                    state.untilMs = now + Seconds(TAKEN_STAY_MIN, TAKEN_STAY_MAX);
                    return;
                case Posture::Ground:
                    me->SetStandState(urand(0, 99) < KNEEL_PCT ? UNIT_STAND_STATE_KNEEL : UNIT_STAND_STATE_SIT);
                    state.seated = true;
                    return;
                case Posture::Stand:
                    break;
            }

            if (row.greet && urand(0, 99) < row.greetPct)
                me->HandleEmoteCommand(row.greet);
            if (row.loopState && urand(0, 99) < row.loopPct)
            {
                state.loopState = row.loopState;
                me->HandleEmoteState(row.loopState);
            }
            ScheduleBeat(state, row, now + 2 * IN_MILLISECONDS);
        }

        bool Update(Player* me, ActState& state, uint32 now)
        {
            if (!state.active)
                return false;
            if (now >= state.untilMs)
                return false;
            if (state.seated || state.loopState || !state.nextBeatMs || now < state.nextBeatMs)
                return true;

            Row const& row = RowOf(state.place);
            if (urand(0, 99) < row.beatPct)
                if (uint32 emote = PickBeat(row.beats))
                    me->HandleEmoteCommand(emote);
            ScheduleBeat(state, row, now);
            return true;
        }

        void End(Player* me, ActState& state)
        {
            if (state.loopState)
                me->HandleEmoteState(0);
            if (state.seated && me->GetStandState() != UNIT_STAND_STATE_STAND)
                me->SetStandState(UNIT_STAND_STATE_STAND);
            // Off the seat, one step back to where it stood (no path leads out of the chair).
            if (state.onChair && me->IsInWorld() && me->IsAlive() &&
                me->GetDistance2d(state.stepX, state.stepY, SizeFactor::None) < STEP_BACK_MAX_YD)
                me->NearTeleportTo(state.stepX, state.stepY, state.stepZ, me->GetOrientation());
            state = ActState();
        }

        bool CanLookAround(ActState const& state)
        {
            return state.active && !state.seated && !state.loopState;
        }

        void FaceAgain(Player* me, ActState const& state)
        {
            if (!state.hasFace)
                return;
            if (std::fabs(state.faceX - me->GetPositionX()) < 0.1f && std::fabs(state.faceY - me->GetPositionY()) < 0.1f)
                return;
            me->SetFacingTo(me->GetAngle(state.faceX, state.faceY));
        }
    }
}
}
