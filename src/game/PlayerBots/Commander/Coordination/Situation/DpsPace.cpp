#include "Commander/Coordination/Situation/DpsPace.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/WorldModel.h"
#include "SharedDefines.h"
#include <algorithm>

namespace Commander
{
    float DpsPaceAssigner::ClassBase(uint8 classId)
    {
        switch (classId)
        {
            case CLASS_ROGUE: return 1.1f;
            case CLASS_WARRIOR: return 1.05f;
            case CLASS_MAGE: return 1.0f;
            case CLASS_WARLOCK: return 1.0f;
            case CLASS_HUNTER: return 1.0f;
            case CLASS_SHAMAN: return 0.9f;
            case CLASS_PRIEST: return 0.85f;
            case CLASS_DRUID: return 0.85f;
            case CLASS_PALADIN: return 0.8f;
            default: return 1.0f;
        }
    }

    float DpsPaceAssigner::Personal(ObjectGuid bot)
    {
        uint64 z = bot.GetRawValue() + 0x9E3779B97F4A7C15ULL;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z ^= z >> 31;
        float const u = float(z >> 40) / float(1ULL << 24);
        return PERSONAL_MIN + (PERSONAL_MAX - PERSONAL_MIN) * u;
    }

    bool DpsPaceAssigner::Paced(PartyMemberFacts const& m)
    {
        return m.isBot && !m.isPet && m.DealsDamage() && !m.HealsTeam() && !Tanks::IsTank(m);
    }

    PartyMemberFacts const* DpsPaceAssigner::PlayerOf(WorldModel const& world)
    {
        auto const fits = [](PartyMemberFacts const& m)
        {
            return !m.isBot && !m.isPet && m.DealsDamage() && !m.HealsTeam() && !Tanks::IsTank(m);
        };
        if (PartyMemberFacts const* leader = world.FindMember(world.leader))
            if (fits(*leader))
                return leader;
        for (PartyMemberFacts const& m : world.party)
            if (fits(m))
                return &m;
        return nullptr;
    }

    float DpsPaceAssigner::CapOf(WorldModel const& world, PartyMemberFacts const& bot, PartyMemberFacts const& player)
    {
        MemberDamage const* his = world.damage.Find(player.guid);
        MemberDamage const* its = world.damage.Find(bot.guid);
        if (!his || !its || his->fights < MIN_FIGHTS || his->dps <= 0.0f)
            return 0.0f;
        float const classRatio = ClassBase(bot.classId) / ClassBase(player.classId);
        float gearRatio = 1.0f;
        if (bot.gearLevel > 0.0f && player.gearLevel > 0.0f)
            gearRatio = std::max(GEAR_MIN, std::min(GEAR_MAX, bot.gearLevel / player.gearLevel));
        return his->dps * classRatio * gearRatio * (1.0f + its->areaShare * AREA_BONUS) * Personal(bot.guid);
    }

    void DpsPaceAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        DpsPacePlan& d = plan.dpsPace;
        bool const fighting = world.HasPartyEnemy();
        if (fighting)
            for (PartyMemberFacts const& m : world.party)
                if (!m.isPet && !m.alive)
                    d.deathThisFight = true;
        if (d.wasFighting && !fighting)
        {
            if (d.deathThisFight)
                d.fullFightsLeft = FULL_AFTER_DEATH;
            else if (d.fullFightsLeft)
                --d.fullFightsLeft;
            d.deathThisFight = false;
        }
        d.wasFighting = fighting;

        // Floors (header "Full speed now").
        d.reason.clear();
        if (d.fullFightsLeft)
            d.reason = "after a fight with a death";
        else if (fighting && std::any_of(world.enemies.begin(), world.enemies.end(),
                                         [](EnemyFacts const& e) { return e.boss && !e.onAllyOnly; }))
            d.reason = "boss";
        else if (plan.crisis.lost)
            d.reason = "crisis";
        else if (std::any_of(plan.rescues.begin(), plan.rescues.end(),
                             [](RescueDuty const& r) { return r.way == RescueWay::Save; }))
            d.reason = "someone about to die";
        else if (fighting && std::any_of(world.party.begin(), world.party.end(), [](PartyMemberFacts const& m)
                 { return m.isBot && !m.isPet && m.alive && m.HealsTeam() && m.manaPct >= 0.0f && m.manaPct < HEALER_MANA_PCT; }))
            d.reason = "healer mana";
        d.full = !d.reason.empty();

        // Step: a new fight kept.
        if (world.damage.keptTotal == d.seenKept)
            return;
        d.seenKept = world.damage.keptTotal;
        PartyMemberFacts const* player = world.humanInTeam ? PlayerOf(world) : nullptr;
        if (!player)
        {
            d.bots.clear();
            return;
        }
        std::vector<BotPace> next;
        for (PartyMemberFacts const& m : world.party)
        {
            if (!Paced(m))
                continue;
            BotPace p;
            p.bot = m.guid;
            for (BotPace const& old : d.bots)
                if (old.bot == m.guid)
                    p.pace = old.pace;
            p.capDps = CapOf(world, m, *player);
            MemberDamage const* its = world.damage.Find(m.guid);
            if (p.capDps > 0.0f && its)
            {
                if (its->dps > p.capDps)
                    p.pace -= PACE_STEP;
                else if (its->dps < p.capDps * RAISE_BELOW)
                    p.pace += PACE_STEP;
                p.pace = std::max(PACE_MIN, std::min(1.0f, p.pace));
            }
            next.push_back(p);
        }
        d.bots = std::move(next);
    }

    void DpsPaceAssigner::Clear(TeamPlan& plan) const
    {
        plan.dpsPace = DpsPacePlan();
    }
}
