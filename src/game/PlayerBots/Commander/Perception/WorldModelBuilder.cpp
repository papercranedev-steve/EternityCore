#include "Commander/Perception/WorldModelBuilder.h"
#include "Commander/Perception/PackResolver.h"
#include "PartyBotAI.h"
#include "Creature.h"
#include "CreatureGroups.h"
#include "Group.h"
#include "Pet.h"
#include "Player.h"
#include "Spell.h"
#include "ThreatManager.h"
#include "HostileRefManager.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <algorithm>

namespace Commander
{
namespace WorldModelBuilder
{
    namespace
    {
        Position3 PositionOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }

        void FillUnitFacts(Unit const* unit, PartyMemberFacts& f)
        {
            f.guid = unit->GetObjectGuid();
            f.alive = unit->IsAlive();
            f.classId = unit->GetClass();
            f.healthPct = unit->GetHealthPercent();
            f.manaPct = unit->GetPowerType() == POWER_MANA && unit->GetMaxPower(POWER_MANA)
                ? unit->GetPowerPercent(POWER_MANA) : -1.0f;
            f.pos = PositionOf(unit);
        }

        bool IgnoresAssistThreat(Unit const* enemy)
        {
            // ThreatManager::addThreat zeroes assist (heal/buff) threat in these states
            return enemy->HasUnitState(UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING | UNIT_STATE_ISOLATED) ||
                  (enemy->HasUnitState(UNIT_STATE_STUNNED) && enemy->HasBreakableByDamageAuraType(SPELL_AURA_MOD_STUN, 0));
        }

        uint32 CastingSpellId(Unit const* unit)
        {
            if (Spell const* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL))
                return spell->m_spellInfo->Id;
            if (Spell const* spell = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
                return spell->m_spellInfo->Id;
            return 0;
        }
    }

    void RefreshCombat(PartyBotAI* bot, WorldModel& world)
    {
        Player* me = bot->me;
        Map* map = me->GetMap();
        Group* group = me->GetGroup();

        world.party.clear();
        world.enemies.clear();
        world.leader = group ? group->GetLeaderGuid() : ObjectGuid();
        if (!group)
            return;

        // Party members and pets on this map.
        std::vector<Unit*> partyUnits;
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
        {
            Player* member = itr->getSource();
            if (!member || !member->IsInWorld() || member->GetMap() != map)
                continue;

            PartyMemberFacts f;
            FillUnitFacts(member, f);
            if (member->AI())
            {
                if (PartyBotAI const* ai = dynamic_cast<PartyBotAI const*>(member->AI()))
                {
                    f.isBot = true;
                    f.role = uint8(ai->m_role);
                }
            }
            world.party.push_back(f);
            partyUnits.push_back(member);

            if (Pet* pet = member->GetPet())
            {
                if (pet->IsInWorld() && pet->GetMap() == map)
                {
                    PartyMemberFacts pf;
                    FillUnitFacts(pet, pf);
                    pf.isPet = true;
                    pf.ownerGuid = member->GetObjectGuid();
                    world.party.push_back(pf);
                    partyUnits.push_back(pet);
                }
            }
        }

        // Every creature that has one of us on its threat list.
        std::vector<Creature*> enemies;
        for (Unit* unit : partyUnits)
        {
            for (HostileReference* ref = unit->GetHostileRefManager().getFirst(); ref; ref = ref->next())
            {
                Unit* owner = ref->getSource()->getOwner();
                if (!owner || !owner->IsCreature() || !owner->IsAlive() || !owner->IsInWorld() || owner->GetMap() != map)
                    continue;
                Creature* creature = static_cast<Creature*>(owner);
                if (std::find(enemies.begin(), enemies.end(), creature) == enemies.end())
                    enemies.push_back(creature);
            }
        }

        world.enemies.reserve(enemies.size());
        for (Creature* creature : enemies)
        {
            EnemyFacts e;
            e.guid = creature->GetObjectGuid();
            e.entry = creature->GetEntry();
            e.level = creature->GetLevel();
            e.elite = creature->IsElite();
            e.healthPct = creature->GetHealthPercent();
            e.pos = PositionOf(creature);
            e.castingSpellId = CastingSpellId(creature);
            e.crowdControlled = creature->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL);
            e.ignoresAssistThreat = IgnoresAssistThreat(creature);

            ThreatManager& tm = creature->GetThreatManager();
            if (HostileReference* victimRef = tm.getCurrentVictim())
            {
                e.victim = victimRef->getUnitGuid();
                e.victimThreat = victimRef->getThreat();
            }

            for (HostileReference* ref : tm.getThreatList())
            {
                ThreatEntry t;
                t.guid = ref->getUnitGuid();
                t.threat = ref->getThreat();
                t.onList = true;
                auto unitItr = std::find_if(partyUnits.begin(), partyUnits.end(),
                    [&t](Unit* u) { return u->GetObjectGuid() == t.guid; });
                if (unitItr != partyUnits.end())
                {
                    t.partySide = true;
                    t.inMeleeReach = creature->CanReachWithMeleeAutoAttack(*unitItr);
                }
                e.threat.push_back(t);
            }

            // Party members not on the list yet: one heal or hit puts them on it.
            for (Unit* unit : partyUnits)
            {
                if (!unit->IsAlive())
                    continue;
                ObjectGuid const guid = unit->GetObjectGuid();
                bool const listed = std::any_of(e.threat.begin(), e.threat.end(),
                    [guid](ThreatEntry const& t) { return t.guid == guid; });
                if (listed)
                    continue;

                ThreatEntry t;
                t.guid = guid;
                t.partySide = true;
                t.inMeleeReach = creature->CanReachWithMeleeAutoAttack(unit);
                e.threat.push_back(t);
            }

            std::stable_sort(e.threat.begin(), e.threat.end(), [](ThreatEntry const& a, ThreatEntry const& b)
            {
                return a.threat > b.threat;
            });

            world.enemies.push_back(std::move(e));
        }
    }

    void RefreshPacks(PartyBotAI* bot, WorldModel& world)
    {
        Player* me = bot->me;
        Player* leader = bot->GetPartyLeader();
        WorldObject* center = (leader && leader->IsInWorld() && leader->GetMap() == me->GetMap())
            ? static_cast<WorldObject*>(leader) : static_cast<WorldObject*>(me);

        std::list<Unit*> units;
        MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(center, me, PACK_SCAN_RADIUS);
        MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, check);
        Cell::VisitAllObjects(center, searcher, PACK_SCAN_RADIUS);

        std::vector<PackCreature> creatures;
        for (Unit* unit : units)
        {
            if (!unit->IsCreature() || unit->IsInCombat() || unit->GetOwnerGuid() ||
                unit->GetCreatureType() == CREATURE_TYPE_CRITTER || !me->IsValidAttackTarget(unit))
                continue;

            Creature* creature = static_cast<Creature*>(unit);
            if (creature->IsPet() || creature->IsTotem())
                continue;

            PackCreature c;
            c.guid = creature->GetObjectGuid();
            if (CreatureGroup const* cg = creature->GetCreatureGroup())
                c.dbGroupLeader = cg->GetOriginalLeaderGuid();
            c.entry = creature->GetEntry();
            c.level = creature->GetLevel();
            c.elite = creature->IsElite();
            c.aggressive = me->IsHostileTo(creature);
            c.pos = PositionOf(creature);
            creatures.push_back(c);
        }

        world.packs = PackResolver::Resolve(creatures, PositionOf(center));
    }
}
}
