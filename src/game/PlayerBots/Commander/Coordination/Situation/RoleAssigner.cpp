#include "Commander/Coordination/Situation/RoleAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        using RA = RoleAssigner;

        float Score(PartyMemberFacts const& m, CombatBotRoles role)
        {
            return m.cap.For(role) + (m.role == role ? RA::INCUMBENT_BONUS : 0.0f);
        }

        bool CanTank(PartyMemberFacts const& m) { return m.cap.tank >= RoleCapabilities::MIN_TANK; }
        bool CanHeal(PartyMemberFacts const& m) { return m.cap.heal >= RoleCapabilities::MIN_HEAL; }

        struct Choice
        {
            PartyMemberFacts const* tank = nullptr;
            PartyMemberFacts const* healer = nullptr;
            float score = -1000.0f;
        };

        // Party of up to 5: try every (tank, healer) pair (header comment).
        Choice ChooseSmall(std::vector<PartyMemberFacts const*> const& free, bool needTank, bool needHealer)
        {
            std::vector<PartyMemberFacts const*> tanks{ nullptr };
            std::vector<PartyMemberFacts const*> healers{ nullptr };
            for (PartyMemberFacts const* m : free)
            {
                if (needTank && CanTank(*m))
                    tanks.push_back(m);
                if (needHealer && CanHeal(*m))
                    healers.push_back(m);
            }
            Choice best;
            for (PartyMemberFacts const* t : tanks)
            {
                for (PartyMemberFacts const* h : healers)
                {
                    if (t && t == h)
                        continue;
                    float score = 0.0f;
                    score += t ? Score(*t, ROLE_TANK) : (needTank ? -RA::MISSING_ROLE : 0.0f);
                    score += h ? Score(*h, ROLE_HEALER) : (needHealer ? -RA::MISSING_ROLE : 0.0f);
                    for (PartyMemberFacts const* m : free)
                        if (m != t && m != h)
                            score += Score(*m, m->cap.BestNonTank());
                    if (score > best.score)
                    {
                        best.tank = t;
                        best.healer = h;
                        best.score = score;
                    }
                }
            }
            return best;
        }

        // Larger groups: best qualifying candidates, tanks first.
        void PickGreedy(std::vector<PartyMemberFacts const*>& free, CombatBotRoles role, uint32 count,
                        std::vector<PartyMemberFacts const*>& out)
        {
            for (uint32 i = 0; i < count; ++i)
            {
                auto best = free.end();
                for (auto itr = free.begin(); itr != free.end(); ++itr)
                {
                    bool const ok = role == ROLE_TANK ? CanTank(**itr) : CanHeal(**itr);
                    if (ok && (best == free.end() || Score(**itr, role) > Score(**best, role)))
                        best = itr;
                }
                if (best == free.end())
                    return;
                out.push_back(*best);
                free.erase(best);
            }
        }
    }

    void RoleAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        // Only between fights: a role change rebuilds the spell lists and switches the rotation.
        if (world.HasPartyEnemy() || plan.pull.phase != PullPhase::None || plan.clear.active || plan.retreat.active)
            return;

        ChooseRoles(world, plan);
        BuildCoverage(world, plan);
    }

    uint8 RoleAssigner::FunctionsOf(uint8 role)
    {
        switch (role)
        {
            case ROLE_TANK:      return FUNCTION_HOLD | FUNCTION_DAMAGE;
            case ROLE_MELEE_DPS:
            case ROLE_RANGE_DPS: return FUNCTION_DAMAGE;
            case ROLE_HEALER:    return FUNCTION_HEAL;
            default:             return 0;
        }
    }

    void RoleAssigner::BuildCoverage(WorldModel const& world, TeamPlan& plan)
    {
        CoveragePlan& cover = plan.coverage;
        cover = CoveragePlan();

        // The role each member plays after ApplyRoles: this refresh's choice for a free bot.
        auto roleOf = [&plan](PartyMemberFacts const& m) -> uint8
        {
            for (RoleDuty const& d : plan.roles)
                if (d.bot == m.guid)
                    return d.role;
            return m.role;
        };

        std::vector<PartyMemberFacts const*> bots;   // same order as cover.members' bots
        uint8 covered = 0;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet)
                continue;
            MemberCover c;
            c.member = m.guid;
            c.functions = FunctionsOf(roleOf(m));
            covered |= c.functions;
            cover.members.push_back(c);
            bots.push_back(m.isBot ? &m : nullptr);
        }

        // Fill the gaps: a function nobody has goes to the bots that can do it.
        auto add = [&](TeamFunction function, char const* reason, auto canDo)
        {
            if (covered & function)
                return;
            for (size_t i = 0; i < cover.members.size(); ++i)
            {
                if (!bots[i] || !canDo(*bots[i], cover.members[i]))
                    continue;
                cover.members[i].functions |= function;
                cover.members[i].added |= function;
                cover.members[i].reason = reason;
            }
        };
        // Nobody kills: every healer bot deals damage too (a lone healer, two healers).
        add(FUNCTION_DAMAGE, "nobody else deals damage",
            [](PartyMemberFacts const&, MemberCover const& c) { return (c.functions & FUNCTION_HEAL) != 0; });
        // Nobody heals: the bots that can (a retribution paladin next to a warrior).
        add(FUNCTION_HEAL, "nobody else heals",
            [](PartyMemberFacts const& m, MemberCover const&) { return m.cap.heal >= RoleCapabilities::MIN_HEAL; });
        // Nobody holds: not given to anyone, each member takes the enemies that hit it.
        cover.noHolder = !cover.members.empty() && !(covered & FUNCTION_HOLD);
    }

    void RoleAssigner::ChooseRoles(WorldModel const& world, TeamPlan& plan)
    {
        std::vector<PartyMemberFacts const*> free;
        uint32 members = 0;
        uint32 lockedTanks = 0;
        uint32 lockedHealers = 0;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet)
                continue;
            ++members;
            if (m.roleLocked)
            {
                lockedTanks += m.role == ROLE_TANK ? 1 : 0;
                lockedHealers += m.role == ROLE_HEALER ? 1 : 0;
            }
            else if (m.isBot)
                free.push_back(&m);
        }

        plan.roles.clear();
        if (free.empty())
            return;
        // Alone (a team of one): it must kill things itself, so never the healer role (a healer
        // gets no target, TargetAssigner); it heals itself in its damage role.
        if (world.IsSolo())
        {
            RoleDuty d;
            d.bot = free.front()->guid;
            d.role = free.front()->cap.BestSolo();
            d.reason = "alone: fights by itself";
            plan.roles.push_back(d);
            return;
        }
        if (members < 2)
            return;

        bool const small = members <= SMALL_PARTY;
        uint32 const needTanks = small ? NEED_TANKS_SMALL : NEED_TANKS_RAID;
        uint32 const needHealers = small ? NEED_HEALERS_SMALL : (members + SMALL_PARTY - 1) / SMALL_PARTY;
        uint32 const openTanks = lockedTanks >= needTanks ? 0 : needTanks - lockedTanks;
        uint32 const openHealers = lockedHealers >= needHealers ? 0 : needHealers - lockedHealers;

        std::vector<PartyMemberFacts const*> tanks;
        std::vector<PartyMemberFacts const*> healers;
        if (small)
        {
            Choice const c = ChooseSmall(free, openTanks > 0, openHealers > 0);
            if (c.tank)
                tanks.push_back(c.tank);
            if (c.healer)
                healers.push_back(c.healer);
        }
        else
        {
            std::vector<PartyMemberFacts const*> pool = free;
            PickGreedy(pool, ROLE_TANK, openTanks, tanks);
            PickGreedy(pool, ROLE_HEALER, openHealers, healers);
        }

        for (PartyMemberFacts const* m : free)
        {
            RoleDuty d;
            d.bot = m->guid;
            if (std::find(tanks.begin(), tanks.end(), m) != tanks.end())
            {
                d.role = ROLE_TANK;
                d.reason = "best tank of the party";
            }
            else if (std::find(healers.begin(), healers.end(), m) != healers.end())
            {
                d.role = ROLE_HEALER;
                d.reason = "best healer of the party";
            }
            else
            {
                d.role = m->cap.BestNonTank();
                d.reason = d.role == ROLE_HEALER ? "heals best (extra healer)" : "damage: its best role";
            }
            plan.roles.push_back(d);
        }
    }

    void RoleAssigner::Clear(TeamPlan& plan) const
    {
        plan.roles.clear();
        plan.coverage = CoveragePlan();
    }
}
