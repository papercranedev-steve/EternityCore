#include "Commander/Perception/Forecast/SheetTimers.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Profiles/CreatureProfile.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
namespace SheetTimers
{
    namespace
    {
        // Cooldown estimate of one sheet row (SheetTimers.h), false = left out.
        bool Estimate(EnemyFacts const& e, CreatureSpellRef const& ref, uint32 nowMs, uint32& cooldownMs)
        {
            if (ref.healthPct > 0.0f && e.healthPct > ref.healthPct)
                return false;
            if (uint32 const last = e.LastCastStartMs(ref.spellId))
            {
                if (ref.OncePerFight())
                    return false;
                uint32 const since = WorldTimer::getMSTimeDiff(last, nowMs);
                cooldownMs = ref.repeatMinMs > since ? ref.repeatMinMs - since : 0;
                return true;
            }
            uint32 const age = e.firstSeenMs ? WorldTimer::getMSTimeDiff(e.firstSeenMs, nowMs) : 0;
            if (age < ref.initialMinMs)
                cooldownMs = ref.initialMinMs - age;
            else if (ref.OncePerFight())
                cooldownMs = 0;
            else
                cooldownMs = (ref.repeatMinMs + ref.repeatMaxMs) / 4;
            return true;
        }

        void AnnotateOne(EnemyFacts& e, uint32 nowMs)
        {
            if (!e.spellList.empty())
                return;     // a live creature_spells list
            std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(e.entry);
            if (!profile)
                return;
            for (CreatureSpellRef const& ref : profile->spells)
            {
                if (ref.source != SpellSource::Sheet)
                    continue;
                uint32 cooldownMs = 0;
                if (!Estimate(e, ref, nowMs, cooldownMs))
                    continue;
                ListSpellFacts f;
                f.spellId = ref.spellId;
                f.cooldownMs = cooldownMs;
                f.repeatMinMs = ref.repeatMinMs;
                f.repeatMaxMs = ref.repeatMaxMs;
                f.probability = 100;
                f.castTarget = ref.castTarget;
                f.castFlags = 0;
                f.once = ref.OncePerFight();
                e.spellList.push_back(f);
                e.spellListEstimated = true;
            }
            if (e.spellListEstimated)
                e.spellCheckInMs = 0;
        }
    }

    void Annotate(WorldModel& world, uint32 nowMs)
    {
        for (EnemyFacts& e : world.enemies)
            AnnotateOne(e, nowMs);
        for (EnemyFacts& e : world.targeted)
            AnnotateOne(e, nowMs);
    }
}
}
