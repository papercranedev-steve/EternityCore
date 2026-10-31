#include "Commander/Encounters/EncounterRegistry.h"
#include "Commander/Encounters/DeadminesVanCleef.h"
#include "Commander/Encounters/ScarletMograineWhitemane.h"

namespace Commander
{
    namespace
    {
        template <class T>
        std::unique_ptr<IEncounter> Make()
        {
            return std::make_unique<T>();
        }

        struct EncounterFactory
        {
            uint32 mapId;
            std::unique_ptr<IEncounter> (*create)();
        };

        // One line per boss module.
        EncounterFactory const FACTORIES[] =
        {
            { DeadminesVanCleef::MAP_ID,        &Make<DeadminesVanCleef> },
            { ScarletMograineWhitemane::MAP_ID, &Make<ScarletMograineWhitemane> },
        };
    }

    namespace EncounterRegistry
    {
        void Create(uint32 mapId, EncounterList& out)
        {
            for (EncounterFactory const& f : FACTORIES)
                if (f.mapId == mapId)
                    out.push_back(f.create());
        }

        bool HasEncounterOnMap(uint32 mapId)
        {
            for (EncounterFactory const& f : FACTORIES)
                if (f.mapId == mapId)
                    return true;
            return false;
        }
    }
}
