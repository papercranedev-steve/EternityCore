#include "Commander/Behaviors/AreaDamageBehavior.h"
#include "Commander/Behaviors/ThreatBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/AggroTurnCost.h"
#include "Commander/Perception/ManaWorth.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/SpellTags.h"
#include "Commander/Perception/ThreatView.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "PartyBotAI.h"
#include "DamageSpells.h"
#include "DBCStores.h"
#include "Map.h"
#include "Spell.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>

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

        // How the damage comes: a channel (Blizzard) in `count` ticks every `periodSec`, anything
        // else in one go after `periodSec` = the busy time.
        struct Ticks
        {
            uint32 count = 1;
            float periodSec = 0.0f;
        };

        Ticks TicksOf(Unit const* caster, SpellEntry const* spell)
        {
            Ticks t;
            t.periodSec = DamageSpells::BusySec(caster, spell);
            if (!spell->IsChanneledSpell())
                return t;
            int32 const duration = spell->GetDuration();
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                if (spell->EffectAmplitude[i] == 0 || duration <= 0)
                    continue;
                t.count = std::max<uint32>(1, uint32(duration) / spell->EffectAmplitude[i]);
                t.periodSec = spell->EffectAmplitude[i] / 1000.0f;
                break;
            }
            return t;
        }

        // Threat one tick of `damage` gives the caster (spell multiplier, caster modifiers).
        float ThreatOf(Player* me, SpellEntry const* spell, float damage)
        {
            return me->ApplyTotalThreatModifier(damage * sSpellMgr.GetSpellThreatMultiplier(spell),
                                                spell->GetSpellSchoolMask());
        }

        // Threat of the bot's own bolt still in the air towards `enemy` (cast finished, not landed:
        // the generic slot in SPELL_STATE_DELAYED; a channel started after it leaves it there,
        // SpellCaster::SetCurrentCastedSpell). Not yet in the threat list: a Frostbolt landing
        // right after the decision added ~560 and cancelled the Blizzard after its first tick
        // (strat-20261107-194655).
        float InFlightThreat(Player* me, Unit const* enemy)
        {
            Spell const* spell = me->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            if (!spell || spell->getState() != SPELL_STATE_DELAYED ||
                spell->m_targets.getUnitTargetGuid() != enemy->GetObjectGuid())
                return 0.0f;
            return ThreatOf(me, spell->m_spellInfo, DamageSpells::Estimate(me, enemy, spell->m_spellInfo));
        }

        // Speed of `unit` running at the caster once the area spell's slow (`slowPct`) is on it:
        // a unit already slowed keeps its live speed (the chill is not counted twice).
        float SpeedUnder(Unit const* unit, float slowPct)
        {
            float const live = unit->GetSpeed(MOVE_RUN);
            if (slowPct <= 0.0f || unit->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED))
                return live;
            return live * (1.0f - slowPct);
        }

        // One enemy in the area, for the tick by tick plan (Plan below).
        struct AreaTarget
        {
            EnemyFacts const* facts = nullptr;
            float health = 0.0f;
            bool free = false;              // hits the bot or nobody: no turn to fear
            float myThreat = 0.0f;          // with the bot's bolt in the air
            float holderThreat = 0.0f;
            float holderRate = 0.0f;        // threat per second its holder gains on it
            float factor = ThreatView::RANGED_AGGRO_FACTOR;     // 110% in its melee reach, else 130%
            float speed = 0.0f;             // running at the bot under the spell's slow
            PartyMemberFacts const* holder = nullptr;
        };

        // The channel tick by tick: damage over the targets (each at most its health) and the
        // points lost to enemies turning to the bot (AggroTurnCost) after n ticks, n = 1..count.
        // An enemy turns at the first tick k it survives (health > k ticks) with my threat +
        // k x tick threat above (holder threat + rate x k x period) x factor; a tick that kills it
        // turns nothing (strat-20261108-133706: a Berserker at 150 health stopped the Blizzard).
        struct TickPlan
        {
            std::vector<float> damage;      // [n - 1]
            std::vector<float> turnLoss;    // [n - 1]
        };

        TickPlan Plan(WorldModel const& world, PartyMemberFacts const& self, std::vector<AreaTarget> const& targets,
                      float perTick, float tickThreat, Ticks const& ticks, uint32 count)
        {
            TickPlan plan;
            plan.damage.assign(count, 0.0f);
            plan.turnLoss.assign(count, 0.0f);
            // The tick each enemy turns at (0 = never) and the threat its holder then lacks.
            struct Turning
            {
                uint32 tick = 0;
                AggroTurnCost::Turn turn;
                float hitLoss = 0.0f;       // a holder that is not a party member: all its hits on me
            };
            std::vector<Turning> turning;
            for (AreaTarget const& t : targets)
            {
                for (uint32 n = 1; n <= count; ++n)
                    plan.damage[n - 1] += std::min(perTick * float(n), t.health);
                if (t.free)
                    continue;
                for (uint32 k = 1; k <= count; ++k)
                {
                    if (t.health <= perTick * float(k))
                        break;
                    float const mine = t.myThreat + float(k) * tickThreat;
                    float const holder = t.holderThreat + t.holderRate * float(k) * ticks.periodSec;
                    if (mine <= holder * t.factor)
                        continue;
                    Turning x;
                    x.tick = k;
                    if (t.holder)
                    {
                        // The holder (a melee tank) then needs 110% of mine to take it back.
                        x.turn.enemy = t.facts;
                        x.turn.holder = t.holder;
                        x.turn.speed = t.speed;
                        x.turn.gap = std::max(0.0f, mine * ThreatView::MELEE_AGGRO_FACTOR - holder);
                    }
                    else
                        x.hitLoss = Value::HitLoss(world, *t.facts, self,
                                                   std::min(AggroTurnCost::HORIZON_SEC, Value::EnemyTimeToDie(world, *t.facts)));
                    turning.push_back(x);
                    break;
                }
            }
            // After n ticks every enemy turned by then is on me at once: their holders win them
            // back together (AggroTurnCost::OfGroup shares the tank's threat rate and taunts).
            std::vector<AggroTurnCost::Turn> group;
            for (uint32 n = 1; n <= count; ++n)
            {
                group.clear();
                float loss = 0.0f;
                for (Turning const& x : turning)
                {
                    if (x.tick > n)
                        continue;
                    if (x.turn.enemy)
                        group.push_back(x.turn);
                    else
                        loss += x.hitLoss;
                }
                if (!group.empty())
                    loss += AggroTurnCost::OfGroup(world, group, self);
                plan.turnLoss[n - 1] = loss;
            }
            return plan;
        }

        // The enemies in the area of `spell` centred on `centre` (EnemyFacts + live threat of the
        // map units). False: the spell must not touch this area at all (a control the plan keeps,
        // an enemy the tank has not built threat on yet).
        bool Targets(PartyBotAI const* bot, WorldModel const& world, TeamPlan const* plan, SpellEntry const* spell,
                     Position3 const& centre, float slowPct, bool live, std::vector<AreaTarget>& out)
        {
            Player* me = bot->me;
            Map* map = me->GetMap();
            ObjectGuid const self = me->GetObjectGuid();
            Position3 const here = PositionOf(me);
            float const radius = SpellTags::AreaRadius(spell);
            uint32 const settleMs = uint32(ThreatBehavior::NEW_ENEMY_SETTLE_SEC * 1000.0f);
            out.clear();
            for (EnemyFacts const& e : world.enemies)
            {
                Unit* unit = map->GetUnit(e.guid);
                if (!unit || !unit->IsAlive())
                    continue;
                Position3 const at = live ? PositionOf(unit) : e.pos;
                if (!e.acting || PackResolver::Distance(centre, at) - e.boundingRadius > radius)
                    continue;
                // Polymorph, sap, a stun that breaks on damage, or one the brain is about to put
                // on it (TeamPlan::KeepsUnbroken): the area spell would wake it up.
                if (plan && plan->KeepsUnbroken(e))
                    return false;
                // Feared, stunned for good: not woken, but not standing still either; not counted.
                if (e.crowdControlled)
                    continue;
                AreaTarget t;
                t.facts = &e;
                t.health = float(live ? unit->GetHealth() : e.health);
                Unit* holderUnit = live ? unit->GetVictim() : map->GetUnit(e.victim);
                t.free = !holderUnit || holderUnit == me;
                if (!t.free)
                {
                    // The tank has not built threat on a new enemy yet.
                    if (!live && e.firstSeenMs && WorldTimer::getMSTimeDiff(e.firstSeenMs, world.refreshedAtMs) < settleMs)
                        return false;
                    if (live && unit->CanHaveThreatList())
                    {
                        ThreatManager& threat = unit->GetThreatManager();
                        t.myThreat = threat.getThreat(me);
                        t.holderThreat = threat.getThreat(holderUnit);
                    }
                    else
                    {
                        ThreatEntry const* mine = ThreatView::FindEntry(e, self);
                        t.myThreat = mine ? mine->threat : 0.0f;
                        t.holderThreat = e.victimThreat;
                    }
                    t.myThreat += InFlightThreat(me, unit);
                    bool const melee = live ? unit->CanReachWithMeleeAutoAttack(me)
                                            : PackResolver::Distance(here, e.pos) - e.boundingRadius <= AB::MELEE_REACH;
                    t.factor = melee ? ThreatView::MELEE_AGGRO_FACTOR : ThreatView::RANGED_AGGRO_FACTOR;
                    t.holder = world.FindMember(holderUnit->GetObjectGuid());
                    // A party holder hitting another enemy gains little here: its measured rate is a
                    // jump of the last seconds (+200 at once, without hitting it), not what comes next
                    // (strat-20261107-194655: tank on another Cadaver, Blizzard judged safe for 8 ticks).
                    if (t.holder && t.holder->victim == e.guid)
                        if (ThreatEntry const* h = ThreatView::FindEntry(e, t.holder->guid))
                            t.holderRate = h->threatPerSec * AB::TANK_GAIN_SHARE;
                    t.speed = SpeedUnder(unit, slowPct);
                }
                out.push_back(t);
            }
            return true;
        }

        struct Choice
        {
            uint32 ticks = 0;
            float damage = 0.0f;
            float net = 0.0f;
            float benefit = 0.0f;
            float cost = 0.0f;
        };

        // The best number of ticks (1..count): damage as seconds of the filler, against the time
        // spent and the enemies turning; `alternativeMana` = what the filler would spend in the
        // same time, in points (a channel already paid for: continuing costs no mana).
        Choice Best(WorldModel const& world, PartyMemberFacts const& self, TickPlan const& plan, Ticks const& ticks,
                    float fillerPerSec, float fillerManaPerSec)
        {
            Choice best;
            for (uint32 n = 1; n <= plan.damage.size(); ++n)
            {
                float const sec = float(n) * ticks.periodSec;
                float const benefit = Value::LostAction(world, self, plan.damage[n - 1] / fillerPerSec) +
                                      (fillerManaPerSec > 0.0f ? ManaWorth::Points(world, self, fillerManaPerSec * sec) : 0.0f);
                float const cost = Value::LostAction(world, self, sec) + plan.turnLoss[n - 1];
                if (!best.ticks || benefit - cost > best.net)
                {
                    best.ticks = n;
                    best.damage = plan.damage[n - 1];
                    best.benefit = benefit;
                    best.cost = cost;
                    best.net = benefit - cost;
                }
            }
            return best;
        }

        // ClassKit::AreaDamage entry of `spell` (any rank). False: not one of the bot's.
        bool KitEntry(PartyBotAI const* bot, SpellEntry const* spell, AreaDamageSpell& entry)
        {
            uint32 const first = sSpellMgr.GetFirstSpellInChain(spell->Id);
            for (AreaDamageSpell const& a : bot->Kit().AreaDamage())
                if (a.spell && sSpellMgr.GetFirstSpellInChain(a.spell->Id) == first)
                {
                    entry = a;
                    return true;
                }
            return false;
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
        // This module proposes area spells in combat only.
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
        Unit* victim = me->GetVictim();
        if (!victim || world.enemies.empty() || !me->IsInCombat())
            return;
        // A walk of another module is under way (escape, position): a cast would stop it.
        if (bot->IsMovementHeld() && me->IsMoving())
            return;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->CanAct())
            return;

        // The filler on the victim: what the area spell is compared with.
        SpellEntry const* filler = bot->Kit().Filler();
        if (!filler)
            return;
        float const fillerDamage = DamageSpells::Estimate(me, victim, filler);
        float const fillerMana = DamageSpells::ManaCost(me, filler);
        float const fillerPerSec = fillerDamage / DamageSpells::BusySec(me, filler);
        float const fillerPerMana = fillerMana > 0.0f ? fillerDamage / fillerMana : 0.0f;
        if (fillerPerSec <= 0.0f)
            return;

        Map* map = me->GetMap();
        std::vector<AreaTarget> targets;
        for (AreaDamageSpell const& area : bot->Kit().AreaDamage())
        {
            SpellEntry const* spell = area.spell;
            if (!spell)
                continue;
            Ticks const ticks = TicksOf(me, spell);
            if (ticks.count > 1 && me->IsMoving())
                continue;
            float const perTarget = DamageSpells::Estimate(me, victim, spell);
            float const perTick = perTarget / float(ticks.count);
            float const tickThreat = ThreatOf(me, spell, perTick);
            float const mana = DamageSpells::ManaCost(me, spell);

            // Where: every enemy as the centre of a ground spell (Blizzard), else the bot.
            std::vector<Unit*> centres;
            if (area.onTarget)
            {
                for (EnemyFacts const& e : world.enemies)
                    if (e.acting && !e.crowdControlled)
                        if (Unit* unit = map->GetUnit(e.guid))
                            if (unit->IsAlive() && bot->IsValidHostileTarget(unit) && me->IsWithinLOSInMap(unit))
                                centres.push_back(unit);
            }
            else
                centres.push_back(nullptr);

            Choice best;
            Unit* bestCentre = nullptr;
            for (Unit* centre : centres)
            {
                // Range, mana, cooldown, idle creatures in the area (PartyBotAI::PassesPartyCastChecks).
                if (!bot->CanTryToCastSpell(centre ? centre : me, spell))
                    continue;
                if (!Targets(bot, world, &plan, spell, PositionOf(centre ? centre : me), area.slowPct, false, targets) ||
                    targets.empty())
                    continue;
                TickPlan const tp = Plan(world, *self, targets, perTick, tickThreat, ticks, ticks.count);
                Choice const c = Best(world, *self, tp, ticks, fillerPerSec, 0.0f);
                if (!best.ticks || c.net > best.net)
                {
                    best = c;
                    bestCentre = centre;
                }
            }
            if (!best.ticks)
                continue;

            // Not more damage per mana than the filler, over the ticks it is expected to run:
            // no fight length guess needed (adds).
            if (mana > 0.0f && fillerPerMana > 0.0f && best.damage / mana < fillerPerMana)
                continue;
            // The mana it costs above the filler's over the same time (header: value).
            float const busySec = float(best.ticks) * ticks.periodSec;
            float const extraMana = std::max(0.0f, mana - fillerMana / DamageSpells::BusySec(me, filler) * busySec);
            float const manaCost = extraMana > 0.0f ? ManaWorth::Points(world, *self, extraMana) : 0.0f;
            if (best.net - manaCost <= 0.0f)
                continue;
            best.cost += manaCost;
            best.net -= manaCost;

            Proposal p;
            p.tier = ProposalTier::Assigned;
            p.source = "aoe";
            p.reason = best.ticks < ticks.count ? "area damage, channel until an enemy turning costs more"
                                                : "area damage beats the filler";
            p.action.type = BotActionType::Cast;
            p.action.spell = spell;
            // Ground spells on the chosen enemy; the others on the bot itself (a cone goes the
            // way the bot faces: its victim, the filler turned it there).
            if (bestCentre)
                p.action.target = bestCentre->GetObjectGuid();
            p.SetValue(best.benefit, best.cost);
            out.push_back(p);
        }
    }

    namespace
    {
        // The bot's own area channel from now on (live health, threat, positions): the best
        // number of further ticks and its net. False: not one of its area channels, or nothing
        // to judge (no enemy in the area).
        bool RemainingChannel(PartyBotAI const* bot, WorldModel const& world, Choice& out)
        {
            Player* me = bot->me;
            Spell* channel = me->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
            if (!channel || channel->getState() != SPELL_STATE_CASTING)
                return false;
            SpellEntry const* spell = channel->m_spellInfo;
            AreaDamageSpell area;
            if (!AreaDamageBehavior::Judges(bot, spell) || bot->IsInDuel() || !KitEntry(bot, spell, area))
                return false;
            PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
            Unit* victim = me->GetVictim();
            SpellEntry const* filler = bot->Kit().Filler();
            if (!self || !filler)
                return false;

            Position3 centre = PositionOf(me);
            float x = 0.0f, y = 0.0f, z = 0.0f;
            channel->m_targets.getDestination(x, y, z);
            if (x != 0.0f || y != 0.0f)
            {
                centre.x = x;
                centre.y = y;
                centre.z = z;
            }
            Ticks const ticks = TicksOf(me, spell);
            uint32 const periodMs = std::max<uint32>(1, uint32(ticks.periodSec * 1000.0f));
            uint32 const left = (channel->GetCastedTime() + periodMs - 1) / periodMs;
            if (!left)
                return false;

            std::vector<AreaTarget> targets;
            if (!Targets(bot, world, nullptr, spell, centre, area.slowPct, true, targets) || targets.empty())
                return false;
            Unit const* probe = victim ? victim : me->GetMap()->GetUnit(targets.front().facts->guid);
            if (!probe)
                return false;
            float const perTick = DamageSpells::Estimate(me, probe, spell) / float(ticks.count);
            float const fillerDamage = DamageSpells::Estimate(me, probe, filler);
            float const fillerBusy = DamageSpells::BusySec(me, filler);
            float const fillerPerSec = fillerDamage / fillerBusy;
            if (fillerPerSec <= 0.0f)
                return false;

            // Going on: the next ticks are paid for, the filler instead would cost its mana.
            TickPlan const tp = Plan(world, *self, targets, perTick, ThreatOf(me, spell, perTick), ticks, left);
            out = Best(world, *self, tp, ticks, fillerPerSec, DamageSpells::ManaCost(me, filler) / fillerBusy);
            return true;
        }
    }

    float AreaDamageBehavior::ContinueValue(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& /*plan*/,
                                            SpellEntry const* spell, char const*& reason) const
    {
        Spell const* channel = bot->me->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        if (!channel || channel->m_spellInfo != spell)
            return -1.0f;
        Choice best;
        if (!RemainingChannel(bot, world, best))
            return -1.0f;
        reason = "the channel's next ticks";
        return std::max(0.0f, best.net);
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
        // Stop only when even the best number of further ticks loses points (an enemy turning
        // costs more than the damage, user 2026-11-08: a slowed melee enemy is cheap, the tank
        // takes it back before it gets there). The same value is what ending it costs anyone
        // else (ContinueValue).
        Choice best;
        if (!RemainingChannel(bot, commander->GetWorld(), best) || best.net >= 0.0f)
            return false;
        me->InterruptSpell(CURRENT_CHANNELED_SPELL, true);
        return true;
    }
}
