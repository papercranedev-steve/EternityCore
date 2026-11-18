/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "TeamBotAI.h"
#include "Player.h"
#include "Corpse.h"
#include "CreatureAI.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "PlayerBotMgr.h"
#include "Opcodes.h"
#include "World.h"
#include "WorldPacket.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "Chat.h"
#include "Utilities/Random.h"
#include "RoleCapability.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Team.h"
#include "Commander/BotDirectives.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Classes/RaceKit.h"
#include "Rotations/ClassRotation.h"
#include "Commander/VetoChecks.h"
#include "Commander/PartyCommander.h"
#include "Commander/Behaviors/Combat/AreaDamageBehavior.h"
#include "Commander/Behaviors/Constraints.h"
#include "Commander/Behaviors/CurrentAction.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Values/RestValue.h"
#include "Commander/Perception/Profiles/SpellTags.h"
#include "SpellMgr.h"
#include "HealSpells.h"

#include <algorithm>
#include <random>

enum TeamBotSpells
{
    PB_SPELL_FOOD = 1131,
    PB_SPELL_DRINK = 1137,
    // (Auto Shot, Shoot Wand: Rotations/ClassRotation.h)
};

bool TeamBotAI::UpdateBody()
{
    // Dropping down on the leader's trail: a cast (buff, heal) would stop the fall in the air.
    if (Commander::TrailFollow::IsFalling(me, m_trailFollow) || Commander::Landing::IsFalling(me, m_landing))
        return true;

    if (me->HasUnitState(UNIT_STATE_FEIGN_DEATH) && me->HasAuraType(SPELL_AURA_FEIGN_DEATH) &&
        ShouldEndFeignDeath())
        me->RemoveSpellsCausingAura(SPELL_AURA_FEIGN_DEATH);

    if (me->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL))
    {
        BreakControl();
        return true;
    }

    // Dead: release and run the corpse like a player (Commander/Behaviors/Support/CorpseRun.h).
    if (me->IsDead())
    {
        Commander::CorpseRun::Update(this, m_corpseRun, m_arrival);
        return true;
    }
    Commander::CorpseRun::OnAlive(me, m_corpseRun);

    // The pet (a triggered summon, no cast time): before the walk back, resting and mounting.
    // From the rotation's out-of-combat step only, a warlock back from a corpse run walked to
    // the leader, then rested, and the next pull came first: no Imp until the fight was over.
    if (!me->IsInCombat() && !me->IsMounted())
        SummonPetIfNeeded();

    // (before the auto-repeat step: it returns)
    UpdateWeaponStance();

    if (me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
    {
        // Auto shot / wand would otherwise skip the Commander hook below (e.g. stop shooting near aggro).
        if (me->IsInCombat() && me->GetVictim() && sCommanderMgr.OnBotCombatUpdate(this))
            return true;

        // Stop auto shot if no target.
        if (!me->GetVictim())
            me->InterruptSpell(CURRENT_AUTOREPEAT_SPELL, true);
        // Auto Shot survives a channel (SpellCaster::SetCurrentCastedSpell), so this branch also
        // runs while casting: the rotation's next shot broke the bot's own Mend Pet channel after
        // its mana was spent (strat-20261117-000150). Casting: only the Commander hook above may
        // end it (CastBreak weighs that).
        else if (me->IsNonMeleeSpellCasted(false, false, true))
            return true;
        else if (ClassRotation* pRotation = Rotation())
            pRotation->UpdateWhileAutoRepeating();

        return true;
    }

    // (Upstream "interrupt a pre-cast heal once its target is at full health", 2022, removed
    // 2026-11-16: it restarted a steady healer's heals on a full tank every bot tick,
    // strat-20261116-212207. A healer's heal that is no longer needed is CastWatch's.)
    if (me->IsNonMeleeSpellCasted(false, false, true))
        return true;

    if (me->GetTargetGuid() == me->GetObjectGuid())
        me->ClearTarget();

    // Ammo given to the bot (trade, bag panel) goes into the ammo slot, so Shoot Bow / Gun /
    // Crossbow (ranged pull) and Auto Shot work. Cheap when nothing is missing.
    if (!me->IsInCombat())
        EquipAmmoFromBags();
    return false;
}

bool TeamBotAI::RunCommanderHook(bool outOfCombatControl, bool needsRest, bool resting)
{
    // A victim that can no longer be hit (dead, under a control that breaks on damage, immune,
    // a charmed member) is dropped at once. Which enemy comes next is the Commander's
    // (TargetAssigner / TargetBehavior below).
    if (Unit* pOld = me->GetVictim())
        if (!IsValidHostileTarget(pOld))
            me->AttackStop();
    CancelSplashNearKeptControl();

    // Commander behaviors run before movement, so they can pick a target or a position
    // themselves. Since 2026-11-07 every target comes from there (target assignment doc): the
    // original target selection (leader's victim, own attackers, the first attacker of a
    // member, the tank's "defend party members") is deleted, not bypassed.
    if ((me->IsInCombat() || outOfCombatControl || needsRest || resting) &&
        sCommanderMgr.OnBotCombatUpdate(this))
        return true;

    // A healer not in combat during the party's fight: its heal step is here, after the hook
    // that set this tick's heal order (the out of combat AI runs before it).
    if (!me->IsInCombat() && IsHealingControlled() && CastHealOrder())
        return true;
    return false;
}

void TeamBotAI::FightVictim(Unit* pVictim)
{
    if (pVictim && !me->IsMoving() && !IsMovementHeld())
    {
        if (!me->HasUnitState(UNIT_STATE_MELEE_ATTACKING) &&
           (GetRole() == ROLE_MELEE_DPS || m_role == ROLE_TANK) &&
            IsValidHostileTarget(pVictim) &&
            AttackStart(pVictim))
            return;

        switch (me->GetMotionMaster()->GetCurrentMovementGeneratorType())
        {
            case IDLE_MOTION_TYPE:
            case FOLLOW_MOTION_TYPE:
                BeginChasing(pVictim);
                break;
        }
    }
    // Walking backwards (Commander Backpedal) faces the enemies coming after it: swing at the
    // victim without a chase (AttackStart would chase and end the walk). Without it the tank
    // walked back to its corner for 7 s after the shot without one swing, no rage and 0 threat
    // on five of the six enemies on it; a Counterspell and a few buffs turned them
    // (strat-20261108-200046).
    else if (pVictim && (me->IsMoving() || IsMovementHeld()) && IsBackpedaling() &&
             !me->HasUnitState(UNIT_STATE_MELEE_ATTACKING) &&
             (GetRole() == ROLE_MELEE_DPS || m_role == ROLE_TANK) && IsValidHostileTarget(pVictim))
        me->Attack(pVictim, true);

    KeepFacing(pVictim);

    // In combat; or opening on the creature a team of bots hunts (the rotation's first shot).
    if (me->IsInCombat() || IsOpeningOn(pVictim))
        UpdateInCombatAI();
}

bool TeamBotAI::IsOpeningOn(Unit const* pVictim) const
{
    if (!pVictim || pVictim->IsInCombat() || !sCommanderMgr.IsEnabled())
        return false;
    std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
    return commander && commander->GetPlan().hunt.target == pVictim->GetObjectGuid();
}

bool TeamBotAI::UpdateFrameHands(uint32 diff)
{
    // Commander: second look at a heal being cast, every frame (own CHECK_INTERVAL_MS inside),
    // before the throttle and the "casting -> return" below. A cancelled heal lets the rotation
    // pick a new target in this frame instead of after the next bot interval.
    if (!m_initialized || !me->IsInWorld() || me->IsBeingTeleported() || !me->IsAlive())
        m_castWatch.spell = nullptr;
    else if (Commander::CastWatch::Update(this, m_castWatch))
        m_updateTimer.Reset(0);
    // Same for an area channel (Blizzard): stop when the enemies its next ticks turn cost more than they deal.
    else if (Commander::AreaDamageBehavior::WatchChannel(this, m_areaChannelCheckMs))
        m_updateTimer.Reset(0);
    // Any cast or channel: stop it when an assigned interrupt or an area step cannot wait for it.
    else if (Commander::CastBreak::Update(this, m_castBreak))
        m_updateTimer.Reset(0);

    // Commander walk backwards: one step every frame, so the position and the packets follow
    // the run-back speed smoothly (the bot update runs every bot interval only).
    Commander::Backpedal::Update(this, m_backpedal, diff);
    // Commander jump turn cast: the jump's position and facing every frame.
    Commander::JumpCast::Update(this, m_jumpCast);
    // Commander leader trail walk: the next leg as soon as one ends.
    Commander::TrailFollow::Update(this, m_trailFollow);
    // Commander drop to the ground after a teleport (the point may be in the air).
    Commander::Landing::Update(this, m_landing);

    // Warrior stance swap (DoCastWithStance): cast the spell as soon as the stance is up, every
    // frame, so the bot stays in the other stance for one spell update instead of a bot interval.
    if (m_stanceSwap.spell && m_initialized && me->IsInWorld() && !me->IsBeingTeleported() &&
        ContinueStanceSwap())
        return true;

    // The bot update skips while casting and every cast fails on the global cooldown: without
    // this the next action came up to a bot interval after a cast or GCD ended (an instant
    // cast every ~2 s: strat-20261102-012937). Wake the update in the frame it ends.
    bool const busy = m_initialized && me->IsInWorld() &&
                      (me->IsNonMeleeSpellCasted(false, false, true) || me->HasGCD(nullptr));
    if (m_wasBusy && !busy)
        m_updateTimer.Reset(0);
    m_wasBusy = busy;
    return false;
}

