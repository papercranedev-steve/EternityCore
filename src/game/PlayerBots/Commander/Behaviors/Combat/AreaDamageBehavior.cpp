#include "Commander/Behaviors/Combat/AreaDamageBehavior.h"
#include "Commander/BotDirectives.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Spatial/PatrolZone.h"
#include "Commander/Perception/Values/AreaValue.h"
#include "Commander/Perception/Spatial/HazardZones.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "Spell.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Commander
{
    namespace
    {
        using AB = AreaDamageBehavior;

        Position3 PositionOf(Unit const* unit)
        {
            Position3 p;
            p.x = unit->GetPositionX();
            p.y = unit->GetPositionY();
            p.z = unit->GetPositionZ();
            return p;
        }

        // The kit's spell the plan names (the facts hold the kit's spell ids).
        SpellEntry const* KitSpell(PartyBotAI const* bot, uint32 spellId)
        {
            for (AreaDamageSpell const& a : bot->Kit().AreaDamage())
                if (a.spell && a.spell->Id == spellId)
                    return a.spell;
            return nullptr;
        }

        bool InPatrolPath(WorldModel const& world, Position3 const& p)
        {
            return PatrolZone::Comes(world, p, Value::FightRemainingSec(world));
        }

        // Map checks of the plan's spot (header: Move). `spot` gets its ground height.
        bool UsableSpot(Player* me, WorldModel const& world, Position3 const& centre, Position3& spot)
        {
            Map* map = me->GetMap();
            float const ground = map->GetHeight(spot.x, spot.y, me->GetPositionZ() + 3.0f);
            if (ground <= INVALID_HEIGHT || std::fabs(ground - me->GetPositionZ()) > AB::MAX_STEP_Z)
                return false;
            spot.z = ground;
            if (HazardZones::IsInside(HazardZones::Get(world), HAZARD_RANGED, spot) || InPatrolPath(world, spot))
                return false;
            SafeRoute::IdleCreatures const idle(me);
            if (idle.Near(spot))
                return false;
            if (!map->isInLineOfSight(spot.x, spot.y, spot.z + AB::LOS_EYE_HEIGHT,
                                      centre.x, centre.y, centre.z + AB::LOS_EYE_HEIGHT))
                return false;
            return SafeRoute::PathIsSafe(me, idle, spot, AB::MAX_PATH_DETOUR);
        }

        // The bot's own area channel from now on: AreaValue on the ticks left (paid).
        bool RemainingChannel(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                              AreaValue::Result& out)
        {
            Player* me = bot->me;
            Spell* channel = me->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
            if (!channel || channel->getState() != SPELL_STATE_CASTING)
                return false;
            SpellEntry const* spell = channel->m_spellInfo;
            if (!AreaDamageBehavior::Judges(bot, spell))
                return false;
            PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
            if (!self)
                return false;
            AreaSpellFacts const* facts = AreaValue::SpellOf(*self, spell->Id);
            if (!facts || !facts->channel)
                return false;

            AreaValue::Request r;
            r.caster = self;
            r.spell = facts;
            r.from = PositionOf(me);
            r.centre = r.from;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            channel->m_targets.getDestination(x, y, z);
            if (x != 0.0f || y != 0.0f)
            {
                r.centre.x = x;
                r.centre.y = y;
                r.centre.z = z;
            }
            uint32 const periodMs = std::max<uint32>(1, uint32(facts->periodSec * 1000.0f));
            r.ticks = (channel->GetCastedTime() + periodMs - 1) / periodMs;
            if (!r.ticks)
                return false;
            r.paid = true;
            if (plan.fightSpot.moving)
                r.turn.busyTanks.push_back(plan.fightSpot.tank);
            r.forbidden = [&plan](EnemyFacts const& e) { return plan.KeepsUnbroken(e); };
            out = AreaValue::Evaluate(world, r);
            return out.valid;
        }

        void ProposeAllyShield(PartyBotAI const* bot, WorldModel const& world, AreaCast const& a,
                               PartyMemberFacts const& self, std::vector<Proposal>& out)
        {
            Player* me = bot->me;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(a.allyShieldSpellId);
            Unit* caster = me->GetMap()->GetUnit(a.caster);
            if (!spell || !caster || !caster->IsAlive() || caster->HasAura(spell->Id) ||
                !me->IsWithinLOSInMap(caster) || !bot->CanTryToCastSpell(caster, spell))
                return;
            Proposal p;
            p.tier = ProposalTier::Assigned;
            p.source = "aoe";
            p.reason = "area phase: shield the caster";
            p.action.type = BotActionType::Cast;
            p.action.spell = spell;
            p.action.target = a.caster;
            float const cost = Value::LostAction(world, self, Value::GCD_SEC);
            p.SetValue(a.allyShieldGain + cost, cost);
            out.push_back(p);
        }

        // Within SPOT_REACHED_YD of one of `spots`.
        bool AtSpot(Position3 const& here, std::vector<Position3> const& spots)
        {
            for (Position3 const& s : spots)
                if (PackResolver::Distance(here, s) <= AB::SPOT_REACHED_YD)
                    return true;
            return false;
        }

        // A walk to the first of `spots` (best first, AreaSpot.h) that passes the live checks.
        // False: none does.
        bool ProposeWalk(Player* me, WorldModel const& world, Position3 const& centre, std::vector<Position3> const& spots,
                         float benefit, float cost, char const* reason, std::vector<Proposal>& out)
        {
            Position3 const here = PositionOf(me);
            for (Position3 const& s : spots)
            {
                Position3 spot = s;
                if (!UsableSpot(me, world, centre, spot))
                    continue;
                float const walkSec = PackResolver::Distance(here, spot) / AB::RUN_YD_PER_SEC;
                Proposal p;
                p.tier = ProposalTier::Assigned;
                p.source = "aoe";
                p.reason = reason;
                p.action.type = BotActionType::MoveTo;
                p.action.x = spot.x;
                p.action.y = spot.y;
                p.action.z = spot.z;
                p.action.holdMs = uint32(walkSec * 1000.0f) + AB::HOLD_EXTRA_MS;
                p.SetValue(benefit, cost);
                out.push_back(p);
                return true;
            }
            return false;
        }

        void ProposePrepShield(PartyBotAI const* bot, AreaPrep const& a, std::vector<Proposal>& out);

        // The brain's prep step (AreaPlan::preps): before the phase, while the party holds or
        // the phase waits, walk to the cast spot, then put the shield up. The shield's gain is
        // net of the mana; nothing else is lost (the caster waits). Out of combat the walk costs
        // nothing either (as the stance modules' places before the fight).
        void ProposePrep(PartyBotAI const* bot, WorldModel const& world, AreaPrep const& a, std::vector<Proposal>& out)
        {
            Player* me = bot->me;
            if (!a.spots.empty() && !AtSpot(PositionOf(me), a.spots) &&
                !me->HasUnitState(UNIT_STATE_ROOT) && !me->IsNonMeleeSpellCasted(false, false, true))
            {
                if (bot->IsMovementHeld() && me->IsMoving())
                {
                    Proposal hold;
                    hold.tier = ProposalTier::Assigned;
                    hold.source = "aoe";
                    hold.reason = "area phase prep: walking to the cast spot";
                    hold.action.type = BotActionType::Hold;
                    hold.SetValue(a.moveGain, 0.0f);
                    out.push_back(hold);
                    return;
                }
                float const cost = me->IsInCombat() ? a.moveCost : 0.0f;
                if (ProposeWalk(me, world, a.centre, a.spots, a.moveGain, cost,
                                "area phase prep: stand at the cast spot first", out))
                    return;
            }
            if (a.shieldId)
                ProposePrepShield(bot, a, out);
        }

        void ProposePrepShield(PartyBotAI const* bot, AreaPrep const& a, std::vector<Proposal>& out)
        {
            Player* me = bot->me;
            SpellEntry const* shield = sSpellMgr.GetSpellEntry(a.shieldId);
            if (!shield || me->HasAura(shield->Id) || !bot->CanTryToCastSpell(me, shield))
                return;
            Proposal p;
            p.tier = ProposalTier::Assigned;
            p.source = "aoe";
            p.reason = "area phase prep: shield before the pull ends";
            p.action.type = BotActionType::Cast;
            p.action.spell = shield;
            p.action.target = me->GetObjectGuid();
            p.SetValue(a.gain, 0.0f);
            out.push_back(p);
        }
    }

    bool AreaDamageBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        CombatBotRoles const role = bot->GetRole();
        if ((role == ROLE_RANGE_DPS || role == ROLE_MELEE_DPS) && !bot->Kit().AreaDamage().empty())
            return true;
        return bot->Kit().AllyShield() != nullptr;      // the area phase's shield on the caster
    }

    bool AreaDamageBehavior::Judges(PartyBotAI const* bot, SpellEntry const* spell)
    {
        if (!sCommanderMgr.IsEnabled() || !sCommanderMgr.IsModuleEnabled("aoe"))
            return false;
        // This module casts area spells in combat only.
        if (!bot->me->IsInCombat())
            return false;
        uint32 const first = sSpellMgr.GetFirstSpellInChain(spell->Id);
        for (AreaDamageSpell const& a : bot->Kit().AreaDamage())
            if (a.spell && sSpellMgr.GetFirstSpellInChain(a.spell->Id) == first)
                return true;
        return false;
    }

    void AreaDamageBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                     TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        AreaPlan const& plans = plan.area;
        ObjectGuid const guid = me->GetObjectGuid();
        AreaCast const* mine = plans.CastOf(guid);
        // 0. Before its spell: to its spot, its shield up (AreaPhase prep; the caster need not
        // be in combat yet).
        if (!mine)
            if (AreaPrep const* prep = plans.PrepOf(guid))
            {
                if (!me->GetTransport())
                    ProposePrep(bot, world, *prep, out);
                return;
            }
        if (!plans.Active() || !me->IsInCombat())
            return;
        PartyMemberFacts const* self = world.FindMember(guid);
        if (!self || !self->CanAct())
            return;

        // A healer's shield on a caster (each cast names its own).
        for (AreaCast const& c : plans.casts)
            if (c.allyShieldBot == guid && c.allyShieldSpellId)
                ProposeAllyShield(bot, world, c, *self, out);
        if (!mine || mine->step == AreaStep::Channel)
            return;
        AreaCast const& a = *mine;
        SpellEntry const* spell = KitSpell(bot, a.spellId);
        if (!spell)
            return;

        // 1. Walk to the spot.
        bool fromHere = true;
        if (a.step == AreaStep::Move && !AtSpot(PositionOf(me), a.spots))
        {
            if (ProposeWalk(me, world, a.centrePos, a.spots, a.moveBenefit, a.moveCost,
                            "area phase: cast from a better place", out))
                return;
            fromHere = a.netHere > 0.0f;
        }
        if (!fromHere)
            return;
        // Another module's walk is under way (escape, position): a cast would stop it.
        if (bot->IsMovementHeld() && me->IsMoving())
            return;

        // 2. Its own shield first.
        if (a.shieldSpellId)
        {
            SpellEntry const* shield = sSpellMgr.GetSpellEntry(a.shieldSpellId);
            if (shield && !me->HasAura(shield->Id))
            {
                // Not castable this moment (the GCD, a cast running): the shield still comes
                // first; the brain plans it again next refresh (before 2026-11-12 the spell
                // went out without it).
                if (!bot->CanTryToCastSpell(me, shield))
                    return;
                Proposal p;
                p.tier = ProposalTier::Assigned;
                p.source = "aoe";
                p.reason = "area phase: shield before the area spell";
                p.action.type = BotActionType::Cast;
                p.action.spell = shield;
                p.action.target = guid;
                // A step of the phase (the shield is up before every area spell, AreaPhase.h):
                // worth the spell that follows it.
                float const cost = Value::LostAction(world, *self, Value::GCD_SEC);
                p.SetValue(std::max(a.net, a.shieldGain) + cost, cost);
                out.push_back(p);
                return;
            }
        }

        // 3. The area spell: a ground spell at the plan's point (AreaCentre.h), else around the caster.
        Proposal p;
        p.tier = ProposalTier::Assigned;
        p.source = "aoe";
        p.reason = "area phase: the whole spell pays";
        p.action.type = BotActionType::Cast;
        p.action.spell = spell;
        if (a.centre.IsEmpty())
        {
            if (!bot->CanTryToCastSpell(me, spell))
                return;
        }
        else
        {
            Position3 at = a.centrePos;
            float const ground = me->GetMap()->GetHeight(at.x, at.y, at.z + 3.0f);
            if (ground > INVALID_HEIGHT && std::fabs(ground - at.z) <= AB::MAX_STEP_Z)
                at.z = ground;
            if (!bot->CanTryToCastSpellAt(at.x, at.y, at.z, spell))
                return;
            p.action.atPoint = true;
            p.action.x = at.x;
            p.action.y = at.y;
            p.action.z = at.z;
        }
        p.SetValue(a.benefit, a.cost);
        out.push_back(p);
    }

    float AreaDamageBehavior::ContinueValue(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                            SpellEntry const* spell, char const*& reason) const
    {
        Spell const* channel = bot->me->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        if (!channel || channel->m_spellInfo != spell)
            return -1.0f;
        AreaValue::Result v;
        if (!RemainingChannel(bot, world, plan, v))
            return -1.0f;
        reason = "the channel's next ticks";
        return std::max(0.0f, v.net);
    }

    bool AreaDamageBehavior::WatchChannel(PartyBotAI* bot, uint32& lastCheckMs)
    {
        Player* me = bot->me;
        Spell* channel = me->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        if (!channel || channel->getState() != SPELL_STATE_CASTING)
            return false;
        uint32 const now = WorldTimer::getMSTime();
        if (WorldTimer::getMSTimeDiff(lastCheckMs, now) < CHANNEL_CHECK_MS)
            return false;
        lastCheckMs = now;
        std::shared_ptr<PartyCommander> commander = sCommanderMgr.FindCommander(me);
        if (!commander)
            return false;
        WorldModel const& world = commander->GetWorld();
        // Stop only when even the best number of further ticks loses points (the enemies it
        // turns, the tank sent away, the caster's own health under them).
        AreaValue::Result v;
        if (!RemainingChannel(bot, world, commander->GetPlan(), v) || v.net >= 0.0f)
            return false;
        uint32 const spellId = channel->m_spellInfo->Id;
        uint32 const leftMs = channel->GetCastedTime();
        me->InterruptSpell(CURRENT_CHANNELED_SPELL, true);

        // Decision log: why it stopped (strat-20261108-161025: two stops nobody could see).
        char what[64];
        snprintf(what, sizeof(what), "stop-channel spell%u", spellId);
        char reason[160];
        snprintf(reason, sizeof(reason), "every further tick loses: %u ticks left (%.1f s), %u turning, turn loss %.1f, own health %.0f%%",
            v.ticks, leftMs / 1000.0f, uint32(v.turning.size()), v.turnLoss, me->GetHealthPercent());
        CandidateRecord r;
        r.source = "aoe";
        r.what = what;
        r.tier = uint8(ProposalTier::Assigned);
        r.valued = true;
        r.benefit = v.benefit;
        r.cost = v.cost;
        r.score = v.net;
        r.reason = reason;
        r.result = CandidateRecord::Result::Executed;
        DecisionRecord record;
        record.timeMs = now;
        record.candidates.push_back(std::move(r));
        BotDirectives& directives = bot->Directives();
        directives.SetLastDecision(now, std::string("aoe ") + what);
        directives.RecordDecision(std::move(record));
        return true;
    }
}
