#include "Commander/Coordination/Duties/DispelAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Values/DispelValue.h"
#include "Commander/Perception/Values/ValueModel.h"

namespace Commander
{
    void DispelAssigner::Clear(TeamPlan& plan) const
    {
        plan.dispels.clear();
    }

    void DispelAssigner::Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const
    {
        if (!world.HasPartyEnemy())
            return;     // out of combat the rotation dispels

        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet || !m.alive)
                continue;
            for (DebuffFacts const& d : m.debuffs)
            {
                DispelValue::Worth const worth = DispelValue::Of(world, m, d);
                if (worth.points <= 0.0f)
                    continue;

                ObjectGuid previousBot;
                for (DispelDuty const& p : plan.dispels)
                    if (p.target == m.guid && p.auraSpellId == d.spellId)
                        previousBot = p.bot;

                DutyNeed need;
                need.kind = DutyKind::Dispel;
                need.target = m.guid;
                need.auraSpellId = d.spellId;
                need.now = worth.control;
                need.points = worth.points;
                need.detail = worth.control ? 1 : 0;

                uint32 const typeBit = 1u << d.dispelType;
                for (PartyMemberFacts const& b : world.party)
                {
                    if (!b.isBot || b.isPet || !b.CanAct())
                        continue;
                    float const distance = PackResolver::Distance(b.pos, m.pos);
                    for (DispelAbility const& a : b.dispels)
                    {
                        if (!a.ready || !(a.dispelMask & typeBit) || distance > a.maxRange ||
                            (m.charmed && !a.hostileOk))
                            continue;
                        float const cost = Value::LostAction(world, b, Value::GCD_SEC);
                        if (worth.points <= cost)
                            continue;
                        DutyCandidate c;
                        c.bot = b.guid;
                        c.spellId = a.spellId;
                        c.rank = (b.guid == previousBot ? Value::LostAction(world, b, STICKY_SEC) : 0.0f) - cost;
                        need.candidates.push_back(c);
                        break;  // one dispel of the right type per bot
                    }
                }
                if (!need.candidates.empty())
                    out.push_back(std::move(need));
            }
        }
    }

    void DispelAssigner::Apply(WorldModel const& /*world*/, std::vector<DutyResult> const& results, TeamPlan& plan) const
    {
        std::vector<DispelDuty> duties;
        for (DutyResult const& r : results)
        {
            if (r.kind != DutyKind::Dispel || r.primary.IsEmpty())
                continue;
            DispelDuty d;
            d.target = r.target;
            d.auraSpellId = r.auraSpellId;
            d.bot = r.primary;
            d.spellId = r.primarySpellId;
            d.points = r.points;
            d.control = r.detail != 0;
            duties.push_back(d);
        }
        plan.dispels = std::move(duties);
    }
}
