#include "Commander/Perception/ThreatView.h"
#include "ThreatManager.h"
#include "HostileRefManager.h"
#include "Unit.h"
#include "SpellMgr.h"
#include "SpellEntry.h"
#include <algorithm>

namespace Commander
{
    PartyMemberFacts const* WorldModel::FindMember(ObjectGuid guid) const
    {
        for (PartyMemberFacts const& m : party)
            if (m.guid == guid)
                return &m;
        return nullptr;
    }

    EnemyFacts const* WorldModel::FindEnemy(ObjectGuid guid) const
    {
        for (EnemyFacts const& e : enemies)
            if (e.guid == guid)
                return &e;
        return nullptr;
    }

namespace ThreatView
{
    ThreatEntry const* FindEntry(EnemyFacts const& enemy, ObjectGuid who)
    {
        for (ThreatEntry const& t : enemy.threat)
            if (t.guid == who)
                return &t;
        return nullptr;
    }

    float AggroThreshold(EnemyFacts const& enemy, ThreatEntry const& who)
    {
        return enemy.victimThreat * (who.inMeleeReach ? MELEE_AGGRO_FACTOR : RANGED_AGGRO_FACTOR);
    }

    float Headroom(EnemyFacts const& enemy, ObjectGuid who)
    {
        if (enemy.victim.IsEmpty())
        {
            // No victim yet: the highest threat becomes the victim.
            float top = 0.0f;
            float own = 0.0f;
            for (ThreatEntry const& t : enemy.threat)
            {
                if (t.guid == who)
                    own = t.threat;
                else
                    top = std::max(top, t.threat);
            }
            return top - own;
        }

        if (enemy.victim == who)
            return NO_LIMIT;

        ThreatEntry const* entry = FindEntry(enemy, who);
        if (!entry)
            return NO_LIMIT;    // not a party-side unit we track

        return AggroThreshold(enemy, *entry) - entry->threat;
    }

    float VictimLead(EnemyFacts const& enemy, ObjectGuid* closestOut)
    {
        float lead = NO_LIMIT;
        for (ThreatEntry const& t : enemy.threat)
        {
            if (t.guid == enemy.victim || !t.partySide)
                continue;

            float const h = AggroThreshold(enemy, t) - t.threat;
            if (h < lead)
            {
                lead = h;
                if (closestOut)
                    *closestOut = t.guid;
            }
        }
        return lead;
    }

    bool WouldPullAggro(EnemyFacts const& enemy, ObjectGuid who, float addedThreat)
    {
        return addedThreat > Headroom(enemy, who);
    }

    float WorstHeadroomAfter(WorldModel const& world, ObjectGuid who,
                             std::vector<EnemyThreatDelta> const& deltas, ObjectGuid* worstEnemyOut)
    {
        float worst = NO_LIMIT;
        for (EnemyThreatDelta const& d : deltas)
        {
            EnemyFacts const* enemy = world.FindEnemy(d.enemy);
            if (!enemy)
                continue;

            float const after = Headroom(*enemy, who) - d.added;
            if (after < worst)
            {
                worst = after;
                if (worstEnemyOut)
                    *worstEnemyOut = d.enemy;
            }
        }
        return worst;
    }

    void PredictHealThreat(Unit* healer, Unit* healTarget, float effectiveHeal,
                           SpellEntry const* spell, std::vector<EnemyThreatDelta>& out)
    {
        if (!healer || !healTarget || effectiveHeal <= 0.0f)
            return;

        if (spell && spell->HasAttribute(SPELL_ATTR_EX4_NO_HELPFUL_THREAT))
            return;

        HostileRefManager& refs = healTarget->GetHostileRefManager();
        uint32 const count = refs.getSize();
        if (!count)
            return;

        float const classModifier = healer->GetClass() == CLASS_PALADIN ? 0.25f : 0.5f;
        float const total = effectiveHeal * classModifier * sSpellMgr.GetSpellThreatMultiplier(spell);
        float const perEnemy = total / count;
        SpellSchoolMask const school = spell ? spell->GetSpellSchoolMask() : SPELL_SCHOOL_MASK_NORMAL;

        for (HostileReference* ref = refs.getFirst(); ref; ref = ref->next())
        {
            Unit* pEnemy = ref->getSource()->getOwner();
            if (!pEnemy || !pEnemy->IsAlive())
                continue;

            float threat = perEnemy;
            // same exemption as ThreatManager::addThreat for assist threat
            if (pEnemy->HasUnitState(UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING | UNIT_STATE_ISOLATED) ||
               (pEnemy->HasUnitState(UNIT_STATE_STUNNED) && pEnemy->HasBreakableByDamageAuraType(SPELL_AURA_MOD_STUN, 0)))
                threat = 0.0f;

            EnemyThreatDelta delta;
            delta.enemy = pEnemy->GetObjectGuid();
            delta.added = ThreatCalcHelper::CalcThreat(healer, threat, false, school, spell);
            out.push_back(delta);
        }
    }
}
}
