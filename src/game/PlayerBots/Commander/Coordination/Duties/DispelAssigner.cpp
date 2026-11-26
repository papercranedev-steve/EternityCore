#include "Commander/Coordination/Duties/DispelAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Values/DispelValue.h"
#include "Commander/Perception/Values/ValueModel.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        // Beyond the global cooldown: the removal's own cooldown (Blessing of Freedom 20 s), and
        // a blessing replacing the caster's other blessing on the member for the rest of the
        // fight (it comes back after: PaladinRotation blesses out of combat).
        float ExtraCost(WorldModel const& world, PartyMemberFacts const& caster,
                        PartyMemberFacts const& member, DispelAbility const& a)
        {
            float const fightSec = Value::FightRemainingSec(world);
            float cost = Value::CooldownPoints(a.cooldownSec, fightSec);
            if (a.blessing)
                for (BlessingOnFacts const& on : member.blessingsOn)
                    if (on.caster == caster.guid)
                        cost += Value::LostAction(world, member,
                            DispelAssigner::LOST_BLESSING_SHARE * std::min(fightSec, DispelAssigner::LOST_BLESSING_MAX_SEC));
            return cost;
        }
    }

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
                // The plan's retreat moves everyone: a root or slow then costs (DispelValue).
                bool const retreat = plan.retreat.active;
                DispelValue::Worth const worth = DispelValue::Of(world, m, d, 0.0f, retreat);
                // (a removal that leaves an immunity, Blessing of Freedom, is valued per ability
                // below: it only adds on a debuff the caster puts back)
                if (worth.points <= 0.0f && d.reapplySec <= 0.0f)
                    continue;

                ObjectGuid previousBot;
                for (DispelDuty const& p : plan.dispels)
                    if (p.target == m.guid && p.auraSpellId == d.spellId)
                        previousBot = p.bot;

                DutyNeed need;
                need.kind = DutyKind::Dispel;
                need.target = m.guid;
                need.auraSpellId = d.spellId;
                need.now = worth.control || worth.manaCritical;
                need.points = worth.points;
                need.detail = uint8((worth.control ? DETAIL_CONTROL : 0) | (worth.manaCritical ? DETAIL_MANA_CRITICAL : 0));

                // (DISPEL_NONE: no dispel type removes it, only a removal by mechanic)
                uint32 const typeBit = d.dispelType ? 1u << d.dispelType : 0;
                for (PartyMemberFacts const& b : world.party)
                {
                    if (!b.isBot || b.isPet || !b.CanAct())
                        continue;
                    float const distance = PackResolver::Distance(b.pos, m.pos);
                    float const cost = Value::LostAction(world, b, Value::GCD_SEC);
                    DutyCandidate best;
                    for (DispelAbility const& a : b.dispels)
                    {
                        bool const removes = (a.dispelMask & typeBit) || (a.freesMechanics & d.mechanicMask);
                        if (!a.ready || !removes || distance > a.maxRange || (m.charmed && !a.hostileOk))
                            continue;
                        float const points = a.immuneSec > 0.0f
                            ? DispelValue::Of(world, m, d, a.immuneSec, retreat).points : worth.points;
                        float const spent = cost + ExtraCost(world, b, m, a);
                        if (points <= spent)
                            continue;
                        DutyCandidate c;
                        c.bot = b.guid;
                        c.spellId = a.spellId;
                        c.rank = (b.guid == previousBot ? Value::LostAction(world, b, STICKY_SEC) : 0.0f) - spent;
                        c.value = points + c.rank;
                        c.valued = true;
                        if (!best.valued || c.value > best.value)
                            best = c;
                    }
                    if (best.valued)
                        need.candidates.push_back(best);   // one removal per bot: its best
                }
                if (need.candidates.empty())
                    continue;
                for (DutyCandidate const& c : need.candidates)
                    need.points = std::max(need.points, c.value - c.rank);
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
            d.control = (r.detail & DETAIL_CONTROL) != 0;
            d.manaCritical = (r.detail & DETAIL_MANA_CRITICAL) != 0;
            duties.push_back(d);
        }
        plan.dispels = std::move(duties);
    }
}
