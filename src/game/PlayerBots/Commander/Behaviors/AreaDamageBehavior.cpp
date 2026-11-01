#include "Commander/Behaviors/AreaDamageBehavior.h"
#include "Commander/Behaviors/ThreatBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ThreatView.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/CommanderMgr.h"
#include "PartyBotAI.h"
#include "DamageSpells.h"
#include "DBCStores.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        using AB = AreaDamageBehavior;

        float AreaRadius(SpellEntry const* spell)
        {
            float radius = 0.0f;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (SpellRadiusEntry const* r = sSpellRadiusStore.LookupEntry(spell->EffectRadiusIndex[i]))
                    radius = std::max(radius, r->Radius);
            return radius;
        }

        Position3 PositionOf(Unit const* unit)
        {
            Position3 p;
            p.x = unit->GetPositionX();
            p.y = unit->GetPositionY();
            p.z = unit->GetPositionZ();
            return p;
        }

        struct Judgement
        {
            uint32 targets = 0;
            float damage = 0.0f;        // over all targets, each at most its health
            bool unsafe = false;
        };

        Judgement Judge(PartyBotAI const* bot, WorldModel const& world, AreaDamageSpell const& area,
                        Unit* victim, float perTarget, float busySec)
        {
            Player* me = bot->me;
            ObjectGuid const self = me->GetObjectGuid();
            Position3 const here = PositionOf(me);
            Position3 const centre = area.onTarget ? PositionOf(victim) : here;
            float const radius = AreaRadius(area.spell);
            float const threat = me->ApplyTotalThreatModifier(
                perTarget * sSpellMgr.GetSpellThreatMultiplier(area.spell), area.spell->GetSpellSchoolMask());
            uint32 const settleMs = uint32(ThreatBehavior::NEW_ENEMY_SETTLE_SEC * 1000.0f);

            Judgement j;
            for (EnemyFacts const& e : world.enemies)
            {
                if (!e.acting || e.crowdControlled)
                    continue;
                if (PackResolver::Distance(centre, e.pos) - e.boundingRadius > radius)
                    continue;
                ++j.targets;
                j.damage += std::min(perTarget, float(e.health));
                if (e.victim == self || e.victim.IsEmpty())
                    continue;

                if (e.firstSeenMs && WorldTimer::getMSTimeDiff(e.firstSeenMs, world.refreshedAtMs) < settleMs)
                {
                    j.unsafe = true;
                    continue;
                }
                ThreatEntry const* mine = ThreatView::FindEntry(e, self);
                bool const melee = PackResolver::Distance(here, e.pos) - e.boundingRadius <= AB::MELEE_REACH;
                float const factor = melee ? ThreatView::MELEE_AGGRO_FACTOR : ThreatView::RANGED_AGGRO_FACTOR;
                float victimGain = 0.0f;
                if (ThreatEntry const* holder = ThreatView::FindEntry(e, e.victim))
                    victimGain = holder->threatPerSec * busySec * AB::TANK_GAIN_SHARE;
                float const room = (e.victimThreat + victimGain) * factor - (mine ? mine->threat : 0.0f);
                if (room < threat)
                    j.unsafe = true;
            }
            return j;
        }
    }

    bool AreaDamageBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        CombatBotRoles const role = bot->GetRole();
        return (role == ROLE_RANGE_DPS || role == ROLE_MELEE_DPS) && !bot->Kit().AreaDamage().empty();
    }

    bool AreaDamageBehavior::Judges(PartyBotAI const* bot, SpellEntry const* spell)
    {
        if (!sCommanderMgr.IsEnabled() || !sCommanderMgr.IsModuleEnabled("aoe"))
            return false;
        uint32 const first = sSpellMgr.GetFirstSpellInChain(spell->Id);
        for (AreaDamageSpell const& a : bot->Kit().AreaDamage())
            if (a.spell && sSpellMgr.GetFirstSpellInChain(a.spell->Id) == first)
                return true;
        return false;
    }

    void AreaDamageBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                     TeamPlan const& /*plan*/, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        Unit* victim = me->GetVictim();
        if (!victim || world.enemies.empty())
            return;

        // The filler on the victim: what the area spell is compared with.
        SpellEntry const* filler = bot->Kit().Filler();
        float fillerPerSec = 0.0f;
        float fillerPerMana = 0.0f;
        if (filler)
        {
            float const damage = DamageSpells::Estimate(me, victim, filler);
            float const mana = DamageSpells::ManaCost(me, filler);
            fillerPerSec = damage / DamageSpells::BusySec(me, filler);
            fillerPerMana = mana > 0.0f ? damage / mana : 0.0f;
        }

        for (AreaDamageSpell const& area : bot->Kit().AreaDamage())
        {
            if (!area.spell)
                continue;
            // Only what the rotation could cast now needs a judgement.
            if (!bot->CanTryToCastSpell(area.onTarget ? victim : me, area.spell))
                continue;

            float const perTarget = DamageSpells::Estimate(me, victim, area.spell);
            float const busySec = DamageSpells::BusySec(me, area.spell);
            float const mana = DamageSpells::ManaCost(me, area.spell);
            Judgement const j = Judge(bot, world, area, victim, perTarget, busySec);
            if (!j.targets)
                continue;

            bool const slower = j.damage / busySec < fillerPerSec;
            bool const costlier = mana > 0.0f && fillerPerMana > 0.0f && j.damage / mana < fillerPerMana;
            if (!j.unsafe && !slower && !costlier)
                continue;

            Proposal veto;
            veto.kind = ProposalKind::Veto;
            veto.tier = ProposalTier::Assigned;
            veto.source = "aoe";
            veto.reason = j.unsafe ? "area spell would pull aggro" :
                          slower ? "area spell slower than the filler" : "area spell costs more mana than the filler";
            veto.veto.type = VetoType::Cast;
            veto.veto.spellId = sSpellMgr.GetFirstSpellInChain(area.spell->Id);
            out.push_back(veto);
        }
    }
}
