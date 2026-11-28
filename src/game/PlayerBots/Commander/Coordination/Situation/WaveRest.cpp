#include "Commander/Coordination/Situation/WaveRest.h"
#include "Commander/Perception/Forecast/WaveEvent.h"

namespace Commander
{
namespace WaveRest
{
    Rule Of(WorldModel const& world)
    {
        WaveEventFacts const& waves = world.waves;
        if (!waves.active)
            return Rule::Free;
        switch (waves.pace)
        {
            case WavePace::Player:
                return Rule::Free;
            case WavePace::Continuous:
                return Rule::StandUp;
            case WavePace::Timer:
                break;
        }
        if (world.HasPartyEnemy())
            return Rule::StandUp;
        if (waves.nextArrivalSec < 0.0f)
            return Rule::Free;
        if (waves.nextArrivalSec < WaveEvent::STAND_LEAD_SEC)
            return Rule::StandUp;
        if (waves.nextArrivalSec < WaveEvent::STAND_LEAD_SEC + WaveEvent::SIT_SEC)
            return Rule::NoNew;
        return Rule::Free;
    }

    char const* Name(Rule rule)
    {
        switch (rule)
        {
            case Rule::NoNew:   return "no new rest";
            case Rule::StandUp: return "stand up";
            default:            return "free";
        }
    }
}
}
