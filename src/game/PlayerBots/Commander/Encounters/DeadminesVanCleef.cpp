/*
 * VanCleef boss module (header comment: match, phases, proposals).
 *
 * Value estimates (points, ValueModel.h: 1 point = 1% of party max health):
 *   SummonLoss(g)  = Value::HitLoss of a synthetic enemy with g Blackguards' melee on the
 *                    healer for LOOSE_SEC (death penalty included if the healer would not last).
 *                    Blackguard melee comes from the last Blackguard seen this fight (the
 *                    originals fight first), else GUARD_DPS_SHARE x VanCleef's melee.
 *   Hold benefit   = risk x SummonLoss(2 + original guards alive). risk is in 0..1:
 *                    1 while an original guard is alive (four elites at once), else the larger of
 *                    the tank-health and healer-mana shortfalls, each ramping from 0 at the
 *                    ready threshold to 1 at RISK_RAMP_PCT below it.
 *   Hold cost      = Value::LostAction for one bot tick (same as ThreatBehavior's "near aggro, stop").
 *   Area threat    = GROUP_THREAT_SHARE x SummonLoss(2) (Demoralizing: FALLBACK_THREAT_SHARE),
 *                    cost one global cooldown of the tank.
 *   Heal veto      = SummonLoss(2), cost EncounterHelpers::HealDelayCost (two bot ticks).
 *
 * Known limits:
 *   - "Tank area threat on cooldown" (scenario 6, 3.3) is not a hold reason: the module reads
 *     facts only, cooldowns are live state of the tank bot.
 *   - The heal veto lasts Arbiter::VETO_MS (3 s), so it can
 *     outlive HEAL_VETO_WINDOW_MS by up to that much. It is only set while the tank is above
 *     HEAL_VETO_MIN_TANK_PCT, and ThreatBehavior's own heal veto takes over afterwards.
 *   - Summons are seen on the first refresh after they put the tank on their threat list
 *     (0.2 - 1.2 s); the windows start at EnemyFacts::firstSeenMs, not at the cast.
 *   - The player is not held: if the player keeps hitting, the boss crosses 50% anyway.
 *   - The stun trick (stun at 51-52% so the summon cast fails) is not done; unverified in game.
 *   - Pull ready thresholds are not raised for this fight (needs a PullPlanner parameter).
 */

#include "Commander/Encounters/DeadminesVanCleef.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Behaviors/Combat/ThreatBehavior.h"
#include "Commander/Behaviors/Combat/TargetBehavior.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Map.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/CommanderMgr.h"
#include "PartyBotAI.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>
#include <cstdio>

namespace Commander
{
    namespace
    {
        char const* const SOURCE = "vancleef";

        bool IsSummonedGuard(EnemyFacts const& e)
        {
            return e.entry == DeadminesVanCleef::NPC_BLACKGUARD && e.summoned;
        }

        bool IsOriginalGuard(EnemyFacts const& e)
        {
            return e.entry == DeadminesVanCleef::NPC_BLACKGUARD && !e.summoned;
        }

        float Ramp(float ready, float value, float span)
        {
            return std::min(1.0f, std::max(0.0f, (ready - value) / span));
        }
    }