Commander::BotDirectives& TeamBotAI::Directives()
{
    // Only called from the bot's map thread.
    if (!m_directives)
        m_directives = std::make_shared<Commander::BotDirectives>();
    return *m_directives;
}

Commander::BotDirectives const& TeamBotAI::Directives() const
{
    static Commander::BotDirectives const empty;
    return m_directives ? *m_directives : empty;
}

Commander::ClassKit const& TeamBotAI::Kit() const
{
    if (!m_classKit)
        m_classKit = Commander::ClassKits::Create(*this);
    return *m_classKit;
}

ClassRotation* TeamBotAI::Rotation()
{
    // The rotation may depend on the role too (warrior tank): made again after a role change.
    CombatBotRoles const role = GetRole();
    if (!m_rotationCreated || role != m_rotationRole)
    {
        m_rotation = ClassRotations::Create(*this);
        m_rotationRole = role;
        m_rotationCreated = true;
    }
    return m_rotation.get();
}

bool TeamBotAI::IsMovementHeld() const
{
    return m_directives && m_directives->IsMovementHeld(WorldTimer::getMSTime());
}

void TeamBotAI::WakeUp()
{
    // Only the normal wait between bot updates is cut short, not a pause.
    if (m_updateTimer.GetExpiry() <= int32(sCommanderMgr.GetBotUpdateInterval()))
        m_updateTimer.Reset(0);
}

void TeamBotAI::ClearMovementHold()
{
    // No directives yet: nothing is held, do not create them here.
    if (m_directives)
        m_directives->ReleaseMovement();
    Commander::Backpedal::Stop(me, m_backpedal);
    Commander::TrailFollow::Stop(this, m_trailFollow);
}

bool TeamBotAI::OrderJumpCast(SpellEntry const* pSpellEntry, float orientation)
{
    return Commander::JumpCast::Order(this, m_jumpCast, pSpellEntry, orientation);
}

bool TeamBotAI::OrderBackpedal(Commander::Position3 const& to, ObjectGuid face, uint32 durationMs)
{
    return Commander::Backpedal::Order(this, m_backpedal, to, face, durationMs);
}

bool TeamBotAI::IsStanceControlled() const
{
    // (the stance modules take over only while the party fights: no own combat check here,
    // a healer still out of combat is placed too)
    // (a ranged damage dealer fighting in melee, no mana and no ranged weapon or a form, keeps
    // the original chase: RangedStanceBehavior does not place it)
    // (tanks since 2026-11-14: TankStanceBehavior)
    return m_directives &&
           (GetRole() == ROLE_HEALER || GetRole() == ROLE_MELEE_DPS || m_role == ROLE_TANK || UsesCasterChase()) &&
           m_directives->IsTakenOver(Commander::Takeover::Stance, WorldTimer::getMSTime());
}

bool TeamBotAI::IsChaseControlled() const
{
    return (UsesCasterChase() || GetRole() == ROLE_MELEE_DPS || m_role == ROLE_TANK) && IsStanceControlled();
}

bool TeamBotAI::OrderChase(Unit* pVictim, float angle)
{
    if (!pVictim || !pVictim->IsAlive() || me->HasUnitState(UNIT_STATE_ROOT))
        return false;
    float current;
    if (GetChaseAngle(pVictim, current) && std::fabs(current - angle) < 0.01f)
        return true;
    if (me->HasDistanceCasterMovement())
        me->SetCasterChaseDistance(0.0f);
    // (distance 1 as the original chase, so the angle holds)
    me->GetMotionMaster()->MoveChase(pVictim, 1.0f, angle);
    m_chaseVictim = pVictim->GetObjectGuid();
    m_chaseAngle = angle;
    return true;
}

bool TeamBotAI::GetChaseAngle(Unit const* pVictim, float& angle) const
{
    if (!pVictim || me->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE ||
        me->GetVictim() != pVictim)
        return false;
    if (m_chaseVictim == pVictim->GetObjectGuid())
        angle = m_chaseAngle;
    else
        angle = GetRole() == ROLE_MELEE_DPS ? M_PI_F : 0.0f;    // the original chase (BeginChasing)
    return true;
}

void TeamBotAI::StopTakenOverMovement()
{
    if (IsMovementHeld())
        return;
    // MoveIdle alone left the follow spline running to its end (up to a few yards).
    MovementGeneratorType const motion = me->GetMotionMaster()->GetCurrentMovementGeneratorType();
    if ((motion == FOLLOW_MOTION_TYPE && IsStanceControlled()) ||
        (motion == CHASE_MOTION_TYPE && me->GetVictim() && IsChaseControlled() &&
         m_chaseVictim != me->GetVictim()->GetObjectGuid()))
    {
        if (!me->IsStopped())
            me->StopMoving();
        me->GetMotionMaster()->Clear(false, true);
        me->GetMotionMaster()->MoveIdle();
    }
}

void TeamBotAI::BreakControl()
{
    // Divine Shield / Ice Block lift a stun too, but they are the bot's emergency cooldowns: the
    // team rescue decides them (RescueAssigner gives one when this bot is about to die), not the
    // stun. The arbiter does not run while the bot is controlled, so its duty is cast here.
    if (CastRescueWhileControlled())
        return;
    // Will of the Forsaken (2 min) before the trinket (5 min): one answer to a fear or charm
    // (cooldown doc 2026-11-14, section 8 of the spell list).
    if (SpellEntry const* pBreak = Commander::RaceKit::FearBreak(*this))
        if ((me->HasAuraType(SPELL_AURA_MOD_FEAR) || me->HasAuraType(SPELL_AURA_MOD_CHARM)) &&
            me->IsSpellReady(pBreak) && DoCastSpell(me, pBreak) == SPELL_CAST_OK)
            return;
    // Class fear break (warrior Berserker Rage, 30 s): fear only, not charm.
    if (SpellEntry const* pBreak = Kit().FearBreak())
        if (me->HasAuraType(SPELL_AURA_MOD_FEAR) && me->IsSpellReady(pBreak) &&
            pBreak->GetErrorAtShapeshiftedCast(me->GetShapeshiftForm()) == SPELL_CAST_OK &&
            DoCastSpell(me, pBreak) == SPELL_CAST_OK)
            return;
    BreakCrowdControlEffects(false);
}

bool TeamBotAI::CastRescueWhileControlled()
{
    // Same map as the bot, so the commander belongs to this map thread.
    std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
    if (!commander)
        return false;
    Commander::RescueDuty const* duty = commander->GetPlan().FindRescueFor(me->GetObjectGuid());
    if (!duty || duty->way != Commander::RescueWay::Save)
        return false;
    // Only a spell the server lets a controlled caster cast (Spell::CheckCasterAuras).
    SpellEntry const* spell = sSpellMgr.GetSpellEntry(duty->spellId);
    if (!spell || !spell->HasAttribute(SPELL_ATTR_EX_IMMUNITY_PURGES_EFFECT))
        return false;
    Unit* target = me->GetMap()->GetUnit(duty->victim);
    if (!target || !target->IsAlive() || (target != me && !me->IsWithinLOSInMap(target)) ||
        !CanTryToCastSpell(target, spell))
        return false;
    return DoCastSpell(target, spell) == SPELL_CAST_OK;
}

bool TeamBotAI::ShouldEndFeignDeath() const
{
    std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
    if (commander)
    {
        // The fight is lost (WipeAssigner): lie still until it is over, to run back for the others.
        Commander::WipePlan const& wipe = commander->GetPlan().wipe;
        if (wipe.active)
            for (Commander::WipeHider const& h : wipe.hiders)
                if (h.bot == me->GetObjectGuid())
                    return false;
    }

    // The enemies pick their next victim on their own update: give them that time first.
    Unit::AuraList const& feigns = me->GetAurasByType(SPELL_AURA_FEIGN_DEATH);
    if (!feigns.empty())
    {
        Aura const* aura = feigns.front();
        int32 const maxMs = aura->GetAuraMaxDuration();
        if (maxMs > 0 && maxMs - aura->GetAuraDuration() < int32(FEIGN_DEATH_MIN_MS))
            return false;
    }

    // The threat is gone (Unit::SetFeignDeath): up again once nothing still hits this bot.
    // The pet keeps the hunter in combat, so the combat flag is not the signal: up to
    // 2026-11-16 it waited for the pet to leave combat and lay there the whole fight.
    if (!me->GetAttackers().empty())
        return false;
    if (commander)
        for (Commander::EnemyFacts const& e : commander->GetWorld().enemies)
            if (e.victim == me->GetObjectGuid())
                return false;
    return true;
}

