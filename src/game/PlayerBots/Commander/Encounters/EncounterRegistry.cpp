#include "Commander/Encounters/EncounterRegistry.h"
#include "Commander/Encounters/BlackfathomFires.h"
#include "Commander/Encounters/BlackrockDepthsAngerforge.h"
#include "Commander/Encounters/BlackrockDepthsArgelmach.h"
#include "Commander/Encounters/BlackrockDepthsFlamelash.h"
#include "Commander/Encounters/BlackrockDepthsRingOfLaw.h"
#include "Commander/Encounters/BlackrockDepthsSeven.h"
#include "Commander/Encounters/BlackrockDepthsThaurissan.h"
#include "Commander/Encounters/BlackrockSpireUrok.h"
#include "Commander/Encounters/DeadminesVanCleef.h"
#include "Commander/Encounters/DireMaulGordok.h"
#include "Commander/Encounters/DireMaulKromcrush.h"
#include "Commander/Encounters/GnomereganGrubbis.h"
#include "Commander/Encounters/ScarletHerod.h"
#include "Commander/Encounters/ScarletMograineWhitemane.h"
#include "Commander/Encounters/GnomereganThermaplugg.h"
#include "Commander/Encounters/RazorfenBelnistrasz.h"
#include "Commander/Encounters/RazorfenGong.h"
#include "Commander/Encounters/ScholomanceGandling.h"
#include "Commander/Encounters/StratholmeWilley.h"
#include "Commander/Encounters/WailingCavernsNaralex.h"
#include "Commander/Encounters/StratholmeAnastari.h"
#include "Commander/Encounters/StratholmeRivendare.h"
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
            { BlackfathomFires::MAP_ID,         &Make<BlackfathomFires> },
            { BlackrockDepthsAngerforge::MAP_ID, &Make<BlackrockDepthsAngerforge> },
            { BlackrockDepthsArgelmach::MAP_ID, &Make<BlackrockDepthsArgelmach> },
            { BlackrockDepthsFlamelash::MAP_ID, &Make<BlackrockDepthsFlamelash> },
            { BlackrockDepthsRingOfLaw::MAP_ID, &Make<BlackrockDepthsRingOfLaw> },
            { BlackrockDepthsSeven::MAP_ID,     &Make<BlackrockDepthsSeven> },
            { BlackrockDepthsThaurissan::MAP_ID, &Make<BlackrockDepthsThaurissan> },
            { BlackrockSpireUrok::MAP_ID,       &Make<BlackrockSpireUrok> },
            { DeadminesVanCleef::MAP_ID,        &Make<DeadminesVanCleef> },
            { DireMaulGordok::MAP_ID,           &Make<DireMaulGordok> },
            { DireMaulKromcrush::MAP_ID,        &Make<DireMaulKromcrush> },
            { GnomereganGrubbis::MAP_ID,        &Make<GnomereganGrubbis> },
            { ScarletHerod::MAP_ID,             &Make<ScarletHerod> },
            { ScarletMograineWhitemane::MAP_ID, &Make<ScarletMograineWhitemane> },
            { GnomereganThermaplugg::MAP_ID,    &Make<GnomereganThermaplugg> },
            { RazorfenBelnistrasz::MAP_ID,      &Make<RazorfenBelnistrasz> },
            { RazorfenGong::MAP_ID,             &Make<RazorfenGong> },
            { ScholomanceGandling::MAP_ID,      &Make<ScholomanceGandling> },
            { StratholmeWilley::MAP_ID,         &Make<StratholmeWilley> },
            { WailingCavernsNaralex::MAP_ID,    &Make<WailingCavernsNaralex> },
            { StratholmeAnastari::MAP_ID,       &Make<StratholmeAnastari> },
            { StratholmeRivendare::MAP_ID,      &Make<StratholmeRivendare> },
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
