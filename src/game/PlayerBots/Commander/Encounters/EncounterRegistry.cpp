#include "Commander/Encounters/EncounterRegistry.h"
#include "Commander/Encounters/BlackrockDepthsAngerforge.h"
#include "Commander/Encounters/BlackrockDepthsFlamelash.h"
#include "Commander/Encounters/DeadminesVanCleef.h"
#include "Commander/Encounters/ScarletHerod.h"
#include "Commander/Encounters/ScarletMograineWhitemane.h"
#include "Commander/Encounters/GnomereganThermaplugg.h"
#include "Commander/Encounters/RazorfenBelnistrasz.h"
#include "Commander/Encounters/ScholomanceGandling.h"
#include "Commander/Encounters/WailingCavernsNaralex.h"
#include "Commander/Encounters/StratholmeAnastari.h"
#include "Commander/Encounters/SunkenTempleHakkar.h"
#include "Commander/Encounters/ZulFarrakPyramid.h"

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
            { BlackrockDepthsAngerforge::MAP_ID, &Make<BlackrockDepthsAngerforge> },
            { BlackrockDepthsFlamelash::MAP_ID, &Make<BlackrockDepthsFlamelash> },
            { DeadminesVanCleef::MAP_ID,        &Make<DeadminesVanCleef> },
            { ScarletHerod::MAP_ID,             &Make<ScarletHerod> },
            { ScarletMograineWhitemane::MAP_ID, &Make<ScarletMograineWhitemane> },
            { GnomereganThermaplugg::MAP_ID,    &Make<GnomereganThermaplugg> },
            { RazorfenBelnistrasz::MAP_ID,      &Make<RazorfenBelnistrasz> },
            { ScholomanceGandling::MAP_ID,      &Make<ScholomanceGandling> },
            { WailingCavernsNaralex::MAP_ID,    &Make<WailingCavernsNaralex> },
            { StratholmeAnastari::MAP_ID,       &Make<StratholmeAnastari> },
            { SunkenTempleHakkar::MAP_ID,       &Make<SunkenTempleHakkar> },
            { ZulFarrakPyramid::MAP_ID,         &Make<ZulFarrakPyramid> },
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
