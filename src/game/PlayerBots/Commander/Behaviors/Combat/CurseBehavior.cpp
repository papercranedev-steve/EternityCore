#include "Commander/Behaviors/Combat/CurseBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Profiles/WarlockCurses.h"
#include "Commander/Perception/Values/CurseValue.h"
#include "Commander/Perception/WorldModel.h"
#include "PartyBotAI.h"
#include "SpellEntry.h"
#include "SpellMgr.h"

namespace Commander
{
    namespace
    {
        // Its own curse with time left, or anyone else's of the same kind.
        bool CurseUp(EnemyFacts const& e, ObjectGuid self, uint8 kind)
        {
            for (CurseOnFacts const& c : e.curses)
                if (c.kind == kind && (c.caster != self || c.remainingMs > CurseBehavior::REFRESH_MS))
                    return true;
            return false;
        }
    }

    bool CurseBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        for (SpellEntry const* spell : bot->Kit().Curses())
            if (spell)
                return true;
        return false;
    }

    void CurseBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        if (!me->IsInCombat())
            return;
        CurseDuty const* duty = plan.curses.For(me->GetObjectGuid());
        if (!duty)
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;
        Unit* victim = me->GetVictim();
        if (!victim || !victim->IsAlive())
            return;
        EnemyFacts const* enemy = world.FindEnemy(victim->GetObjectGuid());
        if (!enemy || CurseUp(*enemy, me->GetObjectGuid(), duty->kind))
            return;
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(duty->spellId);
        if (!spell || !me->IsWithinLOSInMap(victim) || !bot->CanTryToCastSpell(victim, spell))
            return;

        CurseEffect const curse = WarlockCurses::EffectOf(spell);
        Proposal p;
        p.tier = ProposalTier::Routine;
        p.source = "curse";
        p.reason = WarlockCurses::Name(curse.kind);
        p.action.type = BotActionType::Cast;
        p.action.spell = spell;
        p.action.target = victim->GetObjectGuid();
        p.SetValue(CurseValue::Benefit(world, *enemy, curse), CurseValue::Cost(world, *self, *enemy, curse));
        out.push_back(p);
    }
}