bool TeamBotAI::IsStealthControlled() const
{
    return m_directives && m_directives->IsTakenOver(Commander::Takeover::Stealth, WorldTimer::getMSTime());
}

bool TeamBotAI::IsEngageForbidden(Unit const* pUnit) const
{
    if (!pUnit)
        return false;
    // Same map as the bot, so the commander belongs to this map thread.
    std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
    return commander && !commander->GetPlan().MayEngage(me->GetObjectGuid(), pUnit->GetObjectGuid());
}

bool TeamBotAI::IsGapCloserForbidden(SpellEntry const* pSpell) const
{
    if (!pSpell || !pSpell->HasEffect(SPELL_EFFECT_CHARGE))
        return false;
    std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
    return commander && commander->GetPlan().ForbidsGapCloser(me->GetObjectGuid());
}

bool TeamBotAI::IsSpellHeld(SpellEntry const* pSpell) const
{
    if (!pSpell)
        return false;
    std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
    return commander && commander->GetPlan().HoldsSpell(me->GetObjectGuid(), pSpell->Id, pSpell->NeedsComboPoints());
}

bool TeamBotAI::IsLongCastHeld(SpellEntry const* pSpell) const
{
    // Cheap tests first: most casts of a tick are instant.
    if (!pSpell || !me->IsInCombat() ||
        (pSpell->GetCastTime(me) <= 0 && !pSpell->IsChanneledSpell()))
        return false;
    std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
    return commander && Commander::Constraints::LongCastHeld(this, commander->GetPlan(), pSpell) != nullptr;
}

bool TeamBotAI::IsCastWasted(Unit const* pTarget, SpellEntry const* pSpell) const
{
    // Cheap tests first: most casts of a tick are instant or not at a creature.
    if (!pSpell || !pTarget || pTarget == me || !pTarget->IsCreature() || !me->IsInCombat() ||
        !Commander::Constraints::MayBeWasted(this, pSpell))
        return false;
    std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
    return commander && Commander::Constraints::WastedCast(this, commander->GetWorld(), pTarget, pSpell,
                                                           WorldTimer::getMSTime()) != nullptr;
}

void TeamBotAI::AutoAssignRole()
{
    // Before: only the 31-point talents counted (Shield Slam, Holy Shield, Shadowform...), so
    // below level 40 warriors were always melee and paladins, priests, shamans, druids healers.
    m_role = RoleCapabilities::Assess(me).Best();
}

void TeamBotAI::SetRole(CombatBotRoles role)
{
    m_roleBeforeEmergency = ROLE_INVALID;   // the user's choice ends an emergency role
    m_roleLocked = role != ROLE_INVALID;
    m_role = role;
    if (role == ROLE_INVALID)
        AutoAssignRole();
    m_resetSpellData = true;
}

void TeamBotAI::AssignRole(CombatBotRoles role)
{
    if (m_roleLocked || HasEmergencyRole() || role == ROLE_INVALID || role == m_role)
        return;
    m_role = role;
    m_resetSpellData = true;
}

void TeamBotAI::SetEmergencyRole(CombatBotRoles role)
{
    if (role == ROLE_INVALID || role == m_role)
        return;
    if (!HasEmergencyRole())
        m_roleBeforeEmergency = m_role;
    m_role = role;
    m_resetSpellData = true;
}

void TeamBotAI::RestoreRole()
{
    if (!HasEmergencyRole())
        return;
    if (m_role != m_roleBeforeEmergency)
    {
        m_role = m_roleBeforeEmergency;
        m_resetSpellData = true;
    }
    m_roleBeforeEmergency = ROLE_INVALID;
}

void TeamBotAI::BeginChasing(Unit* pVictim) const
{
    if (IsMovementHeld())
        return;
    // Fix: the rogue chased its casters back into a patrol's path after the party moved off
    // it (strat-20261102-225354). The Commander patrol guard brings them out instead; a bot
    // the brain sent to kill one may go (TeamPlan::EngageBlock).
    if (IsEngageForbidden(pVictim))
        return;
    // Fix: the 25 yd chase walked the mage to 20 yd from a wandering Ravaged Cadaver's home
    // (aggro range 17, wander 5); it came after 14 s with its pack of seven
    // (strat-20261107-152527). The Commander stance modules pick a spot instead; for melee the
    // melee stance module chases itself (OrderChase), checking the route for idle packs.
    if (IsChaseControlled())
        return;

    m_chaseVictim.Clear();
    CombatBotBaseAI::BeginChasing(pVictim);
}

bool TeamBotAI::IsValidDispelTarget(Unit const* pTarget, SpellEntry const* pSpellEntry) const
{
    if (!CombatBotBaseAI::IsValidDispelTarget(pTarget, pSpellEntry))
        return false;
    // Offensive dispels (purge) are not vetoed.
    if (!pTarget->IsFriendlyTo(me))
        return true;
    return Commander::VetoChecks::HasAllowedDebuff(this, pTarget, pSpellEntry);
}

bool TeamBotAI::IsHealingControlled() const
{
    // (the heal module takes over only while the party fights, also before this healer is in
    // combat itself: HealPlanBehavior::ControlsHealingOf)
    return m_directives && m_directives->IsTakenOver(Commander::Takeover::Healing, WorldTimer::getMSTime());
}

bool TeamBotAI::CastHealOrder()
{
    uint32 const now = WorldTimer::getMSTime();
    Commander::BotAction const* order = m_directives ? m_directives->GetHealOrder(now) : nullptr;
    if (!order || !order->spell)
        return false;
    Commander::BotAction const action = *order;
    m_directives->ClearHealOrder();     // one cast per order

    Unit* pTarget = action.target.IsEmpty() ? me : me->GetMap()->GetUnit(action.target);
    if (!pTarget || !pTarget->IsInWorld() || !pTarget->IsAlive())
        return false;
    // A healer resting during the party's fight (RestBehavior) gets up for the heal.
    if (!me->IsStandingUp())
        me->SetStandState(UNIT_STAND_STATE_STAND);
    if (!CanTryToCastSpell(pTarget, action.spell))
        return false;
    if (!CastPrepSpell(action.prepSpell))
        return false;
    if (DoCastSpell(pTarget, action.spell) != SPELL_CAST_OK)
        return false;
    // A pure HoT starts no heal chain: the target is not topped up by it now, the next heal
    // waits for its deficit as usual.
    // (Holy Shock: its triggered heal, HealSpells::HealingPart)
    if (HealSpells::HealingPart(action.spell)->HasEffect(SPELL_EFFECT_HEAL))
    {
        uint32 const busyMs = std::max(uint32(HealSpells::GCD_SEC * 1000.0f), action.spell->GetCastTime(me));
        m_directives->NoteOrderedHeal(pTarget->GetObjectGuid(), now, busyMs);
    }
    return true;
}

bool TeamBotAI::CastPrepSpell(SpellEntry const* pPrep)
{
    if (!pPrep)
        return true;
    // Already on: put up in an earlier tick whose main cast did not go out.
    if (me->HasAura(pPrep->Id))
        return true;
    if (!CanTryToCastSpell(me, pPrep))
        return false;
    return DoCastSpell(me, pPrep) == SPELL_CAST_OK;
}

bool TeamBotAI::FindAndPreHealTarget()
{
    if (IsHealingControlled())
        return false;   // the Commander's heal order already ran at the heal step
    return CombatBotBaseAI::FindAndPreHealTarget();
}

bool TeamBotAI::FindAndHealInjuredAlly(float selfHealPercent, float groupHealPercent)
{
    // The Commander heal module decides target and rank (Behaviors/Support/HealPlanBehavior.h).
    // No order = rest: nobody needs a heal yet. Out of combat the order is cast after the
    // Commander hook (UpdateAI): the out of combat AI runs before the hook sets it.
    if (IsHealingControlled())
        return me->IsInCombat() && CastHealOrder();

    Unit* pTarget = SelectHealTarget(selfHealPercent, groupHealPercent);
    if (!pTarget)
        return false;

    if (Commander::VetoChecks::IsHealVetoed(this, pTarget))
        return false;

    return HealInjuredTarget(pTarget);
}

