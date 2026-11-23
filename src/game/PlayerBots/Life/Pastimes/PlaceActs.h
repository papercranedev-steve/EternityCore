/*
 * Bot daily life (PlayerBots/Life), pastimes: what a person does while standing (or sitting) at
 * one place in town for a while. The actions were all checked in game on the Classic client
 * (.kiro/docs/2026-11-19-population-liveliness.md, review 2026-11-19).
 *
 * Each kind of place has one row in a table (PlaceActs.cpp): how long people stay, a greeting
 * on arrival (a bow to the trainer, a salute to the smith), a looping state some of them keep
 * (fiddling at the mailbox, hammering at the anvil), the posture (a chair, the ground by a
 * fire) and the beats now and then (talk, nod, point...). A new kind of place is a new row.
 * No speech: animations only.
 *
 * The caller walks the bot there (Life/Errands/Trip/Approach.h), then Begin; Update until it
 * returns false (the stay is over), then End (stands up, the state off). End also when it is
 * interrupted (leaving, a review).
 *
 * Map thread of the bot.
 */

#ifndef MANGOS_LIFE_PASTIMES_PLACE_ACTS_H
#define MANGOS_LIFE_PASTIMES_PLACE_ACTS_H

#include "Common.h"
#include <string>

class Player;
class GameObject;

namespace Life
{
namespace Pastimes
{
    enum class Place : uint8
    {
        Spot,       // a plain spot among people (scene point "idle", a showcase spot)
        Vendor,
        Trainer,
        Smith,      // the repair NPC
        Anvil,      // an anvil or a forge
        Quest,      // a quest giver
        Inn,        // the innkeeper
        Mail,
        Flight,
        Bank,
        Auction,
        Chair,
        Fire,
    };

    // The place of a population anchor type (ah bank mail idle showcase / chair fire inn smith
    // vendor trainer quest flight); `object`: the anchor is a gameobject (the smith's anvil).
    Place PlaceOfAnchor(std::string const& type, bool object);

    struct ActState
    {
        Place place = Place::Spot;
        bool active = false;
        uint32 untilMs = 0;
        uint32 nextBeatMs = 0;
        bool seated = false;        // on a chair or the ground: no beats, cannot turn
        // On a chair's seat (inside the chair's collision: no path out, Trip/StepOff.h): End
        // puts it back where it stood before sitting down.
        bool onChair = false;
        float stepX = 0.0f, stepY = 0.0f, stepZ = 0.0f;
        uint32 loopState = 0;       // EMOTE_STATE_* it keeps, 0: none
        float faceX = 0.0f, faceY = 0.0f;
        bool hasFace = false;
    };

    namespace PlaceActs
    {
        // The bot stands at the place now (facing it). `chair`: the chair of a Chair place
        // (taken only if its seat is free; else the stay is short and standing). The bot
        // stands beside the chair, not on it: GameObject::Use puts it onto the seat, End back
        // onto this spot.
        void Begin(Player* me, ActState& state, Place place, float faceX, float faceY, bool hasFace,
                   GameObject* chair, uint32 now);
        // False: the stay is over.
        bool Update(Player* me, ActState& state, uint32 now);
        void End(Player* me, ActState& state);
        // Standing and free to look around (not seated, not in a looping state).
        bool CanLookAround(ActState const& state);
        // Back to the place's facing (after looking at someone).
        void FaceAgain(Player* me, ActState const& state);
    }
}
}

#endif
