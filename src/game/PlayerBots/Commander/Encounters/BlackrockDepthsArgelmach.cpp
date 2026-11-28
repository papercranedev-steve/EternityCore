/*
 * Golem Lord Argelmach module (header comment: script, before the pull).
 */

#include "Commander/Encounters/BlackrockDepthsArgelmach.h"
#include "Commander/Perception/Spatial/PackResolver.h"

namespace Commander
{
    void BlackrockDepthsArgelmach::BeforePull(WorldModel const& world, std::vector<BossPrep>& out) const
    {
        PackCreature const* boss = nullptr;
        for (PackFacts const& pack : world.packs)
            for (PackCreature const& c : pack.members)
                if (c.entry == NPC_ARGELMACH)
                    boss = &c;
        if (!boss)
            return;

        BossPrep prep;
        prep.boss = boss->guid;
        prep.bossPos = boss->pos;
        prep.voiceKey = "voice.boss.argelmach.golems";
        for (PackFacts const& pack : world.packs)
            for (PackCreature const& c : pack.members)
                if ((c.entry == NPC_RAGEREAVER || c.entry == NPC_WRATH_HAMMER) &&
                    PackResolver::Distance(c.pos, boss->pos) <= CALL_RANGE_YD)
                    prep.clearFirst.push_back(c.guid);
        out.push_back(prep);
    }
}
