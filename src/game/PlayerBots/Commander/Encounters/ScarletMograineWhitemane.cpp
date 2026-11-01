/*
 * Mograine / Whitemane boss module (header comment: match, stages, proposals).
 *
 * Known limits:
 *   - The tank is not moved to Mograine's body / Whitemane's path in DIED_ONCE; it only
 *     switches to Whitemane as soon as she is on the threat list (no MoveTo proposal).
 *   - The REVIVED taunt is one attempt per tick inside the window; if every taunt is on
 *     cooldown, ThreatBehavior's "win back with threat" is what is left.
 *   - The pre-sleep top-up only picks the heal; it does not check whether the heal pulls
 *     Whitemane (it skips when ThreatBehavior already vetoed healing the tank).
 *   - Ranged / healer positioning (20-30 yd from Whitemane against Mind Control) is not done.
 *   - Stage inference without instance data assumes the feign death happens once (true for
 *     this script); Divine Shield in REVIVED does not move the stage back.
 *   - Script assist at pull (82 yd) and the "skip event bosses in clear" rule are elsewhere
 *     (AssistPredictor / ClearPlanner), not in this module.
 */

#include "Commander/Encounters/ScarletMograineWhitemane.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include "Commander/Behaviors/ThreatBehavior.h"
#include "Commander/BotDirectives.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/ValueModel.h"
#include "PartyBotAI.h"
#include "InstanceData.h"
#include "Map.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        char const* const SOURCE = "mograine-whitemane";

        // Same base-point measure CombatBotBaseAI::SelectMostEfficientHealingSpell uses.
        int32 BasePoints(SpellEntry const* spell)
        {
            int32 points = 0;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (spell->Effect[i] == SPELL_EFFECT_HEAL)
                    points += spell->EffectBasePoints[i];
            return points;
        }

        // Who `enemy` hits next: its victim, else the top of its threat list.
        ObjectGuid NextVictim(EnemyFacts const& enemy)
        {
            if (!enemy.victim.IsEmpty())
                return enemy.victim;
            for (ThreatEntry const& t : enemy.threat)
                if (t.onList)
                    return t.guid;
            return ObjectGuid();
        }
    }

    bool ScarletMograineWhitemane::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        for (EnemyFacts const& e : world.enemies)
            if (e.entry == NPC_MOGRAINE || e.entry == NPC_WHITEMANE)
                return true;

        // Neither boss on a threat list: Mograine feigning death before Whitemane is on one.
        // Keep the stage (no reset) within the grace window while the party still fights.
        if (!m_bossEngagedMs || m_stage == STAGE_DONE)
            return false;
        uint32 const now = world.refreshedAtMs;
        uint32 const grace = BOSS_GONE_GRACE_MS;
        if (WorldTimer::getMSTimeDiff(m_bossEngagedMs, now) >= grace)
            return false;
        // Mograine down: the party usually drops combat while Whitemane walks over to him out
        // of combat (the script only pulls everyone in again at REVIVED), longer than
        // PARTY_OUT_OF_COMBAT_MS. The grace window alone decides; it ends once the live feign
        // evidence is gone (Refresh stops renewing m_bossEngagedMs).
        if (m_stage == STAGE_DIED_ONCE)
            return true;
        for (PartyMemberFacts const& m : world.party)
            if (m.alive && m.inCombat)
                return true;
        uint32 const outOfCombat = PARTY_OUT_OF_COMBAT_MS;
        return m_partyCombatMs && WorldTimer::getMSTimeDiff(m_partyCombatMs, now) < outOfCombat;
    }

    void ScarletMograineWhitemane::OnReset()
    {
        *this = ScarletMograineWhitemane();
    }

    void ScarletMograineWhitemane::SetStage(uint32 stage, uint32 nowMs)
    {
        if (stage == m_stage)
            return;
        m_stage = stage;
        m_stageSinceMs = nowMs;
        if (stage == STAGE_DIED_ONCE)
            m_pendingNotes.push_back("Mograine is down (not dead): focus Whitemane when she comes in.");
        else if (stage == STAGE_REVIVED)
        {
            m_revivedAtMs = nowMs;
            m_pendingNotes.push_back("Mograine revived: tank taunts him, focus stays on Whitemane.");
        }
    }

    void ScarletMograineWhitemane::Refresh(PartyBotAI* bot, WorldModel const& world)
    {
        uint32 const now = world.refreshedAtMs;
        EnemyFacts const* mograine = EncounterHelpers::FindEnemyByEntry(world, NPC_MOGRAINE);
        EnemyFacts const* whitemane = EncounterHelpers::FindEnemyByEntry(world, NPC_WHITEMANE);
        if (mograine)
            m_mograine = mograine->guid;
        if (whitemane)
            m_whitemane = whitemane->guid;
        PartyMemberFacts const* tank = EncounterHelpers::FindTankBot(world);
        m_tank = tank ? tank->guid : ObjectGuid();
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.alive && m.inCombat)
            {
                m_partyCombatMs = now;
                break;
            }
        }

        // Live units (map thread): Mograine's feign death, Whitemane walking in.
        bool const live = bot && bot->me->IsInWorld();
        Unit* mograineUnit = (live && !m_mograine.IsEmpty()) ? bot->me->GetMap()->GetUnit(m_mograine) : nullptr;
        Unit* whitemaneUnit = (live && !m_whitemane.IsEmpty()) ? bot->me->GetMap()->GetUnit(m_whitemane) : nullptr;
        bool const mograineFeigning = mograineUnit && mograineUnit->IsAlive() &&
            (mograineUnit->IsFeigningDeath() || mograineUnit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE) ||
             !mograineUnit->IsTargetableBy(bot->me));
        bool const whitemaneEngaged = (whitemane && whitemane->acting) ||
            (!whitemane && whitemaneUnit && whitemaneUnit->IsAlive() && whitemaneUnit->IsInCombat());

        // Grace window for Matches: a boss on a threat list, or still in the fight off them.
        if (mograine || whitemane || mograineFeigning || whitemaneEngaged)
            m_bossEngagedMs = now;
        if (mograineUnit && whitemaneUnit && !mograineUnit->IsAlive() && !whitemaneUnit->IsAlive())
            m_bossEngagedMs = 0;        // both dead: the fight is over
        else if (!mograine && !whitemane && mograineUnit && whitemaneUnit && !mograineFeigning &&
                 !mograineUnit->IsInCombat() && !whitemaneUnit->IsInCombat())
            m_bossEngagedMs = 0;        // both out of combat (evaded): the fight is over

        // Instance stage: live object, map thread (we are called from PartyCommander::Update).
        m_stageFromInstance = false;
        uint32 stage = m_stage;
        if (live)
        {
            if (InstanceData* data = bot->me->GetMap()->GetInstanceData())
            {
                stage = data->GetData(TYPE_MOGRAINE_AND_WHITE_EVENT);
                m_stageFromInstance = true;
            }
        }
        if (!m_stageFromInstance)
        {
            // Feigned: not acting, alive but not attackable, or off the threat lists (seen
            // earlier, not dead) while Whitemane is engaged.
            bool const feigned = (mograine && !mograine->acting) || mograineFeigning ||
                (!mograine && !m_mograine.IsEmpty() && whitemaneEngaged && (!mograineUnit || mograineUnit->IsAlive()));
            if (m_stage == STAGE_REVIVED)
                stage = STAGE_REVIVED;          // feign death happens once; Divine Shield is not it
            else if (feigned)
            {
                m_sawFeignDeath = true;
                stage = STAGE_DIED_ONCE;
            }
            else if (m_sawFeignDeath)
                // Back only when he is on a threat list and acting again; absent = still down.
                stage = (mograine && mograine->acting) ? STAGE_REVIVED : STAGE_DIED_ONCE;
            else
                stage = STAGE_IN_PROGRESS;
        }
        SetStage(stage, now);
    }

    void ScarletMograineWhitemane::AdjustPlan(WorldModel const& world, TeamPlan& plan)
    {
        for (std::string& note : m_pendingNotes)
            plan.notes.push_back(std::move(note));
        m_pendingNotes.clear();

        if (m_stage != STAGE_DIED_ONCE && m_stage != STAGE_REVIVED)
            return;

        EnemyFacts const* whitemane = world.FindEnemy(m_whitemane);
        if (whitemane && whitemane->acting)
            EncounterHelpers::SetFocus(world, plan, *whitemane, "mograine-whitemane: kill Whitemane first");
        else if (m_stage == STAGE_DIED_ONCE && !plan.focus.enemy.IsEmpty() && plan.focus.enemy == m_mograine)
            EncounterHelpers::ClearFocus(plan);     // feigning death: cannot be attacked
    }

    void ScarletMograineWhitemane::Propose(PartyBotAI const* bot, WorldModel const& world,
                                           TeamPlan const& /*plan*/, std::vector<Proposal>& out) const
    {
        switch (bot->GetRole())
        {
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

    void ScarletMograineWhitemane::ProposeTank(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        ObjectGuid const self = me->GetObjectGuid();
        PartyMemberFacts const* myFacts = world.FindMember(self);
        if (!myFacts || !myFacts->CanAct())
            return;
        uint32 const now = WorldTimer::getMSTime();

        // 1. Mograine just got up: healer threat piled on him while he lay there (problem 3).
        EnemyFacts const* mograine = world.FindEnemy(m_mograine);
        if (m_stage == STAGE_REVIVED && m_revivedAtMs &&
            WorldTimer::getMSTimeDiff(m_revivedAtMs, now) < REVIVE_TAUNT_WINDOW_MS &&
            mograine && mograine->acting)
        {
            ObjectGuid const next = NextVictim(*mograine);
            PartyMemberFacts const* victim = next.IsEmpty() ? nullptr : world.FindMember(next);
            Unit* unit = me->GetMap()->GetUnit(m_mograine);
            if (next != self && victim && !victim->isPet && unit)
            {
                float const benefit = Value::HitLoss(world, *mograine, *victim, REVIVE_LOOSE_SEC);
                float const cost = Value::LostAction(world, *myFacts, Value::GCD_SEC);
                for (SpellEntry const* taunt : bot->m_spellListTaunt)
                {
                    if (!bot->CanTryToCastSpell(unit, taunt))
                        continue;
                    Proposal p = EncounterHelpers::MakeReplace(SOURCE, ProposalTier::Urgent, BotActionType::Cast,
                                                               m_mograine, taunt);
                    p.reason = "Mograine revived, not on the tank";
                    p.SetValue(benefit, cost);
                    out.push_back(p);
                }
            }
        }

        // 2. Feign death: get on Whitemane as soon as she is in the fight.
        EnemyFacts const* whitemane = world.FindEnemy(m_whitemane);
        if (m_stage == STAGE_DIED_ONCE && whitemane && whitemane->acting)
        {
            Unit const* current = me->GetVictim();
            if (!current || current->GetObjectGuid() != m_whitemane)
            {
                float loss = 0.0f;
                if (!whitemane->victim.IsEmpty() && whitemane->victim != self)
                    if (PartyMemberFacts const* v = world.FindMember(whitemane->victim))
                        if (!v->isPet)
                            loss = Value::HitLoss(world, *whitemane, *v, ThreatBehavior::TANK_RECLAIM_SEC);
                Proposal p = EncounterHelpers::MakeReplace(SOURCE, ProposalTier::Assigned, BotActionType::Attack, m_whitemane);
                p.reason = "Whitemane arrived, tank her";
                p.SetValue(loss + FIRST_THREAT_POINTS,
                           Value::LostAction(world, *myFacts, ThreatBehavior::SWITCH_TARGET_SEC));
                out.push_back(p);
            }
        }
    }

    void ScarletMograineWhitemane::ProposeHealer(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const
    {
        if (m_stage != STAGE_DIED_ONCE)
            return;
        EnemyFacts const* whitemane = world.FindEnemy(m_whitemane);
        if (!whitemane || !whitemane->acting || whitemane->healthPct <= SLEEP_PCT || whitemane->healthPct > PRE_SLEEP_PCT)
            return;

        PartyMemberFacts const* tank = world.FindMember(m_tank);
        PartyMemberFacts const* myFacts = world.FindMember(bot->me->GetObjectGuid());
        if (!tank || !tank->alive || tank->healthPct >= TANK_TOPUP_PCT || !myFacts || !myFacts->CanAct())
            return;
        for (PartyMemberFacts const& m : world.party)
            if (!m.isPet && m.alive && m.guid != tank->guid && m.healthPct < OTHER_CRITICAL_PCT)
                return;     // someone else needs it more: the rotation decides

        uint32 const now = WorldTimer::getMSTime();
        if (bot->Directives().IsVetoed(VetoType::Heal, tank->guid, 0, now))
            return;         // ThreatBehavior (or someone) found healing the tank pulls aggro
        Unit* target = bot->me->GetMap()->GetUnit(tank->guid);
        if (!target)
            return;

        // Strongest first (HealSpellCompare): the first one that does not overheal, else the smallest.
        float const missing = float(tank->maxHealth > tank->health ? tank->maxHealth - tank->health : 0);
        SpellEntry const* chosen = nullptr;
        for (SpellEntry const* spell : bot->m_spellListDirectHeal)
        {
            if (!bot->CanTryToCastSpell(target, spell))
                continue;
            chosen = spell;
            if (float(BasePoints(spell)) <= missing)
                break;
        }
        if (!chosen)
            return;

        float const healed = std::min(missing, float(BasePoints(chosen)));
        Proposal p = EncounterHelpers::MakeReplace(SOURCE, ProposalTier::Assigned, BotActionType::Cast, tank->guid, chosen);
        p.reason = "top up tank before Deep Sleep";
        p.SetValue(SLEEP_WEIGHT * Value::Points(world, healed), Value::LostAction(world, *myFacts, HEAL_CAST_SEC));
        out.push_back(p);
    }
}
