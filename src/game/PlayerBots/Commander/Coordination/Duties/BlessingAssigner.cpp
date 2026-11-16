
#include "Commander/Coordination/Duties/BlessingAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "SharedDefines.h"
#include <algorithm>
#include <map>
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

        PaladinBuffAbility const* AbilityOf(std::vector<PaladinBuffAbility> const& spells, uint8 kind)
        {
            for (PaladinBuffAbility const& s : spells)
                if (s.kind == kind && s.spellId)
                    return &s;
            return nullptr;
        }

        bool Contains(std::vector<uint8> const& kinds, uint8 kind)
        {
            return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
        }

        struct Pick
        {
            size_t paladin = 0;     // index into the paladin list
            uint8 kind = 0;
        };

        struct Dealt
        {
            uint32 score = 0;
            uint32 kept = 0;        // picks equal to the paladin's class-wide one
            std::vector<Pick> picks;
        };

        // The paladins in `turn` order each take the first blessing of `order` they know that
        // is not taken. Score: a higher place outweighs any number of lower ones.
        // classKinds[i]: paladin i's blessing for the member's class (0 = none).
        Dealt Deal(std::vector<PartyMemberFacts const*> const& paladins, std::vector<size_t> const& turn,
                   std::vector<BlessingKind> const& order, std::vector<uint8> taken,
                   std::vector<uint8> const& classKinds)
        {
            Dealt out;
            for (size_t i : turn)
            {
                for (size_t place = 0; place < order.size(); ++place)
                {
                    uint8 const kind = uint8(order[place]);
                    if (Contains(taken, kind) || !AbilityOf(paladins[i]->blessingSpells, kind))
                        continue;
                    out.picks.push_back({ i, kind });
                    taken.push_back(kind);
                    out.score += 1u << (order.size() - place);
                    if (classKinds[i] == kind)
                        ++out.kept;
                    break;
                }
            }
            return out;
        }

        // Every order of the paladins (up to MAX_PERMUTED, else guid order): the higher score,
        // then the more class-wide blessings kept (ties: the first order).
        Dealt BestDeal(std::vector<PartyMemberFacts const*> const& paladins, std::vector<BlessingKind> const& order,
                       std::vector<uint8> const& taken, std::vector<uint8> const& classKinds)
        {
            std::vector<size_t> turn(paladins.size());
            std::iota(turn.begin(), turn.end(), size_t(0));
            Dealt best = Deal(paladins, turn, order, taken, classKinds);
            if (paladins.size() > BlessingAssigner::MAX_PERMUTED)
                return best;
            while (std::next_permutation(turn.begin(), turn.end()))
            {
                Dealt d = Deal(paladins, turn, order, taken, classKinds);
                if (d.score > best.score || (d.score == best.score && d.kept > best.kept))
                    best = std::move(d);
            }
            return best;
        }
    }

    std::vector<BlessingKind> BlessingAssigner::Order(uint8 classId, uint8 role)
    {
        if (role == ROLE_TANK)
            return { BlessingKind::Light, BlessingKind::Kings, BlessingKind::Might, BlessingKind::Sanctuary };
        if (classId == CLASS_WARLOCK)
            return { BlessingKind::Salvation, BlessingKind::Kings, BlessingKind::Light, BlessingKind::Wisdom,
                     BlessingKind::Sanctuary };
        return { UsesWisdom(classId, role) ? BlessingKind::Wisdom : BlessingKind::Might, BlessingKind::Kings,
                 BlessingKind::Salvation, BlessingKind::Light, BlessingKind::Sanctuary };
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
                PaladinBuffAbility const* aura = nullptr;
                for (PaladinBuffAbility const& a : p->auraSpells)
                    if (a.kind == uint8(kind) && a.spellId)
                        aura = &a;
                if (!aura)
                    continue;
                out.auras.push_back({ p->guid, aura->spellId });
                auraTaken.push_back(uint8(kind));
                break;
            }
        }

        // Blessings.
        std::vector<PartyMemberFacts const*> targets;
        for (PartyMemberFacts const& m : world.party)
            if (!m.isPet && m.alive)
                targets.push_back(&m);
        auto takenOn = [&isAssigned](PartyMemberFacts const& target)
        {
            std::vector<uint8> taken;
            for (BlessingOnFacts const& b : target.blessingsOn)
                if (!isAssigned(b.caster))
                    taken.push_back(b.kind);
            return taken;
        };

        // Per class: dealt for its first member that is not a tank (else its first member).
        std::map<uint8, PartyMemberFacts const*> representatives;
        for (PartyMemberFacts const* t : targets)
        {
            PartyMemberFacts const*& rep = representatives[t->classId];
            if (!rep || (rep->role == ROLE_TANK && t->role != ROLE_TANK))
                rep = t;
        }
        std::map<uint8, std::vector<uint8>> classKinds;
        for (auto const& entry : representatives)
        {
            PartyMemberFacts const& rep = *entry.second;
            std::vector<uint8> kinds(paladins.size(), 0);
            Dealt const d = BestDeal(paladins, Order(rep.classId, rep.role), takenOn(rep), kinds);
            for (Pick const& p : d.picks)
                kinds[p.paladin] = p.kind;
            classKinds[entry.first] = std::move(kinds);
        }

        // Per member: the class-wide one as a Greater Blessing where it fits the member, a
        // plain one where the member wants another (a tank in a class of damage dealers).
        for (PartyMemberFacts const* t : targets)
        {
            std::vector<uint8> const& kinds = classKinds[t->classId];
            Dealt const d = BestDeal(paladins, Order(t->classId, t->role), takenOn(*t), kinds);
            for (Pick const& p : d.picks)
            {
                PaladinBuffAbility const* a = AbilityOf(paladins[p.paladin]->blessingSpells, p.kind);
                uint32 const spellId = kinds[p.paladin] == p.kind && a->greaterSpellId ? a->greaterSpellId : a->spellId;
                out.blessings.push_back({ paladins[p.paladin]->guid, t->guid, spellId });
            }
        }
        plan.blessings = std::move(out);
    }

    void BlessingAssigner::Clear(TeamPlan& plan) const
    {
        plan.blessings = BlessingPlan();
    }
}
