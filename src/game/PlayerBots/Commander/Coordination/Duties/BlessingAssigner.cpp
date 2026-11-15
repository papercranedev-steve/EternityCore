#include "Commander/Coordination/Duties/BlessingAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "SharedDefines.h"
#include <algorithm>
#include <numeric>

namespace Commander
{
    namespace
    {
        bool UsesWisdom(uint8 classId, uint8 role)
        {
            switch (classId)
            {
                case CLASS_WARRIOR:
                case CLASS_ROGUE:
                    return false;
                case CLASS_PALADIN:
                case CLASS_SHAMAN:
                case CLASS_DRUID:
                    return role != ROLE_TANK && role != ROLE_MELEE_DPS;
                default:
                    return true;
            }
        }

        uint32 SpellOf(std::vector<PaladinBuffAbility> const& spells, uint8 kind)
        {
            for (PaladinBuffAbility const& s : spells)
                if (s.kind == kind)
                    return s.spellId;
            return 0;
        }

        bool Contains(std::vector<uint8> const& kinds, uint8 kind)
        {
            return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
        }

        struct Pick
        {
            PartyMemberFacts const* paladin = nullptr;
            uint32 spellId = 0;
        };

        // The paladins in `turn` order each take the first blessing of `order` they know that
        // is not taken. Score: a higher place outweighs any number of lower ones.
        uint32 Deal(std::vector<PartyMemberFacts const*> const& paladins, std::vector<size_t> const& turn,
                    std::vector<BlessingKind> const& order, std::vector<uint8> taken, std::vector<Pick>& out)
        {
            uint32 score = 0;
            for (size_t i : turn)
            {
                for (size_t place = 0; place < order.size(); ++place)
                {
                    uint8 const kind = uint8(order[place]);
                    if (Contains(taken, kind))
                        continue;
                    uint32 const spellId = SpellOf(paladins[i]->blessingSpells, kind);
                    if (!spellId)
                        continue;
                    out.push_back({ paladins[i], spellId });
                    taken.push_back(kind);
                    score += 1u << (order.size() - place);
                    break;
                }
            }
            return score;
        }
    }

    std::vector<BlessingKind> BlessingAssigner::Order(uint8 classId, uint8 role)
    {
        std::vector<BlessingKind> out;
        out.push_back(UsesWisdom(classId, role) ? BlessingKind::Wisdom : BlessingKind::Might);
        out.push_back(BlessingKind::Kings);
        if (role != ROLE_TANK)
            out.push_back(BlessingKind::Salvation);
        out.push_back(BlessingKind::Light);
        out.push_back(BlessingKind::Sanctuary);
        return out;
    }

    std::vector<PaladinAuraKind> BlessingAssigner::AuraOrder()
    {
        return { PaladinAuraKind::Devotion, PaladinAuraKind::Concentration, PaladinAuraKind::Retribution,
                 PaladinAuraKind::Sanctity, PaladinAuraKind::ShadowResistance, PaladinAuraKind::FrostResistance,
                 PaladinAuraKind::FireResistance };
    }

    void BlessingAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        BlessingPlan out;
        out.active = true;

        std::vector<PartyMemberFacts const*> paladins;
        for (PartyMemberFacts const& m : world.party)
            if (m.isBot && !m.isPet && m.alive && (!m.blessingSpells.empty() || !m.auraSpells.empty()))
                paladins.push_back(&m);
        std::stable_sort(paladins.begin(), paladins.end(), [](PartyMemberFacts const* a, PartyMemberFacts const* b)
            { return a->guid.GetRawValue() < b->guid.GetRawValue(); });
        if (paladins.empty())
        {
            plan.blessings = std::move(out);
            return;
        }
        auto isAssigned = [&paladins](ObjectGuid guid)
        {
            return std::any_of(paladins.begin(), paladins.end(), [guid](PartyMemberFacts const* p) { return p->guid == guid; });
        };

        // Auras.
        std::vector<uint8> auraTaken;
        for (PartyMemberFacts const& m : world.party)
            if (!m.isPet && m.ownAura && !isAssigned(m.guid))
                auraTaken.push_back(m.ownAura);
        std::vector<PaladinAuraKind> const auraOrder = AuraOrder();
        for (PartyMemberFacts const* p : paladins)
        {
            for (PaladinAuraKind kind : auraOrder)
            {
                if (Contains(auraTaken, uint8(kind)))
                    continue;
                uint32 const spellId = SpellOf(p->auraSpells, uint8(kind));
                if (!spellId)
                    continue;
                out.auras.push_back({ p->guid, spellId });
                auraTaken.push_back(uint8(kind));
                break;
            }
        }

        // Blessings, per member.
        for (PartyMemberFacts const& target : world.party)
        {
            if (target.isPet || !target.alive)
                continue;
            std::vector<BlessingKind> const order = Order(target.classId, target.role);
            std::vector<uint8> taken;
            for (BlessingOnFacts const& b : target.blessingsOn)
                if (!isAssigned(b.caster))
                    taken.push_back(b.kind);

            std::vector<size_t> turn(paladins.size());
            std::iota(turn.begin(), turn.end(), size_t(0));
            std::vector<Pick> best;
            uint32 bestScore = Deal(paladins, turn, order, taken, best);
            if (paladins.size() <= MAX_PERMUTED)
            {
                while (std::next_permutation(turn.begin(), turn.end()))
                {
                    std::vector<Pick> picks;
                    uint32 const score = Deal(paladins, turn, order, taken, picks);
                    if (score > bestScore)
                    {
                        bestScore = score;
                        best = std::move(picks);
                    }
                }
            }
            for (Pick const& p : best)
                out.blessings.push_back({ p.paladin->guid, target.guid, p.spellId });
        }
        plan.blessings = std::move(out);
    }

    void BlessingAssigner::Clear(TeamPlan& plan) const
    {
        plan.blessings = BlessingPlan();
    }
}
