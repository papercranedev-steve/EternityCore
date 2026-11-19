#include "Life/Requests/Intents.h"

namespace Life
{
namespace Requests
{
    namespace
    {
        uint32 constexpr SEC = IN_MILLISECONDS;
        uint32 constexpr MIN = MINUTE * IN_MILLISECONDS;

        IntentDef const INTENTS[] = {
            //  intent                 key        timeout   after "no"  after silence
            { Intent::Errand,       "errand",    30 * SEC,  60 * MIN,   10 * MIN },
            // (the turn-in waits QuestTurnIn::CHOICE_WAIT_MS: the popup closes before it)
            { Intent::RewardChoice, "reward",    25 * SEC,  0,          0 },
            { Intent::Rendezvous,   "meet",      60 * SEC,  0,          0 },
            { Intent::ClassQuest,   "classquest", 30 * SEC, 0,          0 },
        };
    }

    IntentDef const& DefOf(Intent intent)
    {
        for (IntentDef const& def : INTENTS)
            if (def.intent == intent)
                return def;
        return INTENTS[0];
    }
}
}
