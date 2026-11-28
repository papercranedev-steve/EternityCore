#include "Commander/Perception/Forecast/ThreatWipe.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Profiles/CreatureProfile.h"
#include "Commander/Perception/Profiles/BossSheets.h"
#include "Commander/Perception/Profiles/SpellTags.h"
#include "Commander/Perception/Profiles/Tags.h"
#include "ScriptMgr.h"          // ScriptCommands.h: TARGET_T_x (creature_spells)
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
namespace ThreatWipe
{
    namespace
    {
        // The spell wipes threat by its data, or an ability sheet row of `profile` says its
        // script does (BOSS_ABILITY_THREAT_WIPE).
        bool Wipes(CreatureProfile const* profile, uint32 spellId)
        {
            if (SpellTags::WipesTargetThreat(sSpellMgr.GetSpellEntry(spellId)))
                return true;
            if (profile)
                for (CreatureSpellRef const& ref : profile->spells)
                    if (ref.spellId == spellId && (ref.sheetFlags & BOSS_ABILITY_THREAT_WIPE))
                        return true;
            return false;
        }

        void Keep(Next& best, uint32 spellId, float minSec, float maxSec)
        {
            if (best.valid && best.minSec <= minSec)
                return;
            best.valid = true;
            best.spellId = spellId;
            best.minSec = std::max(0.0f, minSec);
            best.maxSec = std::max(best.minSec, maxSec);
        }
    }

    Next Of(EnemyFacts const& e, uint32 nowMs)
    {
        Next best;
        if (!(Tags::RolesOf(e.entry) & TAG_THREAT_WIPE))
            return best;
        std::shared_ptr<CreatureProfile const> p = sProfileStore.GetCreature(e.entry);

        // The live creature_spells timers.
        bool listed = false;
        for (ListSpellFacts const& s : e.spellList)
        {
            if (!Wipes(p.get(), s.spellId))
                continue;
            listed = true;
            if (s.castTarget != TARGET_T_HOSTILE || s.probability < 100)
                continue;
            float const sec = s.cooldownMs / 1000.0f;
            Keep(best, s.spellId, std::max(sec, e.spellCheckInMs / 1000.0f), sec + LIST_CHECK_SEC);
        }
        if (listed)
            return best;

        // EventAI (no live timer): the profile's delays from its last cast or the fight's start.
        if (!p)
            return best;
        for (CreatureSpellRef const& ref : p->spells)
        {
            if (ref.source == SpellSource::OnHit || !Wipes(p.get(), ref.spellId))
                continue;
            if (ref.castTarget != 0xFF && ref.castTarget != TARGET_T_HOSTILE)
                continue;
            uint32 const last = e.LastCastStartMs(ref.spellId);
            uint32 const from = last ? last : e.firstSeenMs;
            uint32 const minMs = last ? ref.repeatMinMs : ref.initialMinMs;
            uint32 const maxMs = last ? ref.repeatMaxMs : ref.initialMaxMs;
            if (!from || !maxMs)
                continue;
            float const since = WorldTimer::getMSTimeDiff(from, nowMs) / 1000.0f;
            Keep(best, ref.spellId, minMs / 1000.0f - since, maxMs / 1000.0f - since);
        }
        return best;
    }
}
}
