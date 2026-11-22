#pragma once

#include "Common.h"

namespace Population
{
    struct SpawnRequest;

    // Who lives here (user 2026-11-22): the races of a starting region or capital make most of
    // its people, fewer the higher the place's levels. Start areas themselves stay single-race
    // (Settlements::StartRacesAt, already set on the request).
    class LocalRaces
    {
    public:
        // The race mask for one spawn: the local races (a local was rolled), the side's other
        // races (a newcomer was rolled), or 0 (no local races here: any race). Only the request's
        // side; a request that already has a race or a race mask gets 0.
        static uint32 Roll(SpawnRequest const& request);
    };
}