float TeamBotAI::GetIncomingDps(Unit const* pTarget) const
{
    // The Commander's facts: melee after armor plus ranged spells of every enemy on the target,
    // refreshed every PerceptionMs. Same map as the bot, so the commander is this thread's.
    if (std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me))
    {
        Commander::WorldModel const& world = commander->GetWorld();
        // 0 also when the attackers are not in the snapshot yet (just pulled): melee then.
        if (world.FindMember(pTarget->GetObjectGuid()))
            if (float const dps = Commander::Value::IncomingOn(world, pTarget->GetObjectGuid()))
                return dps;
    }
    // (no snapshot of the target yet: its attackers' melee)
    return CombatBotBaseAI::GetIncomingDps(pTarget);
}

bool TeamBotAI::IsValidHostileTarget(Unit const* pTarget) const
{
    // The bot the Commander sent to wake the last controlled enemy may attack it
    // (Coordination/Doctrine/ControlledEnd.h); everyone else leaves a breakable control alone.
    bool const valid = CombatBotBaseAI::IsValidHostileTarget(pTarget) ||
                      (CombatBotBaseAI::IsValidHostileTargetIgnoringControl(pTarget) && IsAssignedToBreakControl(pTarget));
    return valid && !Commander::VetoChecks::IsCharmedPartyMember(this, pTarget);
}

bool TeamBotAI::IsAssignedToBreakControl(Unit const* pTarget) const
{
    if (!pTarget || !me->IsInCombat())
        return false;
    // Same map as the bot, so the commander belongs to this map thread.
    std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
    return commander && commander->GetPlan().controlEnd.Breaks(me->GetObjectGuid(), pTarget->GetObjectGuid(),
                                                               WorldTimer::getMSTime());
}

// Same as CombatBotBaseAI::SelectDispelTarget, calling the IsValidDispelTarget above.
Player* TeamBotAI::SelectDispelTarget(SpellEntry const* pSpellEntry) const
{
    // Fix: only dispels that may target a hostile unit (Dispel Magic) can remove a creature's
    // mind control. Cleanse, Purify, Remove Curse, Cure Poison / Disease are friendly-only
    // (TARGET_UNIT_FRIEND): the cast fails with bad targets, and because the charmed member
    // was returned first, every other member's debuffs went undispelled while it lasted.
    bool canTargetHostile = true;
    for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
        if (pSpellEntry->Effect[i] == SPELL_EFFECT_DISPEL && pSpellEntry->EffectImplicitTargetA[i] == TARGET_UNIT_FRIEND)
            canTargetHostile = false;

    {
        for (Player* pMember : Commander::Team::Members(me))
        {
            if (pMember)
            {
                // A member mind controlled by a creature is hostile, so IsValidHelpfulTarget fails;
                // CombatBotBaseAI::IsValidDispelTarget already handles the charm aura itself.
                if ((me->IsValidHelpfulTarget(pMember) ||
                     (canTargetHostile && Commander::VetoChecks::IsCharmedPartyMember(this, pMember))) &&
                   !pMember->IsGameMaster() &&
                    IsValidDispelTarget(pMember, pSpellEntry) &&
                    me->IsWithinLOSInMap(pMember) &&
                    me->IsWithinDist(pMember, 30.0f))
                    return pMember;
            }
        }
    }

    return nullptr;
}

SpellEntry const* TeamBotAI::ActiveRest(BotShared::FoodAndDrink::Kind kind) const
{
    if (SpellEntry const* pSpell = BotShared::FoodAndDrink::Active(me, kind))
        return pSpell;
    // (the free aura spells by id too, whatever their flags)
    uint32 const freeSpell = kind == BotShared::FoodAndDrink::Kind::Food ? PB_SPELL_FOOD : PB_SPELL_DRINK;
    return me->HasAura(freeSpell) ? sSpellMgr.GetSpellEntry(freeSpell) : nullptr;
}

SpellEntry const* TeamBotAI::NextRest(BotShared::FoodAndDrink::Kind kind, Item** item) const
{
    if (item)
        *item = nullptr;
    if (GetsFreeSupplies())
        return sSpellMgr.GetSpellEntry(kind == BotShared::FoodAndDrink::Kind::Food ? PB_SPELL_FOOD : PB_SPELL_DRINK);
    Item* pItem = BotShared::FoodAndDrink::BestInBags(me, kind);
    if (!pItem)
        return nullptr;
    if (item)
        *item = pItem;
    return BotShared::FoodAndDrink::ItemSpell(pItem->GetProto(), kind);
}

float TeamBotAI::RestRatePerSec(BotShared::FoodAndDrink::Kind kind) const
{
    SpellEntry const* pSpell = ActiveRest(kind);
    if (!pSpell)
        pSpell = NextRest(kind);
    if (!pSpell)
        return 0.0f;
    // The free spells' rate the old way (amount per 5 s) when their flags make them no
    // seated aura to FoodAndDrink.
    float const rate = BotShared::FoodAndDrink::RatePerSec(me, pSpell, kind);
    if (rate > 0.0f || (pSpell->Id != PB_SPELL_FOOD && pSpell->Id != PB_SPELL_DRINK))
        return rate;
    AuraType const aura = kind == BotShared::FoodAndDrink::Kind::Food ? SPELL_AURA_MOD_REGEN : SPELL_AURA_MOD_POWER_REGEN;
    float const serverRate = sWorld.getConfig(kind == BotShared::FoodAndDrink::Kind::Food ? CONFIG_FLOAT_RATE_HEALTH : CONFIG_FLOAT_RATE_POWER_MANA);
    for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
        if (pSpell->EffectApplyAuraName[i] == aura)
            return pSpell->CalculateSimpleValue(SpellEffectIndex(i)) / 5.0f * serverRate;
    return 0.0f;
}

bool TeamBotAI::BeginRest(BotShared::FoodAndDrink::Kind kind)
{
    Item* pItem = nullptr;
    SpellEntry const* pSpell = NextRest(kind, &pItem);
    if (!pSpell)
        return false;
    if (pItem)
        return UseBagItem(pItem->GetEntry(), me);
    me->CastSpell(me, pSpell, true);
    me->RemoveSpellCooldown(pSpell);
    return ActiveRest(kind) != nullptr;
}

float TeamBotAI::DrinkManaPerSec() const
{
    // (mana the active power only: a druid in bear or cat form does not drink)
    if (me->GetPowerType() != POWER_MANA || !me->GetMaxPower(POWER_MANA))
        return 0.0f;
    return RestRatePerSec(BotShared::FoodAndDrink::Kind::Drink);
}

float TeamBotAI::FoodHealthPerSec() const
{
    return RestRatePerSec(BotShared::FoodAndDrink::Kind::Food);
}

bool TeamBotAI::IsDrinking() const
{
    return ActiveRest(BotShared::FoodAndDrink::Kind::Drink) != nullptr;
}

bool TeamBotAI::IsResting() const
{
    return IsDrinking() || ActiveRest(BotShared::FoodAndDrink::Kind::Food) != nullptr;
}

bool TeamBotAI::NeedsRest() const
{
    float const full = Commander::RestValue::FULL_SHARE;
    if (float(me->GetHealth()) < float(me->GetMaxHealth()) * full && FoodHealthPerSec() > 0.0f)
        return true;
    return DrinkManaPerSec() > 0.0f && float(me->GetPower(POWER_MANA)) < float(me->GetMaxPower(POWER_MANA)) * full;
}

bool TeamBotAI::StartResting()
{
    using BotShared::FoodAndDrink::Kind;
    if (me->IsInCombat() || m_isBuffing || me->GetVictim())
        return false;
    bool const needToEat = me->GetHealth() < me->GetMaxHealth() && FoodHealthPerSec() > 0.0f;
    bool const needToDrink = DrinkManaPerSec() > 0.0f && me->GetPower(POWER_MANA) < me->GetMaxPower(POWER_MANA);
    if (!needToEat && !needToDrink)
        return false;

    // About to be put next to its team (a party bot far behind the leader): full at once, as before.
    if (FillsUpAtOnce())
    {
        me->SetHealth(me->GetMaxHealth());
        if (me->GetPowerType() == POWER_MANA)
            me->SetPower(POWER_MANA, me->GetMaxPower(POWER_MANA));
        return false;
    }

    bool const startEat = needToEat && !ActiveRest(Kind::Food);
    bool const startDrink = needToDrink && !ActiveRest(Kind::Drink);
    if (startEat || startDrink)
    {
        if (me->IsMounted())
            me->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType())
        {
            me->StopMoving();
            me->GetMotionMaster()->Clear(false, true);
            me->GetMotionMaster()->MoveIdle();
        }
    }
    if (startEat)
        BeginRest(Kind::Food);
    // (an item that is food and drink at once covers both)
    if (startDrink && !ActiveRest(Kind::Drink))
        BeginRest(Kind::Drink);
    return IsResting();
}

