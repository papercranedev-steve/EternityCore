#include "Commander/Coordination/Situation/Crisis/CrisisAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Forecast/FightLoad.h"
#include "Commander/Perception/Profiles/StandIn.h"
#include <cstdio>

namespace Commander
{
    namespace
    {
        // Bots that could heal besides the healer bots (header "Spare healers"), not `except`.
        std::vector<ObjectGuid> SpareHealers(WorldModel const& world, ObjectGuid except)
        {
            std::vector<ObjectGuid> out;
            for (PartyMemberFacts const& m : world.party)
                if (m.isBot && !m.isPet && m.CanAct() && m.role != ROLE_HEALER && m.role != ROLE_TANK &&
                    !m.spareHealRates.empty() &&
                    m.UsesMana() && m.guid != except)
                    out.push_back(m.guid);
            return out;
        }

        // Runs the fight for `option` (its stand-in and healers); false when it cannot be run.
        bool Forecast(WorldModel const& world, CrisisOption& option, float armorMult)
        {
            FightLoad::Lineup lineup;
            lineup.tank = option.standIn;
            lineup.tankArmorMult = armorMult;
            lineup.spareHealers = option.healers;
            FightLoad::Input in;
            if (!FightLoad::FromWorld(world, lineup, in) || in.foes.empty())
                return false;
            FightLoad::Result const r = FightLoad::Simulate(in, "crisis");
            option.bearable = FightLoad::Bearable(r);
            option.score = FightLoad::Score(r);
            option.forecast = FightLoad::Describe(r);
            return true;
        }

        void Add(WorldModel const& world, CrisisPlan& c, CrisisOption option, float armorMult)
        {
            if (Forecast(world, option, armorMult))
                c.options.push_back(std::move(option));
        }

        std::string Note(CrisisPlan const& c)
        {
            std::string what;
            if ((c.lost & CRISIS_TANK) && (c.lost & CRISIS_HEALER))
                what = "Tank and healer down.";
            else if (c.lost & CRISIS_TANK)
                what = "Tank down.";
            else
                what = "Healer down.";
            if (c.best < 0)
                return what + ((c.lost & CRISIS_TANK) ? " Nobody can stand in." : " Nothing left to forecast.");
            CrisisOption const& o = c.options[c.best];
            char buf[256];
            std::string who;
            if (c.lost & CRISIS_TANK)
                who = std::string(StandIn::Describe(o.way)) + " tanks";
            if (!o.healers.empty())
            {
                snprintf(buf, sizeof(buf), "%s%u spare healer(s) heal", who.empty() ? "" : ", ", uint32(o.healers.size()));
                who += buf;
            }
            if (who.empty())
                who = "nobody else heals";
            snprintf(buf, sizeof(buf), " Best: %s: %s (%s).", who.c_str(), o.bearable ? "we can win" : "we lose",
                o.forecast.c_str());
            return what + buf;
        }

        std::string KeyOf(CrisisPlan const& c)
        {
            if (c.best < 0)
                return std::to_string(c.lost) + "|none";
            CrisisOption const& o = c.options[c.best];
            return std::to_string(c.lost) + "|" + std::to_string(o.standIn.GetRawValue()) + "|" +
                   std::to_string(o.healers.size()) + "|" + (o.bearable ? "1" : "0");
        }
    }

    uint8 CrisisAssigner::LossOf(WorldModel const& world)
    {
        if (!world.HasPartyEnemy())
            return 0;
        bool hadTank = false, tankUp = false, hadHealer = false, healerUp = false;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet)
                continue;
            if (m.role == ROLE_TANK)
            {
                hadTank = true;
                tankUp |= m.alive;
            }
            else if (m.role == ROLE_HEALER)
            {
                hadHealer = true;
                healerUp |= m.alive && (!m.UsesMana() || m.manaPct < 0.0f || m.manaPct >= HEALER_OOM_PCT);
            }
        }
        uint8 lost = 0;
        if (hadTank && !tankUp)
            lost |= CRISIS_TANK;
        if (hadHealer && !healerUp)
            lost |= CRISIS_HEALER;
        return lost;
    }

    void CrisisAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        CrisisPlan& c = plan.crisis;
        if (!world.HasPartyEnemy())
            plan.emergencyRoles.clear();    // the fight is over: the old roles come back
        uint8 const lost = LossOf(world);
        if (!lost)
        {
            c = CrisisPlan();
            return;
        }
        if (lost != c.lost)
        {
            c.lost = lost;
            c.sinceMs = world.refreshedAtMs;
        }
        c.options.clear();
        c.best = -1;

        if (lost & CRISIS_TANK)
        {
            for (PartyMemberFacts const& m : world.party)
            {
                StandInFacts const s = StandIn::Of(m);
                if (s.way == StandInWay::None)
                    continue;
                CrisisOption o;
                o.standIn = m.guid;
                o.way = s.way;
                if (lost & CRISIS_HEALER)
                    o.healers = SpareHealers(world, m.isPet ? ObjectGuid() : m.guid);
                Add(world, c, std::move(o), s.armorMult);
            }
        }
        else
        {
            std::vector<ObjectGuid> const spares = SpareHealers(world, ObjectGuid());
            Add(world, c, CrisisOption(), 1.0f);
            for (ObjectGuid g : spares)
            {
                CrisisOption o;
                o.healers.push_back(g);
                Add(world, c, std::move(o), 1.0f);
            }
            if (spares.size() > 1)
            {
                CrisisOption o;
                o.healers = spares;
                Add(world, c, std::move(o), 1.0f);
            }
        }

        for (size_t i = 0; i < c.options.size(); ++i)
            if (c.best < 0 || c.options[i].score > c.options[c.best].score)
                c.best = int(i);
        // Sticky: the last best stays while it is about as good (no flip-flop of notes).
        for (size_t i = 0; c.best >= 0 && !c.notedKey.empty() && i < c.options.size(); ++i)
        {
            CrisisOption const& o = c.options[i];
            if (o.standIn == c.lastStandIn && o.healers.size() == c.lastHealers &&
                o.bearable == c.options[c.best].bearable && o.score >= c.options[c.best].score - STICKY_SCORE)
            {
                c.best = int(i);
                break;
            }
        }
        if (c.best >= 0)
        {
            c.lastStandIn = c.options[c.best].standIn;
            c.lastHealers = c.options[c.best].healers.size();
        }

        std::string const key = KeyOf(c);
        if (key != c.notedKey)
        {
            c.notedKey = key;
            plan.notes.push_back(Note(c));
        }

        // Header "Takeover": the best option's bots take their roles for the rest of the fight.
        if (c.best < 0)
            return;
        CrisisOption const& best = c.options[c.best];
        auto give = [&](ObjectGuid bot, CombatBotRoles role, StandInWay way)
        {
            for (EmergencyRole const& r : plan.emergencyRoles)
                if (r.bot == bot)
                    return;
            EmergencyRole r;
            r.bot = bot;
            r.role = uint8(role);
            r.way = way;
            plan.emergencyRoles.push_back(r);
        };
        PartyMemberFacts const* standIn = world.FindMember(best.standIn);
        if (standIn && !standIn->isPet)     // a pet holds by its taunt on autocast (ThreatBehavior)
            give(standIn->guid, ROLE_TANK, best.way);
        for (ObjectGuid g : best.healers)
            give(g, ROLE_HEALER, StandInWay::None);
    }

    void CrisisAssigner::Clear(TeamPlan& plan) const
    {
        plan.crisis = CrisisPlan();
        plan.emergencyRoles.clear();
    }
}
