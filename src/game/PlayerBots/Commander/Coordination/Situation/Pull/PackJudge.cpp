#include "Commander/Coordination/Situation/Pull/PackJudge.h"
#include "Commander/Coordination/Situation/Pull/Camps.h"
#include <algorithm>

namespace Commander
{
namespace PackJudge
{
    Verdict Judge(PackFacts const& pack, Input const& in, int32& camp)
    {
        camp = -1;
        auto any = [&pack](auto pred) { return std::any_of(pack.members.begin(), pack.members.end(), pred); };

        if (any([](PackCreature const& c) { return c.boss || c.tappedByOther || !c.claimedBy.IsEmpty(); }))
            return Verdict::Exclude;
        if (pack.maxLevel > in.leaderLevel + MAX_LEVEL_ABOVE)
            return Verdict::Exclude;
        if (any([](PackCreature const& c) { return c.comesAnyway; }))
            return Verdict::Pull;

        // Selected: a normal one is pulled; an elite only after asking (players select to look
        // at a creature's level too).
        bool const selected = any([&in](PackCreature const& c) { return c.aggressive && c.guid == in.selection; });
        bool const quest = any([](PackCreature const& c) { return c.questNeeded; });
        bool const elite = pack.eliteCount > 0;
        if ((selected || quest) && !elite)
            return Verdict::Pull;

        // A camp he fought himself or answered about (also "别管" for a quest's elites).
        camp = Camps::Find(in.camps, pack);
        if (camp >= 0)
            return in.camps[camp].pull ? Verdict::Pull : Verdict::Exclude;
        if (quest || selected)
            return Verdict::Ask;

        bool const startsFights = any([](PackCreature const& c) { return c.aggressive && !c.noAggro; });
        bool const allGray = !any([&in](PackCreature const& c) { return c.level > in.grayLevel; });
        if (!startsFights || allGray)
            return Verdict::Exclude;
        return in.wanders ? Verdict::Ask : Verdict::Exclude;
    }
}
}