void TeamBotAI::EndRestWhenFull()
{
    using BotShared::FoodAndDrink::Kind;
    SpellEntry const* pDrink = ActiveRest(Kind::Drink);
    SpellEntry const* pFood = ActiveRest(Kind::Food);
    if (!pDrink && !pFood)
        return;
    bool const manaFull = me->GetPowerType() != POWER_MANA || me->GetPower(POWER_MANA) >= me->GetMaxPower(POWER_MANA);
    bool const healthFull = me->GetHealth() >= me->GetMaxHealth() &&
        !me->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE) &&
        !me->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE_PERCENT) &&
        !me->HasAuraType(SPELL_AURA_PERIODIC_LEECH);
    // A food-and-drink item's aura does both: off only when both are full.
    if (pDrink && manaFull && (pDrink != pFood || healthFull))
        me->RemoveAurasDueToSpell(pDrink->Id);
    if (pFood && healthFull && (pFood != pDrink || manaFull))
        me->RemoveAurasDueToSpell(pFood->Id);
}

bool TeamBotAI::CanTryToCastSpell(Unit const* pTarget, SpellEntry const* pSpellEntry) const
{
    return CombatBotBaseAI::CanTryToCastSpell(pTarget, pSpellEntry) && PassesPartyCastChecks(pTarget, pSpellEntry);
}

float TeamBotAI::SpellReadyInSec(Unit const* pTarget, SpellEntry const* pSpellEntry) const
{
    if (!pSpellEntry || !pTarget)
        return -1.0f;
    if (CanTryToCastSpell(pTarget, pSpellEntry))
        return 0.0f;
    int32 const cooldown = me->GetSpellCooldownLeft(pSpellEntry);
    if (cooldown < 0)
        return -1.0f;
    uint32 const left = std::max(uint32(cooldown), me->GetGCDLeft(pSpellEntry));
    if (!left)
        return -1.0f;
    uint32 const power = pSpellEntry->powerType == POWER_HEALTH ? 0 : me->GetPower(Powers(pSpellEntry->powerType));
    if (!CanTryToCastSpellBesidesTimers(pTarget, pSpellEntry, me->GetShapeshiftForm(), power) ||
        !PassesPartyCastChecks(pTarget, pSpellEntry))
        return -1.0f;
    return left / 1000.0f;
}

bool TeamBotAI::IsSpammedOverOwnAura(SpellEntry const* pSpellEntry) const
{
    return pSpellEntry && (pSpellEntry == Kit().Filler() || pSpellEntry->GetStackAmount() > 1);
}

// Checks the server makes at cast time (Spell::CheckRange, Spell::CheckCast, Spell::CheckItems)
// that CombatBotBaseAI::CanTryToCastSpellAs does not, or does differently. Without them the
// bot sends the cast, gets refused and loses the tick (strat-20261108-200046: the tank's Shield
// Slam / Sunder Armor / Taunt failed 'range' at 6-14 yd; the rogue's Backstab failed 26 "needs
// a dagger in the main hand" and 51 "not behind" 11 times).
bool TeamBotAI::PassesServerCastChecks(Unit const* pTarget, SpellEntry const* pSpellEntry) const
{
    // Weapon or armor the spell needs (Backstab: a dagger in the main hand; Shield Slam: a
    // shield). The server leaves out the other hand's weapon (Spell::CheckItems).
    if (pSpellEntry->EquippedItemClass >= 0)
    {
        Item const* ignore = nullptr;
        WeaponAttackType const attack = pSpellEntry->GetWeaponAttackType();
        if (attack == BASE_ATTACK)
            ignore = me->GetWeaponForAttack(OFF_ATTACK);
        else if (attack == OFF_ATTACK)
            ignore = me->GetWeaponForAttack(BASE_ATTACK);
        if (!me->HasItemFitToSpellReqirements(pSpellEntry, ignore))
            return false;
    }

    if (!pTarget || pTarget == me || pSpellEntry->EffectImplicitTargetA[0] == TARGET_UNIT_CASTER)
        return true;

    // Combat range: the base check takes the combat distance (centres minus both combat
    // reaches) against the DBC 5.5 yd, ~8.5 yd between centres on a player-sized target. The
    // server takes the centres against both combat reaches + 4/3 yd (at least 5).
    // (Facing is not checked here: DoCastSpell turns the bot first.)
    if (pSpellEntry->rangeIndex == SPELL_RANGE_IDX_COMBAT && !pSpellEntry->IsNextMeleeSwingSpell())
    {
        float base = ATTACK_DISTANCE;
        float const mod = 1.0f + me->ApplySpellMod(pSpellEntry->Id, SPELLMOD_RANGE, base, nullptr, false);
        if (!me->CanReachWithMeleeSpellAttack(pTarget, mod))
            return false;
    }

    // Must be behind the target (Backstab, Ambush, Garrote, Ravage, Shred). A creature faces its
    // victim unless stunned, confused or fleeing: Unit::IsBehindTarget (strict).
    if (pSpellEntry->IsFromBehindOnlySpell() && !me->IsBehindTarget(pTarget, true))
        return false;

    return true;
}

bool TeamBotAI::PassesPartyCastChecks(Unit const* pTarget, SpellEntry const* pSpellEntry) const
{
    if (!PassesServerCastChecks(pTarget, pSpellEntry))
        return false;
    // Already queued on the next swing (Heroic Strike, Cleave, Maul, Raptor Strike): casting it
    // again interrupts the queued one and queues it anew, every tick until the swing; the client
    // shows the bot frozen with its weapon out (stockade-20261114-194753, 24.5-31.4 s).
    if (pSpellEntry->IsNextMeleeSwingSpell())
        if (Spell const* queued = me->GetCurrentSpell(CURRENT_MELEE_SPELL))
            if (queued->m_spellInfo == pSpellEntry)
                return false;
    // The pull brings the pack back: charging in would put the tank in the middle of it.
    if (IsGapCloserForbidden(pSpellEntry))
        return false;
    // Kept for an enemy about to run away (Commander flee stop): only that module casts it.
    if (IsSpellHeld(pSpellEntry))
        return false;
    // The area phase reserves this bot: no cast or channel time but its area spells (a 2.5 s
    // Frostbolt held the Blizzard back, strat-20261113-235712).
    if (IsLongCastHeld(pSpellEntry))
        return false;
    // The others kill the target before this cast would land (Commander finishing forecast).
    if (IsCastWasted(pTarget, pSpellEntry))
        return false;
    // A control the brain keeps unbroken (polymorph, shackle, sap): no damage on it, its area,
    // a chained hit or a melee splash next to it. Every cast of the bot, rotations included.
    if (WouldWakeKeptControl(pTarget, pSpellEntry))
        return false;

    if (pSpellEntry->IsAreaOfEffectSpell() && !pSpellEntry->IsPositiveSpell())
    {
        // (controlled enemies: WouldWakeKeptControl above)
        float radius;
        if (pSpellEntry->EffectRadiusIndex[0])
            radius = Spells::GetSpellRadius(sSpellRadiusStore.LookupEntry(pSpellEntry->EffectRadiusIndex[0]));
        else if (pSpellEntry->EffectRadiusIndex[1])
            radius = Spells::GetSpellRadius(sSpellRadiusStore.LookupEntry(pSpellEntry->EffectRadiusIndex[1]));
        else if (pSpellEntry->EffectRadiusIndex[2])
            radius = Spells::GetSpellRadius(sSpellRadiusStore.LookupEntry(pSpellEntry->EffectRadiusIndex[2]));
        else
            radius = 10.0f;

        std::list<Unit*> targets;
        me->GetEnemyListInRadiusAround(pTarget, radius + AOE_IDLE_MARGIN, targets);

        // Any role, the tank included: do not open a new fight with an area spell. An idle
        // creature in the area (a neutral one too) joins the fight when it is hit: the tank's
        // Demoralizing Shout pulled two neutral Ghostly Citizens next to the pack
        // (strat-20261101-225111). The margin covers creatures that wander a little.
        for (Unit const* pUnit : targets)
            if (WouldAreaSpellPullIdle(pUnit))
                return false;

        // do not cast aoe if it will pull aggro
        // (rough rule: the victim must lead by the caster's max health. Skipped for area spells
        // the Commander aoe module judges with the spell's own threat: AreaDamageBehavior.h)
        if (m_role != ROLE_TANK && !Commander::AreaDamageBehavior::Judges(this, pSpellEntry))
        {
            for (auto const& pEnemy : targets)
            {
                if (!pTarget->IsWithinDistInMap(pEnemy, radius))
                    continue;
                if (((pEnemy->GetLevel() + 5) > me->GetLevel()) &&
                    ((pEnemy->GetHealth() * 4) > me->GetHealth()) &&
                    pEnemy->GetVictim() && pEnemy->GetVictim() != me &&
                    pEnemy->IsValidAttackTarget(me) &&
                    pEnemy->CanHaveThreatList())
                {
                    float const myThreat = pEnemy->GetThreatManager().getThreat(me);
                    float const victimThreat = pEnemy->GetThreatManager().getThreat(pEnemy->GetVictim());

                    if (victimThreat < (myThreat + me->GetMaxHealth()))
                        return false;
                }
            }
        }
    }

    return true;
}

