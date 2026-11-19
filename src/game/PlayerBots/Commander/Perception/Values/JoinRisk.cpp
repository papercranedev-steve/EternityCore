#include "Commander/Perception/Values/JoinRisk.h"
#include "Commander/Perception/Forecast/FightLoad.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Trace/Trace.h"
#include "Creature.h"
#include <algorithm>
#include <vector>

namespace Commander
{
namespace JoinRisk
{
    namespace
    {
        struct Kept
        {
            ObjectGuid leader;          // the party (WorldModel::leader)
            uint32 version = 0;
            ObjectGuid guid;
            float loss = 0.0f;
        };
        // Per map thread (parties update on their map's thread).
        thread_local std::vector<Kept> s_kept;
    }

    std::vector<ObjectGuid> Joiners(WorldModel const& world, ObjectGuid creature)
    {
        std::vector<ObjectGuid> out{ creature };
        for (PackFacts const& pack : world.packs)
        {
            if (std::none_of(pack.members.begin(), pack.members.end(),
                             [creature](PackCreature const& c) { return c.guid == creature; }))
                continue;
            for (PackCreature const& c : pack.members)
                if (c.guid != creature)
                    out.push_back(c.guid);
            break;
        }
        return out;
    }

    float Loss(WorldModel const& world, Creature const* creature)
    {
        ObjectGuid const guid = creature->GetObjectGuid();
        s_kept.erase(std::remove_if(s_kept.begin(), s_kept.end(),
            [&world](Kept const& k) { return k.leader == world.leader && k.version != world.version; }), s_kept.end());
        if (s_kept.size() > 256)
            s_kept.clear();
        for (Kept const& k : s_kept)
            if (k.leader == world.leader && k.guid == guid)
                return k.loss;

        float loss = -1.0f;
        PartyMemberFacts const* tank = Tanks::HitTaker(world);
        FightLoad::Input base;
        if (tank && tank->alive && FightLoad::FromWorld(world, base))
        {
            base.partyDps = Value::ActingDps(world);
            FightLoad::Input with = base;
            // Its pack comes with it.
            bool inPack = false;
            for (PackFacts const& pack : world.packs)
            {
                if (std::none_of(pack.members.begin(), pack.members.end(),
                                 [guid](PackCreature const& c) { return c.guid == guid; }))
                    continue;
                for (PackCreature const& c : pack.members)
                    with.foes.push_back(FightLoad::FoeOf(c, *tank, ARRIVE_SEC));
                inPack = true;
                break;
            }
            if (!inPack)
            {
                FightLoad::Foe f = FightLoad::FoeOf(creature, *tank);
                f.arriveSec = ARRIVE_SEC;
                with.foes.push_back(f);
            }
            float const penalty = Value::DeathPenalty(*tank);
            float const asIs = FightLoad::Loss(world, FightLoad::Simulate(base, "join risk: as is"), penalty);
            float const joined = FightLoad::Loss(world, FightLoad::Simulate(with, "join risk: it joins"), penalty);
            loss = std::max(0.0f, joined - asIs);
            if (Trace::On())
                Trace::Emit("joinrisk", Trace::Format("%s joins: %.1f points (dps %.0f)", creature->GetName(), loss, base.partyDps),
                            Trace::Format("{\"creature\":\"%s\",\"loss\":%.2f,\"asIs\":%.2f,\"joined\":%.2f,\"dps\":%.1f,\"pack\":%s}",
                                          Trace::Guid(guid).c_str(), loss, asIs, joined, base.partyDps, inPack ? "true" : "false"));
        }
        Kept k;
        k.leader = world.leader;
        k.version = world.version;
        k.guid = guid;
        k.loss = loss;
        s_kept.push_back(k);
        return loss;
    }
}
}
