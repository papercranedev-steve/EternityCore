/*
 * Bot daily life (PlayerBots/Life), requests: what a bot can ask its leader for, and the rules
 * of each kind of question. Soul companions doc §5 (stage S5).
 *
 * A new intent is one enum value and one row in INTENTS (Intents.cpp): its protocol key, how
 * long the popup stays, and how long the bot keeps quiet after a "no" or no answer. A "no" again
 * and again waits longer (declineBackoffMs x the declines in a row, at most MAX_DECLINE_STEPS).
 */

#ifndef MANGOS_LIFE_REQUEST_INTENTS_H
#define MANGOS_LIFE_REQUEST_INTENTS_H

#include "Common.h"

namespace Life
{
namespace Requests
{
    enum class Intent : uint8
    {
        Errand,         // "wait for me, I go to the vendor / trainer" (Life/Errands/TownTrip)
        RewardChoice,   // "which reward should I take?" (Companions/RewardChoice)
        Rendezvous,     // "done: shall I come to you, or wait at the gate?" (Life/Errands/TownTrip)
        ClassQuest,     // "will you help me with my class quest?" (Companions/ClassQuests/ClassQuestFlow;
                        // it counts its asks itself, no quiet time here)
        Artisan,        // "will you take me to the Artisan trainer?" (Companions/ArtisanTrip; counts
                        // its asks itself)
        Supply,         // "shall I make you water / a healthstone?", "can you make us water?"
                        // (Life/Errands/GiveProvisions, AskProvisions; they keep their own quiet)
    };

    struct IntentDef
    {
        Intent intent;
        char const* key;            // protocol and phrase key ("errand")
        uint32 timeoutMs;           // the popup's life; then the request's fallback answer
        uint32 declineBackoffMs;    // quiet after a "no" (x declines in a row)
        uint32 expireBackoffMs;     // quiet after no answer
    };

    constexpr uint32 MAX_DECLINE_STEPS = 3;

    IntentDef const& DefOf(Intent intent);
}
}

#endif