bool TeamBotAI::WouldWakeKeptControl(Unit const* pTarget, SpellEntry const* pSpellEntry) const
{
    if (!pTarget || !pSpellEntry)
        return false;
    // Cheap test first: most casts of a tick are buffs and heals.
    if (!Commander::SpellTags::DealsDamage(pSpellEntry) && Commander::SpellTags::MeleeSplashRadius(pSpellEntry) <= 0.0f)
        return false;
    // Same map as the bot, so the commander belongs to this map thread.
    std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
    if (!commander)
        return false;
    // While the party fights, not only once the bot itself is in combat: a bot's opener before
    // its own combat flag was set skipped the rules (strat-20261114-183556).
    Commander::WorldModel const& world = commander->GetWorld();
    if (!world.InFight(me->IsInCombat()))
        return false;
    return Commander::Constraints::CastForbidden(this, world, commander->GetPlan(), pTarget, pSpellEntry,
                                                 WorldTimer::getMSTime());
}

void TeamBotAI::CancelSplashNearKeptControl()
{
    Unit* pVictim = me->GetVictim();
    if (!pVictim || !me->IsInCombat())
        return;
    std::shared_ptr<Commander::PartyCommander> commander;
    for (uint32 spellId : Commander::SpellTags::MELEE_SPLASH_SPELLS)
    {
        if (!me->HasAura(spellId))
            continue;
        SpellEntry const* pSpell = sSpellMgr.GetSpellEntry(spellId);
        if (!pSpell)
            continue;
        if (!commander)
            commander = sCommanderMgr.FindCommander(me);
        if (!commander)
            return;
        if (Commander::Constraints::CastForbidden(this, commander->GetWorld(), commander->GetPlan(), pVictim, pSpell,
                                                 WorldTimer::getMSTime()))
            me->RemoveAurasDueToSpell(spellId);
    }
}

bool TeamBotAI::WouldAreaSpellPullIdle(Unit const* pUnit) const
{
    if (pUnit->GetTypeId() != TYPEID_UNIT || !pUnit->IsAlive() || pUnit->IsInCombat())
        return false;
    Creature const* pCreature = static_cast<Creature const*>(pUnit);
    if (pCreature->IsImmuneToAoe() || pCreature->GetCreatureType() == CREATURE_TYPE_CRITTER)
        return false;
    return me->IsValidAttackTarget(pUnit);
}

bool TeamBotAI::AttackStart(Unit* pVictim)
{
    m_isBuffing = false;

    if (me->IsMounted())
        me->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);

    // Cloth casters do not swing their weapon (Unit::Attack meleeAttack = false: victim only):
    // a mage that ran to the tank kept stabbing with its dagger between casts
    // (strat-20261101-235124). Their spells work at any distance.
    // (A melee / tank role keeps melee: UpdateAI restarts the attack of those roles without it.)
    // A ranged hunter swings only in reach (HunterRotation step 0): out of reach it shoots, melee
    // off as a player's (UpdateWeaponStance; else a melee ready stance flashes at each switch).
    bool const rangedHunter = me->GetClass() == CLASS_HUNTER && GetRole() == ROLE_RANGE_DPS &&
                              !me->CanReachWithMeleeAutoAttack(pVictim);
    bool const melee = (!IsClothCaster() || GetRole() == ROLE_MELEE_DPS || GetRole() == ROLE_TANK) &&
                       !rangedHunter;
    if (me->Attack(pVictim, melee))
    {
        BeginChasing(pVictim);
        return true;
    }

    return false;
}

bool TeamBotAI::IsClothCaster() const
{
    switch (me->GetClass())
    {
        case CLASS_MAGE:
        case CLASS_WARLOCK:
        case CLASS_PRIEST:
            return true;
        default:
            return false;
    }
}

bool TeamBotAI::CanResurrectOthers(Player const* player)
{
    // (Player::AI is not const)
    if (PlayerAI const* pAI = const_cast<Player*>(player)->AI())
        if (TeamBotAI const* ai = dynamic_cast<TeamBotAI const*>(pAI))
            return ai->m_resurrectionSpell != nullptr;
    uint32 const level = player->GetLevel();
    switch (player->GetClass())
    {
        case CLASS_PRIEST:  return level >= 10;
        case CLASS_PALADIN:
        case CLASS_SHAMAN:  return level >= 12;
        default:            return false;
    }
}

namespace
{
    // Who is resurrected first (user 2026-11-15, after a wipe): one who resurrects others (two
    // casting at once), then a healer, then a tank, then the rest.
    int ResurrectRank(Player const* pMember)
    {
        int rank = TeamBotAI::CanResurrectOthers(pMember) ? 4 : 0;
        CombatBotRoles role = ROLE_INVALID;
        if (TeamBotAI const* ai = dynamic_cast<TeamBotAI const*>(const_cast<Player*>(pMember)->AI()))
            role = ai->GetRole();
        else
            role = RoleCapabilities::Assess(pMember).Best();
        if (role == ROLE_HEALER)
            rank += 2;
        else if (role == ROLE_TANK)
            rank += 1;
        return rank;
    }

    // A bot's body that keeps a self resurrection rises by itself (CorpseRun): not first.
    bool RisesByItself(Player const* pMember)
    {
        if (!dynamic_cast<TeamBotAI const*>(const_cast<Player*>(pMember)->AI()))
            return false;
#if SUPPORTED_CLIENT_BUILD >= CLIENT_BUILD_1_6_1
        return pMember->GetUInt32Value(PLAYER_SELF_RES_SPELL) != 0;
#else
        return pMember->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_CAN_SELF_RESURRECT);
#endif
    }
}

Player* TeamBotAI::SelectResurrectionTarget() const
{
    Player* pBest = nullptr;
    int bestRank = -1;
    for (Player* pMember : Commander::Team::Members(me))
    {
        if (pMember)
        {
            // Can't resurrect self.
            if (pMember == me)
                continue;

            if (pMember->GetDeathState() != CORPSE)
                continue;

            // Fix: a corpse with a pending request (waiting for the player to accept) is done;
            // returning it again recast on it and hid every other corpse behind it.
            if (pMember->IsRessurectRequested())
                continue;

            if (!me->IsWithinLOSInMap(pMember))
                continue;

            if (!m_resurrectionSpell->IsTargetInRange(me, pMember))
                continue;

            // Fix: the first in the group order came first; now ResurrectRank, and a bot that
            // rises by itself last.
            int const rank = RisesByItself(pMember) ? 0 : ResurrectRank(pMember) + 1;
            if (rank > bestRank)
            {
                pBest = pMember;
                bestRank = rank;
            }
        }
    }

    return pBest;
}

Corpse* TeamBotAI::SelectReleasedCorpse() const
{
    Corpse* pBest = nullptr;
    int bestRank = -1;
    for (Player* pMember : Commander::Team::Members(me))
    {
        if (!pMember || pMember == me || pMember->GetDeathState() != DEAD || pMember->IsRessurectRequested())
            continue;

        // The ghost may be on another map (its graveyard); the corpse must be on this one.
        Corpse* pCorpse = sObjectAccessor.GetCorpseForPlayerGUID(pMember->GetObjectGuid());
        if (!pCorpse || pCorpse->GetType() == CORPSE_BONES ||
            me->GetMap()->GetCorpse(pCorpse->GetObjectGuid()) != pCorpse)
            continue;

        // (as Spell::CheckCast for a corpse target)
        if (!pCorpse->IsWithinLOSInMap(me))
            continue;

        if (!m_resurrectionSpell->IsTargetInRange(me, pCorpse))
            continue;
        int const rank = ResurrectRank(pMember);     // (as SelectResurrectionTarget)
        if (rank > bestRank)
        {
            pBest = pCorpse;
            bestRank = rank;
        }
    }

    return pBest;
}