    bool DeadminesVanCleef::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        for (EnemyFacts const& e : world.enemies)
            if (e.entry == NPC_VANCLEEF || IsSummonedGuard(e))
                return true;
        return false;
    }

    void DeadminesVanCleef::OnReset()
    {
        *this = DeadminesVanCleef();
    }

    uint32 DeadminesVanCleef::MsSinceSummon(uint32 nowMs) const
    {
        return m_summonAtMs ? WorldTimer::getMSTimeDiff(m_summonAtMs, nowMs) : 0xFFFFFFFFu;
    }

    float DeadminesVanCleef::SummonLoss(WorldModel const& world, EnemyFacts const* boss, uint32 guards) const
    {
        PartyMemberFacts const* victim = world.FindMember(m_healer);
        if (!victim)
            victim = world.FindMember(world.leader);
        if (!victim || guards == 0)
            return 0.0f;

        EnemyFacts g;
        if (m_haveGuardModel)
            g = m_guardModel;
        else if (boss)
        {
            g.level = boss->level;
            g.meleeDps = boss->meleeDps * GUARD_DPS_SHARE;
            g.meleePhysical = true;
        }
        else
            return 0.0f;
        g.acting = true;
        g.rangedDps = 0.0f;
        g.meleeDps *= float(guards);
        return Value::HitLoss(world, g, *victim, LOOSE_SEC);
    }

    void DeadminesVanCleef::Refresh(PartyBotAI* /*bot*/, WorldModel const& world)
    {
        uint32 const now = world.refreshedAtMs;

        EnemyFacts const* boss = EncounterHelpers::FindEnemyByEntry(world, NPC_VANCLEEF);
        m_bossAlive = boss != nullptr;
        if (boss)
        {
            m_boss = boss->guid;
            m_bossPct = boss->healthPct;
        }

        m_originals.clear();
        m_summons.clear();
        uint32 firstSummonSeen = 0;
        for (EnemyFacts const& e : world.enemies)
        {
            if (e.entry != NPC_BLACKGUARD)
                continue;
            if (IsOriginalGuard(e))
                m_originals.push_back(e.guid);
            else
            {
                m_summons.push_back(e.guid);
                uint32 const seen = e.firstSeenMs ? e.firstSeenMs : now;
                if (!firstSummonSeen || seen < firstSummonSeen)
                    firstSummonSeen = seen;     // the earlier of the two
            }
            m_guardModel = e;
            m_guardModel.threat.clear();
            m_haveGuardModel = true;
        }
        m_originalsAlive = uint32(m_originals.size());

        if (!m_summons.empty() && !m_summonAtMs)
        {
            m_summonAtMs = firstSummonSeen;
            m_holdDamage = false;
            m_pendingNotes.push_back("VanCleef summoned his guards: tank area threat, healer waits a moment.");
        }

        if (!m_bossAlive)
            m_phase = Phase::BossDead;
        else if (!m_summons.empty())
            m_phase = Phase::Reinforcements;
        else if (m_originalsAlive)
            m_phase = Phase::Opening;
        else
            m_phase = Phase::PushLine;

        PartyMemberFacts const* tank = EncounterHelpers::FindHolder(world, boss);
        m_tank = tank ? tank->guid : ObjectGuid();
        m_tankPct = tank ? tank->healthPct : 100.0f;
        PartyMemberFacts const* healer = EncounterHelpers::FindLowestManaHealer(world);
        m_healer = healer ? healer->guid : ObjectGuid();
        m_healerManaPct = healer ? healer->manaPct : 100.0f;

        // Hold decision.
        bool const inBand = m_bossAlive && !m_summonAtMs && m_bossPct > SUMMON_PCT && m_bossPct <= HOLD_BAND_PCT;
        bool const guardsUp = m_originalsAlive > 0;
        bool const tankLow = m_tankPct < TANK_READY_PCT;
        bool const manaLow = m_healerManaPct < HEALER_READY_MANA_PCT;
        bool const wantHold = inBand && !m_holdGivenUp && (guardsUp || tankLow || manaLow);

        if (wantHold && !m_holdDamage)
        {
            m_holdDamage = true;
            m_holdSinceMs = now;
            char buf[200];
            snprintf(buf, sizeof(buf), "VanCleef at %.0f%%: damage held before the 50%% summon (%s%s%s).",
                m_bossPct, guardsUp ? "guards alive " : "", tankLow ? "tank low " : "", manaLow ? "healer mana low" : "");
            m_pendingNotes.push_back(buf);
        }
        else if (!wantHold && m_holdDamage)
        {
            m_holdDamage = false;
            if (inBand)
                m_pendingNotes.push_back("VanCleef: ready, pushing past 50%.");
        }
        if (m_holdDamage && WorldTimer::getMSTimeDiff(m_holdSinceMs, now) >= MAX_HOLD_MS)
        {
            m_holdDamage = false;
            m_holdGivenUp = true;
            m_pendingNotes.push_back("VanCleef: waited too long, pushing past 50% anyway.");
        }

        m_holdRisk = guardsUp ? 1.0f : std::max(Ramp(TANK_READY_PCT, m_tankPct, RISK_RAMP_PCT),
                                                Ramp(HEALER_READY_MANA_PCT, m_healerManaPct, RISK_RAMP_PCT));
        m_pushLoss = SummonLoss(world, boss, SUMMON_COUNT + m_originalsAlive);
        m_summonLoss = SummonLoss(world, boss, SUMMON_COUNT);
    }

    void DeadminesVanCleef::AdjustPlan(WorldModel const& world, TeamPlan& plan)
    {
        for (std::string& note : m_pendingNotes)
            plan.notes.push_back(std::move(note));
        m_pendingNotes.clear();

        // While damage on the boss is held, the original guards are what the damage dealers
        // should be doing anyway: point the focus at the weakest one.
        if (m_holdDamage && !m_originals.empty() && (plan.focus.enemy.IsEmpty() || plan.focus.enemy == m_boss))
        {
            EnemyFacts const* weakest = nullptr;
            for (ObjectGuid const& guid : m_originals)
                if (EnemyFacts const* e = world.FindEnemy(guid))
                    if (e->acting && (!weakest || e->health < weakest->health))
                        weakest = e;
            if (weakest)
                EncounterHelpers::SetFocus(world, plan, *weakest, "vancleef: guards first while holding the boss");
        }
    }

    void DeadminesVanCleef::Propose(PartyBotAI const* bot, WorldModel const& world,
                                    TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        switch (bot->GetRole())
        {
            case ROLE_MELEE_DPS:
            case ROLE_RANGE_DPS:
                ProposeDamage(bot, world, plan, out);
                break;
            case ROLE_TANK:
                ProposeTank(bot, world, out);
                break;
            case ROLE_HEALER:
                ProposeHealer(bot, world, out);
                break;
            default:
                break;
        }
    }

    void DeadminesVanCleef::ProposeDamage(PartyBotAI const* bot, WorldModel const& world, TeamPlan const& plan,
                                          std::vector<Proposal>& out) const
    {
        if (!m_holdDamage)
            return;
        Unit const* victim = bot->me->GetVictim();
        if (!victim || victim->GetObjectGuid() != m_boss)
            return;
        PartyMemberFacts const* myFacts = world.FindMember(bot->me->GetObjectGuid());
        if (!myFacts)
            return;

        float const tickSec = sCommanderMgr.GetBotUpdateInterval() / 1000.0f;
        float const holdBenefit = m_holdRisk * m_pushLoss;

        // Fix: AdjustPlan points the focus at an original guard while the boss is held, but this
        // HoldFire (same tier, benefit = the whole push loss) outscored the focus switch,
        // so damage dealers stood idle on the boss for up to MAX_HOLD_MS instead of killing the
        // guards. Switching avoids the push as well and keeps the damage HoldFire throws away;
        // if the switch cannot execute (target invalid), the arbiter falls back to HoldFire.
        if (!plan.focus.enemy.IsEmpty() && plan.focus.enemy != m_boss &&
            std::find(m_originals.begin(), m_originals.end(), plan.focus.enemy) != m_originals.end())
        {
            Unit* guard = bot->me->GetMap()->GetUnit(plan.focus.enemy);
            if (guard && guard->IsAlive() && bot->me->IsWithinDistInMap(guard, TargetBehavior::FOCUS_RANGE))
            {
                float walkSec = 0.0f;
                if (bot->GetRole() == ROLE_MELEE_DPS && !bot->me->CanReachWithMeleeAutoAttack(guard))
                    walkSec = std::max(0.0f, bot->me->GetDistance(guard) - TargetBehavior::MELEE_REACH) /
                              TargetBehavior::RUN_YARDS_PER_SEC;
                Proposal attack = EncounterHelpers::MakeReplace(SOURCE, ProposalTier::Assigned, BotActionType::Attack,
                                                                plan.focus.enemy);
                attack.reason = "boss near 50% summon, kill the guards first";
                attack.SetValue(holdBenefit + Value::LostAction(world, *myFacts, tickSec),
                                Value::LostAction(world, *myFacts, ThreatBehavior::SWITCH_TARGET_SEC + walkSec));
                out.push_back(attack);
            }
        }

        Proposal p = EncounterHelpers::MakeReplace(SOURCE, ProposalTier::Assigned, BotActionType::HoldFire);
        p.reason = "boss near 50% summon, not ready";
        p.SetValue(holdBenefit, Value::LostAction(world, *myFacts, tickSec));
        out.push_back(p);
    }

    void DeadminesVanCleef::ProposeTank(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const
    {
        if (m_summons.empty() || MsSinceSummon(WorldTimer::getMSTime()) >= GROUP_THREAT_WINDOW_MS)
            return;
        Player* me = bot->me;
        PartyMemberFacts const* myFacts = world.FindMember(me->GetObjectGuid());
        if (!myFacts || !myFacts->CanAct())
            return;

        bool summonClose = false;   // (`near` is a Windows macro)
        for (ObjectGuid const& guid : m_summons)
            if (EnemyFacts const* e = world.FindEnemy(guid))
                if (PackResolver::Distance(myFacts->pos, e->pos) <= ThreatBehavior::GROUP_THREAT_RADIUS)
                    summonClose = true;
        if (!summonClose)
            return;

        // The class's area threat abilities (ClassKit::AreaThreat), area taunts left out: the
        // main one prevents GROUP_THREAT_SHARE of the summon loss, a shout FALLBACK_THREAT_SHARE.
        Unit* victim = me->GetVictim();
        float const cost = Value::LostAction(world, *myFacts, Value::GCD_SEC);
        for (AreaThreatSpell const& o : bot->Kit().AreaThreat())
        {
            if (!o.spell || o.taunt)
                continue;
            float const share = o.primary ? GROUP_THREAT_SHARE : FALLBACK_THREAT_SHARE;
            Unit* castTarget = o.onTarget ? victim : me;
            // Thunder Clap from Defensive Stance: after a stance swap (PartyBotAI::DoCastWithStance).
            if (!castTarget || !bot->CanCastWithStance(castTarget, o.spell))
                continue;
            float const swapCost = bot->CanTryToCastSpell(castTarget, o.spell) ? 0.0f :
                Value::LostAction(world, *myFacts, ThreatBehavior::STANCE_SWAP_COST_SEC);
            Proposal p = EncounterHelpers::MakeReplace(SOURCE, ProposalTier::Assigned, BotActionType::Cast,
                o.onTarget ? victim->GetObjectGuid() : ObjectGuid(), o.spell);
            p.reason = "area threat on fresh summons";
            p.SetValue(share * m_summonLoss, cost + swapCost);
            out.push_back(p);
        }
    }

    void DeadminesVanCleef::ProposeHealer(PartyBotAI const* /*bot*/, WorldModel const& world, std::vector<Proposal>& out) const
    {
        if (m_summons.empty() || MsSinceSummon(WorldTimer::getMSTime()) >= HEAL_VETO_WINDOW_MS)
            return;
        PartyMemberFacts const* tank = world.FindMember(m_tank);
        if (!tank || !tank->alive || tank->healthPct <= HEAL_VETO_MIN_TANK_PCT)
            return;

        Proposal veto = EncounterHelpers::MakeHealVeto(SOURCE, tank->guid);
        veto.reason = "fresh 0-threat summons, let the tank grab them";
        veto.SetValue(m_summonLoss, EncounterHelpers::HealDelayCost(world, *tank));
        out.push_back(veto);
    }
}
