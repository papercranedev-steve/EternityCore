#include "Commander/Coordination/Duties/PurgeAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Values/ManaWorth.h"
#include "Commander/Perception/Values/ValueModel.h"
#include <algorithm>

namespace Commander
{
    float PurgeAssigner::Worth(WorldModel const& world, EnemyFacts const& e, EnemyBuffFacts const& buff)
    {
        float sec = Value::EnemyTimeToDie(world, e);
        if (buff.remainingMs)
            sec = std::min(sec, buff.remainingMs / 1000.0f);
        sec = std::min(sec, Value::FightRemainingSec(world));
        if (sec <= 0.0f)
            return 0.0f;

        float points = 0.0f;
        // A melee boost on a member's attacker (header).
        if (e.meleeEnabled && e.meleeDps > 0.0f && world.FindMember(e.victim))
        {
            float extraDps = e.meleeDps * std::max(0.0f, 1.0f - buff.periodMult / std::max(0.01f, buff.damageMult));
            if (buff.damageFlat > 0.0f && e.swingPeriodMs)
                extraDps += buff.damageFlat / (e.swingPeriodMs / 1000.0f);
            points += Value::Points(world, extraDps * sec);
        }
        if (buff.absorb > 0.0f)
            points += Value::FightExtension(world, buff.absorb / std::max(1.0f, Value::DealtDps(world)));
        return points;
    }

    void PurgeAssigner::Offer(WorldModel const& world, TeamPlan const& plan, std::vector<DutyNeed>& out) const
    {
        if (!world.HasPartyEnemy())
            return;
        float const fightSec = Value::FightRemainingSec(world);
        for (EnemyFacts const& e : world.enemies)
        {
            if (!e.acting || !e.health)
                continue;
            for (EnemyBuffFacts const& buff : e.buffs)
            {
                float const worth = Worth(world, e, buff);
                if (worth <= 0.0f)
                    continue;
                ObjectGuid previousBot;
                for (PurgeDuty const& p : plan.purges)
                    if (p.enemy == e.guid && p.auraSpellId == buff.spellId)
                        previousBot = p.bot;

                DutyNeed need;
                need.kind = DutyKind::Purge;
                need.target = e.guid;
                need.auraSpellId = buff.spellId;
                need.points = worth;
                uint32 const typeBit = 1u << buff.dispelType;
                for (PartyMemberFacts const& b : world.party)
                {
                    if (!b.isBot || b.isPet || !b.CanAct())
                        continue;
                    float const distance = PackResolver::Distance(b.pos, e.pos) - e.boundingRadius;
                    DutyCandidate best;
                    for (DispelAbility const& a : b.purges)
                    {
                        if (!a.ready || !(a.dispelMask & typeBit) || distance > a.maxRange)
                            continue;
                        float const spent = Value::LostAction(world, b, Value::GCD_SEC) +
                                            ManaWorth::Points(world, b, float(a.manaCost)) +
                                            Value::CooldownPoints(a.cooldownSec, fightSec);
                        if (worth <= spent)
                            continue;
                        DutyCandidate c;
                        c.bot = b.guid;
                        c.spellId = a.spellId;
                        c.rank = (b.guid == previousBot ? Value::LostAction(world, b, STICKY_SEC) : 0.0f) - spent;
                        c.value = worth + c.rank;
                        c.valued = true;
                        if (!best.valued || c.value > best.value)
                            best = c;
                    }
                    if (best.valued)
                        need.candidates.push_back(best);
                }
                if (!need.candidates.empty())
                    out.push_back(std::move(need));
            }
        }
    }

    void PurgeAssigner::Apply(WorldModel const& /*world*/, std::vector<DutyResult> const& results, TeamPlan& plan) const
    {
        std::vector<PurgeDuty> duties;
        for (DutyResult const& r : results)
        {
            if (r.kind != DutyKind::Purge || r.primary.IsEmpty())
                continue;
            PurgeDuty d;
            d.enemy = r.target;
            d.auraSpellId = r.auraSpellId;
            d.bot = r.primary;
            d.spellId = r.primarySpellId;
            d.points = r.points;
            duties.push_back(d);
        }
        plan.purges = std::move(duties);
    }

    void PurgeAssigner::Clear(TeamPlan& plan) const
    {
        plan.purges.clear();
    }
}