bool TeamBotAI::CastResurrection(SpellEntry const* pSpell, ObjectGuid deadGuid)
{
    Player* pDead = pSpell ? sObjectAccessor.FindPlayer(deadGuid) : nullptr;
    if (!pDead || pDead == me || pDead->IsAlive() || pDead->IsRessurectRequested())
        return false;
    if (pDead->GetDeathState() == CORPSE)
    {
        if (pDead->GetMap() != me->GetMap() || !me->IsWithinLOSInMap(pDead) || !pSpell->IsTargetInRange(me, pDead) ||
            !CanTryToCastSpell(pDead, pSpell))
            return false;
        return DoCastSpell(pDead, pSpell) == SPELL_CAST_OK;
    }
    // Released: its corpse (as SelectReleasedCorpse).
    Corpse* pCorpse = sObjectAccessor.GetCorpseForPlayerGUID(deadGuid);
    if (!pCorpse || pCorpse->GetType() == CORPSE_BONES || me->GetMap()->GetCorpse(pCorpse->GetObjectGuid()) != pCorpse ||
        !pCorpse->IsWithinLOSInMap(me) || !pSpell->IsTargetInRange(me, pCorpse))
        return false;
    return CastResurrectionAtCorpse(pCorpse, pSpell);
}

bool TeamBotAI::CastResurrectionAtCorpse(Corpse* pCorpse, SpellEntry const* pResurrection)
{
    SpellEntry const* pSpellEntry = pResurrection ? pResurrection : m_resurrectionSpell;
    if (m_preventCasting || !pSpellEntry || !me->IsSpellReady(pSpellEntry) || me->HasGCD(pSpellEntry))
        return false;
    if (me->GetPower(Powers(pSpellEntry->powerType)) < Spell::CalculatePowerCost(pSpellEntry, me))
        return false;
    if (pSpellEntry->GetErrorAtShapeshiftedCast(me->GetShapeshiftForm()) != SPELL_CAST_OK)
        return false;

    if (!me->IsStopped())
        me->StopMoving();
    // FaceTo, not Unit::SetFacingTo: no spline packet to reset the cast animation (DoCastSpell).
    FaceTo(me->GetAngle(pCorpse));
    if (me->IsMounted())
        me->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);

    // SpellCaster::CastSpell takes only a unit or object target: the corpse target as a
    // client sends it (Spell::FillTargetMap resurrects the corpse's owner).
    Spell* pSpell = new Spell(me, pSpellEntry, false);
    SpellCastTargets targets;
    targets.setCorpseTarget(pCorpse);
    targets.m_targetMask = TARGET_FLAG_CORPSE_ALLY;
    return pSpell->prepare(std::move(targets)) == SPELL_CAST_OK;
}

Player* TeamBotAI::SelectShieldTarget() const
{

    // Until 2026-11-01: any attacked member below 90%. A shield costs more mana per point than
    // a heal and the absorbed damage gives a warrior no rage, so it is kept for members who
    // may die before a heal lands (HealSpells::IsInDanger). The lowest health first.
    Player* pBest = nullptr;
    for (Player* pMember : Commander::Team::Members(me))
    {
        if (pMember)
        {
            // We already checked self.
            if (pMember == me)
                continue;

            if (pMember->IsImmuneToMechanic(MECHANIC_SHIELD) || !IsValidHealTarget(pMember))
                continue;
            if (pBest && pBest->GetHealthPercent() <= pMember->GetHealthPercent())
                continue;
            if (IsInHealDanger(pMember))
                pBest = pMember;
        }
    }

    return pBest;
}

void TeamBotAI::UpdateWeaponStance()
{
    if (me->IsMounted())
        return;

    // Fix: only the client sets the sheath (CMSG_SETSHEATHED, WorldSession::HandleSetSheathedOpcode):
    // it draws the ranged weapon when a player starts Auto Shot or Shoot, the melee weapons when
    // it starts a melee attack. A bot has no client, so it kept its melee weapons in hand while
    // shooting, the gun showing only for the moment of each shot.
    bool const shooting = me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL) &&
                          me->GetWeaponForAttack(RANGED_ATTACK, true, false);

    // Fix: a player's melee attack is off while he shoots from range. The hunter's stayed on
    // (HunterRotation step 0, TeamBotAI::AttackStart), and the observers' client showed the
    // melee ready stance (SMSG_ATTACKSTART) with the gun hanging in hand, the aimed gun only
    // after the fight's SMSG_ATTACKSTOP. Melee off, the victim kept (Unit::Attack meleeAttack
    // = false); it comes back when the victim is in reach.
    Unit* pVictim = me->GetVictim();
    if (shooting && pVictim && me->HasUnitState(UNIT_STATE_MELEE_ATTACKING) &&
        !me->CanReachWithMeleeAutoAttack(pVictim))
    {
        me->ClearUnitState(UNIT_STATE_MELEE_ATTACKING);
        me->SendMeleeAttackStop(pVictim);
    }

    SheathState const sheath = me->GetSheath();
    if (shooting)
    {
        if (sheath != SHEATH_STATE_RANGED)
            me->SetSheath(SHEATH_STATE_RANGED);
    }
    // The ranged weapon goes back for a melee attack, and after the fight (the bot's default:
    // melee weapons in hand).
    else if (sheath == SHEATH_STATE_UNARMED ||
             (sheath == SHEATH_STATE_RANGED &&
              (me->HasUnitState(UNIT_STATE_MELEE_ATTACKING) || !me->IsInCombat())))
        me->SetSheath(SHEATH_STATE_MELEE);
}

void TeamBotAI::KeepFacing(Unit* pVictim)
{
    // A walk (chase, backpedal, jump turn) faces by itself; a controlled bot cannot turn.
    if (me->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL) || me->IsMoving() ||
        IsBackpedaling() || IsJumpCasting())
        return;

    // A cast in progress or an auto-repeat shot on a unit, whose spell checks facing (Frostbolt,
    // Auto Shot, Shoot: CombatBotBaseAI::FaceTo): the server checks again when the cast ends
    // and before every shot (Spell::cast, Unit::UpdateAutoRepeatSpell). An enemy walking
    // around the standing caster made the cast fail, and the auto-repeat stop.
    for (CurrentSpellTypes slot : { CURRENT_GENERIC_SPELL, CURRENT_AUTOREPEAT_SPELL })
    {
        Spell const* spell = me->GetCurrentSpell(slot);
        if (!spell || !spell->m_spellInfo || !spell->m_spellInfo->IsNeedFaceTarget() ||
            (slot == CURRENT_GENERIC_SPELL && spell->getState() != SPELL_STATE_PREPARING))
            continue;
        Unit const* target = spell->m_targets.getUnitTarget();
        if (target && target != me && target->IsAlive() && !me->IsFacingTarget(target))
        {
            FaceTarget(target);
            return;
        }
    }

    // Fix: only the chase movement turned a melee bot to its victim. Since the stance modules
    // hold it on a spot (TankStanceBehavior: "it comes to the tank", already in reach), it kept
    // the facing it had: the guardian it faced died, the cadaver hitting it from the side got no
    // swing for 8 s (bad facing), no rage, no ability, and Dazed from behind
    // (strat-20261114-173302). Turn as a player does.
    if (!pVictim || !pVictim->IsAlive() || !me->HasUnitState(UNIT_STATE_MELEE_ATTACKING) ||
        !me->CanReachWithMeleeAutoAttack(pVictim) ||
        me->HasInArc(pVictim, 2 * M_PI_F / 3))     // Unit::CanAutoAttackTarget's facing arc
        return;
    FaceTarget(pVictim);
}


void TeamBotAI::UpdateOutOfCombatAI()
{
    if (m_resurrectionSpell)
    {
        if (Player* pTarget = SelectResurrectionTarget())
        {
            if (CanTryToCastSpell(pTarget, m_resurrectionSpell))
                if (DoCastSpell(pTarget, m_resurrectionSpell) == SPELL_CAST_OK)
                    return;
        }
        else if (Corpse* pCorpse = SelectReleasedCorpse())
        {
            if (CastResurrectionAtCorpse(pCorpse))
                return;
        }
    }

    if (DispelOutOfCombat())
        return;

    if (ClassRotation* pRotation = Rotation())
        pRotation->UpdateOutOfCombat();
}

void TeamBotAI::UpdateInCombatAI()
{
    // (targets, taunts and crowd control: the Commander)
    // (dispels in combat: the Commander, DispelAssigner / DispelBehavior)
    if (ClassRotation* pRotation = Rotation())
        pRotation->UpdateInCombat();

    if (me->GetVictim())
        UseTrinketEffects();
}

bool TeamBotAI::DispelOutOfCombat()
{
    if (me->GetShapeshiftForm() != FORM_NONE)
        return false;

    for (SpellEntry const* pDispelSpell : Kit().Dispels())
    {
        if (!pDispelSpell)
            continue;
        if (Unit* pFriend = SelectDispelTarget(pDispelSpell))
            if (CanTryToCastSpell(pFriend, pDispelSpell) && DoCastSpell(pFriend, pDispelSpell) == SPELL_CAST_OK)
                return true;
    }
    return false;
}

namespace
{
    // Shapeshift form a warrior stance spell puts the caster in; FORM_NONE if not a stance.
    ShapeshiftForm StanceForm(SpellEntry const* pStance)
    {
        for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (pStance->Effect[i] == SPELL_EFFECT_APPLY_AURA && pStance->EffectApplyAuraName[i] == SPELL_AURA_MOD_SHAPESHIFT)
                return ShapeshiftForm(pStance->EffectMiscValue[i]);
        return FORM_NONE;
    }

    // Warrior threat by stance (vanilla): Defensive Stance x1.3, Battle and Berserker x0.8.
    float StanceThreat(ShapeshiftForm form)
    {
        switch (form)
        {
            case FORM_DEFENSIVESTANCE:
                return 1.3f;
            case FORM_BATTLESTANCE:
            case FORM_BERSERKERSTANCE:
                return 0.8f;
            default:
                return 1.0f;
        }
    }
}

uint32 TeamBotAI::RageKeptOnStanceSwap() const
{
    // Tactical Mastery ranks 1-5 are class scripts 831-835: 5 rage (50 power) per rank.
    for (auto const& aura : me->GetAurasByType(SPELL_AURA_OVERRIDE_CLASS_SCRIPTS))
    {
        int32 const script = aura->GetModifier()->m_miscvalue;
        if (script >= 831 && script <= 835)
            return uint32(script - 830) * 50;
    }
    return 0;
}

SpellEntry const* TeamBotAI::StanceSwapFor(SpellEntry const* pSpellEntry) const
{
    if (me->GetClass() != CLASS_WARRIOR)
        return nullptr;
    ShapeshiftForm const current = me->GetShapeshiftForm();
    if (pSpellEntry->GetErrorAtShapeshiftedCast(current) == SPELL_CAST_OK)
        return nullptr;

    // The tank's home stance first: no swap back needed afterwards.
    SpellEntry const* const stances[] = { m_spells.warrior.pDefensiveStance, m_spells.warrior.pBattleStance,
                                          m_spells.warrior.pBerserkerStance };
    for (SpellEntry const* pStance : stances)
    {
        if (!pStance)
            continue;
        ShapeshiftForm const form = StanceForm(pStance);
        if (form == FORM_NONE || form == current || pSpellEntry->GetErrorAtShapeshiftedCast(form) != SPELL_CAST_OK)
            continue;
        if (CombatBotBaseAI::CanTryToCastSpell(me, pStance))   // 1 s stance cooldown
            return pStance;
    }
    return nullptr;
}

bool TeamBotAI::CanCastWithStance(Unit const* pTarget, SpellEntry const* pSpellEntry) const
{
    if (CanTryToCastSpell(pTarget, pSpellEntry))
        return true;
    if (m_stanceSwap.spell)
        return false;   // one swap at a time
    SpellEntry const* pStance = StanceSwapFor(pSpellEntry);
    if (!pStance)
        return false;

    uint32 const rage = me->GetPower(POWER_RAGE);
    uint32 const kept = std::min(rage, RageKeptOnStanceSwap());
    if (rage - kept > PB_STANCE_SWAP_MAX_RAGE_LOSS)
        return false;
    uint32 const power = Powers(pSpellEntry->powerType) == POWER_RAGE ? kept : me->GetPower(Powers(pSpellEntry->powerType));
    return CanTryToCastSpellAs(pTarget, pSpellEntry, StanceForm(pStance), power) &&
           PassesPartyCastChecks(pTarget, pSpellEntry);
}

bool TeamBotAI::IsReadyWithStance(SpellEntry const* pSpellEntry) const
{
    if (!me->IsSpellReady(pSpellEntry))
        return false;
    Powers const powerType = Powers(pSpellEntry->powerType);
    uint32 const cost = uint32(Spell::CalculatePowerCost(pSpellEntry, me));
    if (pSpellEntry->GetErrorAtShapeshiftedCast(me->GetShapeshiftForm()) == SPELL_CAST_OK)
        return powerType == POWER_HEALTH || me->GetPower(powerType) >= cost;
    if (m_stanceSwap.spell || !StanceSwapFor(pSpellEntry))
        return false;
    uint32 const rage = me->GetPower(POWER_RAGE);
    uint32 const kept = std::min(rage, RageKeptOnStanceSwap());
    if (rage - kept > PB_STANCE_SWAP_MAX_RAGE_LOSS)
        return false;
    uint32 const power = powerType == POWER_RAGE ? kept : me->GetPower(powerType);
    return power >= cost;
}

void TeamBotAI::StandStillFor(SpellEntry const* pSpellEntry)
{
    if (!Commander::CurrentAction::NeedsStandingStill(this, pSpellEntry))
        return;
    if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
        return;     // chase / follow pause by themselves while the cast runs
    ClearMovementHold();
    if (!me->IsStopped())
        me->StopMoving();
    me->GetMotionMaster()->Clear(false, true);
    me->GetMotionMaster()->MoveIdle();
}

bool TeamBotAI::CanTryToCastSpellAt(float x, float y, float z, SpellEntry const* pSpellEntry) const
{
    if (!pSpellEntry)
        return false;
    uint32 const power = pSpellEntry->powerType == POWER_HEALTH ? 0 : me->GetPower(Powers(pSpellEntry->powerType));
    if (!CanTryToCastSpellAs(me, pSpellEntry, me->GetShapeshiftForm(), power))
        return false;
    if (me->GetDistance(x, y, z, SizeFactor::None) > SpellMaxRange(me, pSpellEntry))
        return false;
    if (!me->IsWithinLOS(x, y, z))
        return false;
    if (IsSpellHeld(pSpellEntry))
        return false;
    if (!me->IsInCombat())
        return true;
    std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
    Commander::Position3 const at{ x, y, z };
    return !commander || !Commander::Constraints::CastAtForbidden(this, commander->GetWorld(), commander->GetPlan(),
                                                                  at, pSpellEntry, WorldTimer::getMSTime());
}

SpellCastResult TeamBotAI::DoCastAt(float x, float y, float z, SpellEntry const* pSpellEntry)
{
    StandStillFor(pSpellEntry);
    return DoCastSpellAt(x, y, z, pSpellEntry);
}

SpellCastResult TeamBotAI::DoCastFacing(float orientation, SpellEntry const* pSpellEntry)
{
    if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
    {
        ClearMovementHold();
        me->GetMotionMaster()->Clear(false, true);
        me->GetMotionMaster()->MoveIdle();
    }
    return CombatBotBaseAI::DoCastFacing(orientation, pSpellEntry);
}

SpellCastResult TeamBotAI::DoCastWithStance(Unit* pTarget, SpellEntry const* pSpellEntry)
{
    if (CanTryToCastSpell(pTarget, pSpellEntry))
    {
        StandStillFor(pSpellEntry);
        return DoCastSpell(pTarget, pSpellEntry);
    }
    if (!CanCastWithStance(pTarget, pSpellEntry))
        return SPELL_FAILED_ONLY_SHAPESHIFT;

    SpellEntry const* pStance = StanceSwapFor(pSpellEntry);
    SpellCastResult const result = DoCastSpell(me, pStance);
    if (result == SPELL_CAST_OK)
    {
        m_stanceSwap.spell = pSpellEntry;
        m_stanceSwap.target = pTarget && pTarget != me ? pTarget->GetObjectGuid() : ObjectGuid();
        m_stanceSwap.startMs = WorldTimer::getMSTime();
    }
    return result;
}

bool TeamBotAI::ContinueStanceSwap()
{
    SpellEntry const* pSpellEntry = m_stanceSwap.spell;
    if (!pSpellEntry)
        return false;

    Unit* pTarget = m_stanceSwap.target.IsEmpty() ? me : me->GetMap()->GetUnit(m_stanceSwap.target);
    if (!me->IsAlive() || !pTarget || !pTarget->IsAlive() ||
        WorldTimer::getMSTimeDiff(m_stanceSwap.startMs, WorldTimer::getMSTime()) > PB_STANCE_SWAP_TIMEOUT_MS)
    {
        m_stanceSwap = StanceSwap();
        return false;
    }

    // The stance spell has not gone off yet (non-triggered spells run on the next spell update).
    if (pSpellEntry->GetErrorAtShapeshiftedCast(me->GetShapeshiftForm()) != SPELL_CAST_OK)
        return true;

    m_stanceSwap = StanceSwap();
    if (CanTryToCastSpell(pTarget, pSpellEntry))
    {
        StandStillFor(pSpellEntry);
        return DoCastSpell(pTarget, pSpellEntry) == SPELL_CAST_OK;
    }
    return false;
}

float TeamBotAI::StanceSwapThreatFactor(SpellEntry const* pSpellEntry) const
{
    SpellEntry const* pStance = StanceSwapFor(pSpellEntry);
    if (!pStance)
        return 1.0f;
    return StanceThreat(StanceForm(pStance)) / StanceThreat(me->GetShapeshiftForm());
}
