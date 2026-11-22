#include "Commander/Perception/WorldModelBuilder.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/PackRoutes.h"
#include "Commander/Perception/Profiles/SpellTags.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Forecast/DamageIntake.h"
#include "Commander/Perception/Forecast/DamageDealt.h"
#include "Commander/Perception/Spatial/PatrolTracker.h"
#include "Commander/Perception/Profiles/CreatureProfile.h"
#include "Commander/Perception/Forecast/HealCapacity.h"
#include "Commander/Perception/Profiles/PetTaunt.h"
#include "Commander/Perception/Profiles/PetAbilities.h"
#include "Commander/Perception/Profiles/PaladinBuffs.h"
#include "Commander/Perception/Profiles/WarlockCurses.h"
#include "Commander/Perception/Profiles/ShamanTotems.h"
#include "Commander/Perception/Values/RescueValue.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Coordination/TeamPlan.h"    // CoveragePlan (the last RoleAssigner result)
#include "Commander/Classes/ClassKit.h"
#include "Commander/Classes/RaceKit.h"
#include "Commander/Classes/ItemKit.h"
#include "DamageSpells.h"
#include "HealSpells.h"
#include "RoleCapability.h"
#include "World.h"
#include "TeamBotAI.h"
#include "DBCStores.h"
#include "Creature.h"
#include "CreatureGroups.h"
#include "Group.h"
#include "Commander/Team.h"
#include "Pet.h"
#include "TemporarySummon.h"
#include "Player.h"
#include "Corpse.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "SpellMgr.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "ThreatManager.h"
#include "HostileRefManager.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "Timer.h"
#include "Commander/BotDirectives.h"
#include "MoveSpline.h"
#include <algorithm>
#include <limits>

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

        uint32 const SPELL_RIGHTEOUS_FURY = 25780;     // the aura (spell_template: aura 42 / 10)
        uint32 const SPELL_POWER_INFUSION = 10060;
        uint32 const SPELL_ARCANE_POWER = 12042;

        // PartyMemberFacts::healThreat: the biggest heal of `rates` (hps x time per cast) as
        // threat (Spell.cpp heal effect: x 0.5, paladins 0.25; then the healer's threat modifiers).
        float HealThreat(Player* healer, std::vector<HealRate> const& rates)
        {
            float heal = 0.0f;
            for (HealRate const& r : rates)
                heal = std::max(heal, r.hps * std::max(r.castSec, Value::GCD_SEC));
            float const classMod = healer->GetClass() == CLASS_PALADIN ? 0.25f : 0.5f;
            return healer->ApplyTotalThreatModifier(heal * classMod, SPELL_SCHOOL_MASK_HOLY);
        }

        // An ability's reach (centre distance minus the target's bounding radius): COMBAT_RANGE
        // for combat range spells, else the DBC range with the caster's range talents
        // (CombatBotBaseAI::SpellMaxRange), at least `floor`.
        float AbilityRange(Unit const* caster, SpellEntry const* spell, float floor)
        {
            if (spell->rangeIndex == SPELL_RANGE_IDX_COMBAT)
                return COMBAT_RANGE;
            return std::max(CombatBotBaseAI::SpellMaxRange(caster, spell), floor);
        }

        void FillUnitFacts(Unit const* unit, PartyMemberFacts& f)
        {
            f.guid = unit->GetObjectGuid();
            f.alive = unit->IsAlive();
            f.classId = unit->GetClass();
            f.healthPct = unit->GetHealthPercent();
            f.health = unit->GetHealth();
            f.maxHealth = unit->GetMaxHealth();
            f.level = unit->GetLevel();
            f.armor = uint32(std::max(0, unit->GetArmor()));
            if (Player const* player = unit->ToPlayer())
            {
                f.dodgePct = player->GetFloatValue(PLAYER_DODGE_PERCENTAGE);
                f.parryPct = player->CanParry() && player->GetWeaponForParry()
                    ? player->GetFloatValue(PLAYER_PARRY_PERCENTAGE) : 0.0f;
            }
            else
            {
                f.dodgePct = std::max(0.0f, 5.0f + float(unit->GetTotalAuraModifier(SPELL_AURA_MOD_DODGE_PERCENT)));
                f.parryPct = std::max(0.0f, 5.0f + float(unit->GetTotalAuraModifier(SPELL_AURA_MOD_PARRY_PERCENT)));
            }
            f.defenseSkill = unit->GetDefenseSkillValue();
            f.manaPct = unit->GetPowerType() == POWER_MANA && unit->GetMaxPower(POWER_MANA)
                ? unit->GetPowerPercent(POWER_MANA) : -1.0f;
            f.mana = unit->GetPower(POWER_MANA);
            f.maxMana = unit->GetMaxPower(POWER_MANA);
            f.pos = PositionOf(unit);
            f.standAt = f.pos;
            if (unit->IsMoving() && unit->movespline && !unit->movespline->Finalized())
            {
                Vector3 const end = unit->movespline->FinalDestination();
                f.standAt.x = end.x;
                f.standAt.y = end.y;
                f.standAt.z = end.z;
            }
            if (Unit const* victim = unit->GetVictim())
                f.victim = victim->GetObjectGuid();
            f.casting = unit->IsNonMeleeSpellCasted(false, false, true);
            for (CurrentSpellTypes type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
                if (Spell const* spell = unit->GetCurrentSpell(type))
                    if (spell->getState() != SPELL_STATE_FINISHED)
                        f.castRemainingMs = std::max(f.castRemainingMs, uint32(std::max(0, int32(spell->GetCastedTime()))));
            if (Player const* player = unit->ToPlayer())
            {
                f.comboPoints = player->GetComboPoints();
                f.comboTarget = player->GetComboTargetGuid();
            }
            f.resting = unit->GetStandState() != UNIT_STAND_STATE_STAND ||
                        unit->HasAuraType(SPELL_AURA_MOD_REGEN) || unit->HasAuraType(SPELL_AURA_MOD_POWER_REGEN);
            f.inCombat = unit->IsInCombat();
            // Charmed by a creature (mind control): hostile to the party, acts for the enemy.
            f.charmed = unit->IsCharmed() && !unit->GetCharmerGuid().IsPlayer();
            if (f.charmed)
            {
                f.charmer = unit->GetCharmerGuid();
                for (AuraType type : { SPELL_AURA_MOD_CHARM, SPELL_AURA_MOD_POSSESS })
                    for (Aura const* aura : unit->GetAurasByType(type))
                        if (aura->GetAuraDuration() > 0)
                            f.charmLeftMs = std::max(f.charmLeftMs, uint32(aura->GetAuraDuration()));
            }
            f.controlled = f.charmed || unit->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL);
            f.silenced = unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SILENCED);
            ShapeshiftForm const form = unit->GetShapeshiftForm();
            f.tankStance = form == FORM_DEFENSIVESTANCE || form == FORM_BEAR || form == FORM_DIREBEAR ||
                           (f.classId == CLASS_PALADIN && unit->HasAura(SPELL_RIGHTEOUS_FURY));

            // Team rescue (Perception/Values/SurvivalValue.h): what still protects it.
            for (Aura const* aura : unit->GetAurasByType(SPELL_AURA_SCHOOL_ABSORB))
                f.absorb += std::max(0.0f, aura->GetModifier()->m_amount);
            for (Aura const* aura : unit->GetAurasByType(SPELL_AURA_MANA_SHIELD))
            {
                float amount = std::max(0.0f, aura->GetModifier()->m_amount);
                if (float const perPoint = aura->GetSpellProto()->EffectMultipleValue[aura->GetEffIndex()])
                    amount = std::min(amount, float(unit->GetPower(POWER_MANA)) / perPoint);
                f.absorb += amount;
            }
            for (Aura const* aura : unit->GetAurasByType(SPELL_AURA_PERIODIC_HEAL))
            {
                if (aura->GetModifier()->periodictime <= 0 || aura->GetAuraDuration() <= 0)
                    continue;
                PartyHot h;
                h.caster = aura->GetCasterGuid();
                h.spellId = aura->GetId();
                h.perTick = std::max(0.0f, float(aura->GetModifier()->m_amount));
                h.periodMs = uint32(aura->GetModifier()->periodictime);
                h.nextTickMs = uint32(std::max(0, aura->GetAuraPeriodicTimer()));
                h.remainingMs = uint32(aura->GetAuraDuration());
                f.hots.push_back(h);
            }
            f.damageTakenMult = unit->GetTotalAuraMultiplier(SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN);
            f.immunePhysical = unit->IsImmuneToDamage(SPELL_SCHOOL_MASK_NORMAL);
            f.immuneMagic = unit->IsImmuneToDamage(SpellSchoolMask(SPELL_SCHOOL_MASK_MAGIC));
            for (uint32 mechanic : { uint32(MECHANIC_SHIELD), uint32(MECHANIC_IMMUNE_SHIELD) })
                if (unit->IsImmuneToMechanic(Mechanics(mechanic)))
                    f.immuneMechanics |= 1u << mechanic;
            // Ally boosts (BoostAssigner): what is up already.
            f.manaBoosted = unit->HasAuraType(SPELL_AURA_MOD_POWER_REGEN_PERCENT);
            f.powerBoosted = unit->HasAura(SPELL_POWER_INFUSION) || unit->HasAura(SPELL_ARCANE_POWER);

            DamageIntake::Stats const taken = DamageIntake::Query(f.guid, WorldTimer::getMSTime());
            f.takenDps = taken.dps;
            f.takenBurst = taken.burst;
            if (Player const* player = unit->ToPlayer())
            {
                float const rate = sWorld.getConfig(CONFIG_FLOAT_RATE_POWER_MANA);
                f.manaRegen = player->GetManaRegenPerSec(false) * rate;
                f.manaRegenCasting = player->GetManaRegenPerSec(true) * rate;
                f.fiveSecondRuleMs = player->GetLastManaUseTimer();
            }
        }

        // Players: the role they chose, else from talents and gear. Tank only while no bot is a
        // tank: a warrior leveling with a shield scores as tank-capable, but with a tank bot in
        // the party he is not tanking, and counting him as a tank would stop the bot from taking
        // enemies off him (Tanks.h).
        // Flight time of `spell` from `caster` to `target` (Spell.cpp: distance / speed, at least 5 yd).
        uint32 FlightMs(Unit const* caster, Unit const* target, SpellEntry const* spell)
        {
            if (spell->speed <= 0.0f || caster == target)
                return 0;
            return uint32(std::max(5.0f, caster->GetDistance(target)) / spell->speed * 1000.0f);
        }

        // PartyMemberFacts::pendingHit: a direct damage cast in progress (not a channel), or
        // its bolt in the air (the generic slot in SPELL_STATE_DELAYED).
        void FillPendingHit(Unit* unit, PartyMemberFacts& f)
        {
            Spell const* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            if (!spell || !spell->m_spellInfo || !SpellTags::DealsDamage(spell->m_spellInfo) ||
                spell->m_spellInfo->IsAreaOfEffectSpell())
                return;
            Unit const* target = spell->m_targets.getUnitTarget();
            if (!target || target == unit)
                return;
            SpellEntry const* info = spell->m_spellInfo;
            uint32 landMs;
            if (spell->getState() == SPELL_STATE_PREPARING && spell->GetCastTime() > 0)
                landMs = uint32(std::max(0, int32(spell->GetCastedTime()))) + FlightMs(unit, target, info);
            else if (spell->getState() == SPELL_STATE_DELAYED)
            {
                // The delay is counted on the caster's event clock from GetDelayStart (0 = just started).
                uint64 const end = spell->GetDelayStart() + spell->GetDelayMoment();
                uint64 const now = unit->m_Events.CalculateTime(0);
                landMs = !spell->GetDelayStart() ? uint32(spell->GetDelayMoment())
                                                 : (end > now ? uint32(end - now) : 0);
            }
            else
                return;
            f.pendingHit.target = target->GetObjectGuid();
            f.pendingHit.spellId = info->Id;
            f.pendingHit.landMs = landMs;
            f.pendingHit.damage = DamageSpells::EstimateDirect(unit, target, info);
            f.pendingHit.threat = unit->ApplyTotalThreatModifier(f.pendingHit.damage * sSpellMgr.GetSpellThreatMultiplier(info),
                                                                 info->GetSpellSchoolMask());
        }

        // PartyMemberFacts::pendingHeal: a single target direct heal with a cast time in
        // progress (strat-20261115-173647: the tank used Shield Wall 0.6 s before a Greater Heal
        // for 3800 landed on him, the heal in progress counted as a delay only).
        void FillPendingHeal(Unit* unit, PartyMemberFacts& f)
        {
            Spell const* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            if (!spell || !spell->m_spellInfo || spell->getState() != SPELL_STATE_PREPARING ||
                spell->GetCastTime() <= 0 || !HealSpells::IsSingleTargetHeal(spell->m_spellInfo))
                return;
            Unit const* target = spell->m_targets.getUnitTarget();
            if (!target)
                return;
            float const amount = HealSpells::Estimate(unit, target, spell->m_spellInfo).direct;
            if (amount <= 0.0f)
                return;
            f.pendingHeal.target = target->GetObjectGuid();
            f.pendingHeal.spellId = spell->m_spellInfo->Id;
            f.pendingHeal.landMs = uint32(std::max(0, int32(spell->GetCastedTime())));
            f.pendingHeal.amount = amount;
        }

        void FillPlayerRoles(WorldModel& world)
        {
            bool botTank = false;
            for (PartyMemberFacts const& m : world.party)
                if (m.isBot && !m.isPet && m.role == ROLE_TANK)
                    botTank = true;
            for (PartyMemberFacts& m : world.party)
            {
                if (m.isBot || m.isPet)
                    continue;
                uint8 const chosen = sCommanderMgr.GetPlayerRole(m.guid);
                m.roleLocked = chosen != ROLE_INVALID;
                if (m.roleLocked)
                    m.role = chosen;
                else
                {
                    CombatBotRoles const best = m.cap.Best();
                    m.role = uint8((best == ROLE_TANK && botTank) ? m.cap.BestNonTank() : best);
                }
                m.functions = RoleFunctions(m.role);     // nothing is added to players
            }
        }

        // PartyMemberFacts::functions of a bot: its role now plus what the coverage added.
        uint8 BotFunctions(PartyMemberFacts const& f, CoveragePlan const& coverage)
        {
            MemberCover const* c = coverage.Find(f.guid);
            return RoleFunctions(f.role) | (c ? c->added : 0);
        }

        // Interrupt-cast effects lock the school for the spell's duration; stuns and silences don't.
        uint32 LockoutMs(SpellEntry const* spell)
        {
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (spell->Effect[i] == SPELL_EFFECT_INTERRUPT_CAST)
                    return uint32(std::max(0, spell->GetDuration()));
            return 0;
        }
    }

    float RangedSpellDps(CreatureProfile const& profile, uint32 level, float meleeHit)
    {
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(profile.mainRangedSpellId);
        std::shared_ptr<SpellProfile const> sp = sProfileStore.GetSpell(profile.mainRangedSpellId);
        CreatureSpellRef const* ref = profile.FindSpell(profile.mainRangedSpellId);
        if (!spell || !sp || !ref)
            return 0.0f;
        float const damage = SpellProfiles::Estimate(spell, level, meleeHit).TotalDamage();
        // The repeat delay runs from the start of the cast; the spell list is checked every
        // 1.2 s (CreatureAI.cpp CREATURE_CASTING_DELAY), so half of that is lost on average.
        float const castSec = sp->castMs / 1000.0f + CREATURE_SPELL_CHECK_SEC / 2.0f;
        float const repeatSec = (ref->repeatMinMs + ref->repeatMaxMs) / 2000.0f;
        float const cycle = std::max(castSec, repeatSec);
        return cycle > 0.0f ? damage / cycle : 0.0f;
    }

    namespace
    {
        bool IgnoresAssistThreat(Unit const* enemy)
        {
            // ThreatManager::addThreat zeroes assist (heal/buff) threat in these states
            return enemy->HasUnitState(UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING | UNIT_STATE_ISOLATED) ||
                  (enemy->HasUnitState(UNIT_STATE_STUNNED) && enemy->HasBreakableByDamageAuraType(SPELL_AURA_MOD_STUN, 0));
        }

        void FillCast(Unit const* unit, EnemyFacts& e)
        {
            Spell const* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            bool channeled = false;
            if (!spell || spell->getState() == SPELL_STATE_FINISHED)
            {
                spell = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
                channeled = true;
            }
            if (!spell || spell->getState() == SPELL_STATE_FINISHED)
                return;

            SpellEntry const* info = spell->m_spellInfo;
            e.castingSpellId = info->Id;
            e.castTarget = spell->m_targets.getUnitTargetGuid();
            e.castChanneled = channeled;
            e.castCategory = uint8(SpellTags::Categorize(info));
            e.castInterruptible = SpellTags::IsInterruptible(info, channeled, spell->GetCastTime());
            e.castRemainingMs = spell->GetCastedTime();     // m_timer counts down to 0
            e.castTotalMs = channeled ? uint32(std::max(0, info->GetDuration())) : uint32(std::max(0, spell->GetCastTime()));
        }

        // Swing timers, weapon damage and the live creature_spells timers (IncomingTimeline.h).
        void FillTimers(Creature const* creature, EnemyFacts& e)
        {
            e.swingInMs = creature->GetAttackTimer(BASE_ATTACK);
            e.swingPeriodMs = creature->GetAttackTime(BASE_ATTACK);
            e.meleeMin = creature->GetFloatValue(UNIT_FIELD_MINDAMAGE);
            e.meleeMax = creature->GetFloatValue(UNIT_FIELD_MAXDAMAGE);
            if (creature->HaveOffhandWeapon())
            {
                e.offSwingInMs = creature->GetAttackTimer(OFF_ATTACK);
                e.offSwingPeriodMs = creature->GetAttackTime(OFF_ATTACK);
                e.offMin = creature->GetFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE);
                e.offMax = creature->GetFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE);
            }
            e.canCrush = !creature->IsPet() && !creature->HasStaticFlag(CREATURE_STATIC_FLAG_2_NO_CRUSHING_BLOWS);
            e.alwaysCrush = e.canCrush && creature->HasExtraFlag(CREATURE_FLAG_EXTRA_ALWAYS_CRUSH);

            CreatureAI const* ai = creature->AI();
            if (!ai || !ai->SpellsListRuns())
                return;
            e.spellCheckInMs = ai->GetSpellsListCheckDelay();
            for (CreatureAISpellsEntry const& s : ai->GetSpellsListTimers())
            {
                ListSpellFacts f;
                f.spellId = s.spellId;
                f.cooldownMs = s.cooldown;
                f.repeatMinMs = s.delayRepeatMin;
                f.repeatMaxMs = s.delayRepeatMax;
                f.probability = s.probability;
                f.castTarget = s.castTarget;
                f.castFlags = s.castFlags;
                e.spellList.push_back(f);
            }
        }

        // The melee boost of a spell's auras on its caster (EnrageFacts), spellId 0 = none.
        EnrageFacts EnrageOf(uint32 spellId)
        {
            EnrageFacts f;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
            if (!spell)
                return f;
            bool boosts = false;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            {
                if (spell->Effect[i] != SPELL_EFFECT_APPLY_AURA || spell->EffectImplicitTargetA[i] != TARGET_UNIT_CASTER)
                    continue;
                float const amount = float(spell->CalculateSimpleValue(SpellEffectIndex(i)));
                bool const physical = (spell->EffectMiscValue[i] & SPELL_SCHOOL_MASK_NORMAL) != 0;
                switch (spell->EffectApplyAuraName[i])
                {
                    case SPELL_AURA_MOD_MELEE_HASTE:
                        if (amount > 0.0f)
                        {
                            f.periodMult /= 1.0f + amount / 100.0f;
                            boosts = true;
                        }
                        break;
                    case SPELL_AURA_MOD_DAMAGE_PERCENT_DONE:
                        if (physical && amount > 0.0f)
                        {
                            f.damageMult *= 1.0f + amount / 100.0f;
                            boosts = true;
                        }
                        break;
                    case SPELL_AURA_MOD_DAMAGE_DONE:
                        if (physical && amount > 1.0f)      // Enrage 8599 carries a +1 placeholder
                        {
                            f.damageFlat += amount;
                            boosts = true;
                        }
                        break;
                    default:
                        break;
                }
            }
            if (boosts)
                f.spellId = spellId;
            return f;
        }

        void AddInterrupt(TeamBotAI const* ai, SpellEntry const* spell, PartyMemberFacts& f)
        {
            if (!spell || !SpellTags::StopsCasts(spell))
                return;
            Player* me = ai->me;

            InterruptAbility a;
            a.spellId = spell->Id;
            a.maxRange = AbilityRange(me, spell, 5.0f);
            a.cooldownMs = spell->GetRecoveryTime();
            a.lockoutMs = LockoutMs(spell);
            a.mechanicMask = SpellTags::StopMechanicMask(spell);
            a.needsFront = spell->Attributes == 0x150010;     // same test as Spell::CheckCast (Gouge)
            a.breaksOnDamage = spell->HasAuraInterruptFlag(AURA_INTERRUPT_DAMAGE_CANCELS);
            Powers const powerType = Powers(spell->powerType);
            a.ready = me->IsSpellReady(spell) &&
                      (powerType == POWER_HEALTH || me->GetPower(powerType) >= Spell::CalculatePowerCost(spell, me)) &&
                      spell->GetErrorAtShapeshiftedCast(me->GetShapeshiftForm()) == SPELL_CAST_OK;
            // Asleep / stunned, or silenced for a spell that silence blocks (Kick still works).
            if (f.controlled || (f.silenced && spell->PreventionType == SPELL_PREVENTION_TYPE_SILENCE))
                a.ready = false;
            f.interrupts.push_back(a);
        }

        // A friendly creature that may be an ally of the party: alive, fighting, near `center`,
        // friendly to `reference` (the leader), and nobody's pet, guardian, totem or charm
        // (party pets are party members; other players' pets are not ours to protect).
        bool IsAllyCandidate(Creature const* c, Unit const* reference, WorldObject const* center)
        {
            if (!c->IsAlive() || !c->IsInWorld() || c->GetMap() != center->GetMap() || !c->IsInCombat())
                return false;
            if (c->IsPet() || c->IsTotem() || !c->GetOwnerGuid().IsEmpty() || !c->GetCharmerGuid().IsEmpty() ||
                c->GetCreatureType() == CREATURE_TYPE_CRITTER)
                return false;
            if (!center->IsWithinDistInMap(c, ALLY_RANGE))
                return false;
            return c->IsFriendlyTo(reference);
        }

        void FillAlly(Creature const* c, AllyFacts& a)
        {
            a.guid = c->GetObjectGuid();
            a.entry = c->GetEntry();
            a.level = c->GetLevel();
            a.rank = c->GetCreatureInfo()->rank;
            a.elite = a.rank != CREATURE_ELITE_NORMAL;
            a.healthPct = c->GetHealthPercent();
            a.health = c->GetHealth();
            a.maxHealth = c->GetMaxHealth();
            a.armor = uint32(std::max(0, c->GetArmor()));
            a.pos = PositionOf(c);
            if (Unit const* victim = c->GetVictim())
                a.victim = victim->GetObjectGuid();
        }

        // PackCreature::controlSpellId: a harmful aura taking an idle creature out (a Sap).
        uint32 IdleControl(Creature const* creature)
        {
            for (auto const& itr : creature->GetSpellAuraHolderMap())
            {
                SpellAuraHolder const* holder = itr.second;
                if (holder->IsPositive())
                    continue;
                SpellEntry const* spell = holder->GetSpellProto();
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (SpellTags::IsDisableEffect(spell, i))
                        return spell->Id;
            }
            return 0;
        }

        // `caster`'s harmful control cast that is done and on its way to a unit (SPELL_STATE_DELAYED,
        // spell batching or travel time), or nullptr.
        Spell const* LandingControl(Unit const* caster)
        {
            Spell const* spell = caster->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            if (!spell || spell->getState() != SPELL_STATE_DELAYED || spell->m_targets.getUnitTargetGuid().IsEmpty())
                return nullptr;
            bool disables = false;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                disables = disables || SpellTags::IsDisableEffect(spell->m_spellInfo, i);
            return disables && !spell->m_spellInfo->IsPositiveSpell() ? spell : nullptr;
        }

        // The longest control effect on `creature` (EnemyFacts::control) and the longest periodic
        // damage a party member put on it (partyDotMs).
        void FillControlState(Creature const* creature, std::vector<Unit*> const& partyUnits, EnemyFacts& e)
        {
            for (auto const& itr : creature->GetSpellAuraHolderMap())
            {
                SpellAuraHolder const* holder = itr.second;
                SpellEntry const* spell = holder->GetSpellProto();
                uint32 const left = uint32(std::max(0, holder->GetAuraDuration()));
                bool disables = false;
                bool fear = false;
                bool dot = false;
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                {
                    if (SpellTags::IsDisableEffect(spell, i))
                    {
                        disables = true;
                        fear = fear || spell->EffectApplyAuraName[i] == SPELL_AURA_MOD_FEAR;
                    }
                    if (spell->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE ||
                        spell->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_LEECH)
                        dot = true;
                }
                if (disables && !holder->IsPositive() && left >= e.control.remainingMs)
                {
                    e.control.spellId = spell->Id;
                    e.control.caster = holder->GetCasterGuid();
                    e.control.remainingMs = left;
                    e.control.breaksOnDamage = spell->HasAuraInterruptFlag(AURA_INTERRUPT_DAMAGE_CANCELS);
                    e.control.fear = fear;
                }
                if (dot)
                {
                    ObjectGuid const caster = holder->GetCasterGuid();
                    bool const ours = std::any_of(partyUnits.begin(), partyUnits.end(),
                        [caster](Unit const* u) { return u->GetObjectGuid() == caster; });
                    if (ours)
                    {
                        e.partyDotMs = std::max(e.partyDotMs, left);
                        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                        {
                            if (spell->EffectApplyAuraName[i] != SPELL_AURA_PERIODIC_DAMAGE &&
                                spell->EffectApplyAuraName[i] != SPELL_AURA_PERIODIC_LEECH)
                                continue;
                            Aura const* aura = holder->GetAuraByEffectIndex(SpellEffectIndex(i));
                            if (!aura || aura->GetModifier()->periodictime <= 0)
                                continue;
                            PartyDot d;
                            d.caster = caster;
                            d.spellId = spell->Id;
                            d.perTick = std::max(0.0f, float(aura->GetModifier()->m_amount));
                            d.periodMs = uint32(aura->GetModifier()->periodictime);
                            d.nextTickMs = uint32(std::max(0, aura->GetAuraPeriodicTimer()));
                            d.remainingMs = left;
                            e.partyDots.push_back(d);
                        }
                    }
                }
            }

            // A party control on its way to it (ActiveControl): at its full duration.
            for (Unit const* u : partyUnits)
            {
                Spell const* spell = LandingControl(u);
                if (!spell || spell->m_targets.getUnitTargetGuid() != creature->GetObjectGuid())
                    continue;
                std::shared_ptr<SpellProfile const> profile = sProfileStore.GetSpell(spell->m_spellInfo->Id);
                uint32 const full = profile ? profile->durationMs : 0;
                if (full < e.control.remainingMs)
                    continue;
                e.control.spellId = spell->m_spellInfo->Id;
                e.control.caster = u->GetObjectGuid();
                e.control.remainingMs = full;
                e.control.landing = true;
                e.control.breaksOnDamage = spell->m_spellInfo->HasAuraInterruptFlag(AURA_INTERRUPT_DAMAGE_CANCELS);
                e.control.fear = false;
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (SpellTags::IsDisableEffect(spell->m_spellInfo, i) &&
                        spell->m_spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MOD_FEAR)
                        e.control.fear = true;
            }
        }

        // Melee attack power as Creature::UpdateDamagePhysical scales the damage with it, and the
        // strongest attack power debuff on it (Perception/Values/MitigationValue.h).
        void FillAttackPower(Creature* creature, EnemyFacts& e)
        {
            if (CreatureClassLevelStats const* cls = creature->GetClassLevelStats())
                e.apDb = float(cls->attack_power);
            e.apNow = creature->GetTotalAttackPowerValue(BASE_ATTACK);
            for (Aura const* aura : creature->GetAurasByType(SPELL_AURA_MOD_ATTACK_POWER))
            {
                float const off = -float(aura->GetModifier()->m_amount);
                if (off <= e.apDebuff)
                    continue;
                e.apDebuff = off;
                e.apDebuffMs = uint32(std::max(0, aura->GetAuraDuration()));
            }
        }

        // Damage curses on it, anyone's (EnemyFacts::curses).
        void FillCursesOn(Creature const* creature, EnemyFacts& e)
        {
            for (auto const& itr : creature->GetSpellAuraHolderMap())
            {
                SpellAuraHolder const* holder = itr.second;
                CurseKind const kind = WarlockCurses::KindOf(holder->GetSpellProto());
                if (kind == CurseKind::None)
                    continue;
                CurseOnFacts c;
                c.kind = uint8(kind);
                c.caster = holder->GetCasterGuid();
                c.remainingMs = uint32(std::max(0, holder->GetAuraDuration()));
                e.curses.push_back(c);
            }
        }

        void FillEnemy(Creature* creature, std::vector<Unit*> const& partyUnits, EnemyFacts& e)
        {
            e.guid = creature->GetObjectGuid();
            e.entry = creature->GetEntry();
            e.level = creature->GetLevel();
            // IsElite() is false for every Pet, guardians included (summoned elite adds).
            e.rank = creature->GetCreatureInfo()->rank;
            e.elite = e.rank != CREATURE_ELITE_NORMAL;
            e.healthPct = creature->GetHealthPercent();
            e.health = creature->GetHealth();
            e.maxHealth = creature->GetMaxHealth();
            e.meleeHit = (creature->GetFloatValue(UNIT_FIELD_MINDAMAGE) + creature->GetFloatValue(UNIT_FIELD_MAXDAMAGE)) / 2.0f;
            if (uint32 const attackMs = creature->GetAttackTime(BASE_ATTACK))
                e.meleeDps = e.meleeHit / (attackMs / 1000.0f);
            e.pos = PositionOf(creature);
            e.boundingRadius = creature->GetObjectBoundingRadius();
            e.speed = creature->GetSpeed(MOVE_RUN);
            e.slowed = creature->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED);
            e.rooted = creature->HasUnitState(UNIT_STATE_ROOT);
            FillCast(creature, e);
            FillTimers(creature, e);
            e.crowdControlled = creature->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL);
            e.creatureTypeMask = creature->GetCreatureTypeMask();
            FillControlState(creature, partyUnits, e);
            e.ignoresAssistThreat = IgnoresAssistThreat(creature);
            e.meleeEnabled = !creature->AI() || creature->AI()->IsMeleeAttackEnabled();
            e.moving = creature->IsMoving();
            e.patrol = creature->GetDefaultMovementType() == WAYPOINT_MOTION_TYPE;
            // Temporary damage shields and spell reflection (doctrine HoldFire, RoleStamps.h).
            for (AuraType type : { SPELL_AURA_DAMAGE_SHIELD, SPELL_AURA_REFLECT_SPELLS, SPELL_AURA_REFLECT_SPELLS_SCHOOL })
                for (Aura const* aura : creature->GetAurasByType(type))
                    if (aura->GetAuraDuration() > 0 && uint32(aura->GetAuraDuration()) > e.reflectMs)
                    {
                        e.reflectMs = uint32(aura->GetAuraDuration());
                        e.reflectSpellId = aura->GetId();
                    }
            for (auto const& itr : creature->GetSpellAuraHolderMap())
            {
                SpellAuraHolder const* holder = itr.second;
                if (holder && holder->GetSpellProto()->HasAttribute(SPELL_ATTR_EX2_SPECIAL_TAMING_FLAG) &&
                    std::any_of(partyUnits.begin(), partyUnits.end(), [holder](Unit const* u)
                    {
                        return u->GetObjectGuid() == holder->GetCasterGuid();
                    }))
                    e.beingTamed = true;
            }
            e.meleePhysical = creature->GetCreatureInfo()->damage_school == SPELL_SCHOOL_NORMAL;
            FillAttackPower(creature, e);
            e.armor = creature->GetArmor();
            FillCursesOn(creature, e);
            if (!e.meleeEnabled)
                e.meleeDps = 0.0f;     // walking bombs, casters that stand back: no melee to count
            if (std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(e.entry))
            {
                e.boss = profile->boss;
                // Standing back with melee off: it hurts with its main ranged spell instead.
                if (profile->mainRangedSpellId && !e.meleeEnabled)
                    e.rangedDps = RangedSpellDps(*profile, e.level, e.meleeHit);
                // EventAI low health enrages not up yet (IncomingTimeline).
                for (CreatureSpellRef const& ref : profile->spells)
                {
                    if (ref.source != SpellSource::EventAI || ref.healthPct <= 0.0f || creature->HasAura(ref.spellId))
                        continue;
                    EnrageFacts f = EnrageOf(ref.spellId);
                    if (!f.spellId)
                        continue;
                    f.atHealthPct = ref.healthPct;
                    e.enrages.push_back(f);
                }
            }
            e.silenced = creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SILENCED);
            e.maxMana = creature->GetMaxPower(POWER_MANA);
            if (e.maxMana > 0)
            {
                e.mana = creature->GetPower(POWER_MANA);
                e.createMana = creature->GetCreateMana();
            }
            e.acting = !creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE) &&
                       !creature->IsTotalImmune() && !creature->IsInEvadeMode();
            if (creature->IsTemporarySummon())
            {
                e.summoned = true;
                e.summoner = static_cast<TemporarySummon const*>(creature)->GetSummonerGuid();
            }
            else if (creature->IsPet() || !creature->GetCreatorGuid().IsEmpty())
            {
                e.summoned = true;
                e.summoner = creature->GetOwnerGuid() ? creature->GetOwnerGuid() : creature->GetCreatorGuid();
            }

            ThreatManager& tm = creature->GetThreatManager();
            if (HostileReference* victimRef = tm.getCurrentVictim())
            {
                e.victim = victimRef->getUnitGuid();
                e.victimThreat = victimRef->getThreat();
            }

            // Every row of the live list. Rows of allies (and anyone else) stay with
            // partySide = false: ThreatView::Headroom needs the victim's threat when an ally
            // tank is the one being hit, VictimLead skips them.
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
        }

        // Creatures whose threat list holds `unit` (reverse lookup through its HostileRefManager),
        // alive and on `map`, appended to `out` unless already there. Stops at `limit` entries.
        void CollectAttackers(Unit* unit, Map const* map, std::vector<Creature*>& out, size_t limit,
                              Player const* validator)
        {
            for (HostileReference* ref = unit->GetHostileRefManager().getFirst(); ref; ref = ref->next())
            {
                if (out.size() >= limit)
                    return;
                Unit* owner = ref->getSource()->getOwner();
                if (!owner || !owner->IsCreature() || !owner->IsAlive() || !owner->IsInWorld() || owner->GetMap() != map)
                    continue;
                // Allies' attackers only: must be something we may attack (not a neutral
                // creature an escort picked a fight with in a town, not a duel).
                if (validator && !validator->IsValidAttackTarget(owner))
                    continue;
                Creature* creature = static_cast<Creature*>(owner);
                if (std::find(out.begin(), out.end(), creature) == out.end())
                    out.push_back(creature);
            }
        }

        // The start of each enemy's current cast goes into world.castStartLog (kept across
        // refreshes); entries of enemies no longer on the lists are dropped. Then every enemy
        // gets its own entries in castStarts.
        void UpdateCastStarts(WorldModel& world, uint32 nowMs)
        {
            std::vector<WorldModel::CastStartRecord>& log = world.castStartLog;
            log.erase(std::remove_if(log.begin(), log.end(), [&world](WorldModel::CastStartRecord const& r)
            {
                return !world.FindEnemy(r.enemy);
            }), log.end());

            for (EnemyFacts& e : world.enemies)
            {
                if (e.castingSpellId)
                {
                    uint32 const elapsed = e.castTotalMs > e.castRemainingMs ? e.castTotalMs - e.castRemainingMs : 0;
                    uint32 const start = nowMs - elapsed;
                    auto itr = std::find_if(log.begin(), log.end(), [&e](WorldModel::CastStartRecord const& r)
                    {
                        return r.enemy == e.guid && r.cast.spellId == e.castingSpellId;
                    });
                    if (itr == log.end())
                    {
                        WorldModel::CastStartRecord r;
                        r.enemy = e.guid;
                        r.cast.spellId = e.castingSpellId;
                        itr = log.insert(log.end(), r);
                    }
                    // Same cast seen again: same start (up to timer steps). A new cast: later start.
                    itr->cast.lastStartMs = start;
                }
                for (WorldModel::CastStartRecord const& r : log)
                    if (r.enemy == e.guid)
                        e.castStarts.push_back(r.cast);
            }
        }

        // Has an interrupt that works from range (Counterspell, Earth Shock, Silence, Hammer of
        // Justice, Feral Charge): those need line of sight; melee ones need reach instead.
        bool HasRangedInterrupt(PartyMemberFacts const& f)
        {
            return std::any_of(f.interrupts.begin(), f.interrupts.end(),
                [](InterruptAbility const& a) { return a.maxRange > MELEE_INTERRUPT_RANGE; });
        }

        void FillInterrupts(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            for (SpellEntry const* spell : ai->Kit().Interrupts())
                AddInterrupt(ai, spell, f);
        }

        // Casts the arbiter tried and could not execute (BotDirectives::GetCastFailures).
        void FillCastFailures(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            for (CastFailure const& c : ai->Directives().GetCastFailures(WorldTimer::getMSTime()))
            {
                CastFailureFacts x;
                x.target = c.target;
                x.spellId = c.spellId;
                x.count = c.count;
                x.failingMs = WorldTimer::getMSTimeDiff(c.firstMs, c.lastMs);
                f.castFailures.push_back(x);
            }
        }

        void FillDispels(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            Player* me = ai->me;
            for (SpellEntry const* spell : ai->Kit().Dispels())
            {
                if (!spell)
                    continue;
                DispelAbility a;
                a.spellId = spell->Id;
                a.hostileOk = true;
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                {
                    if (spell->Effect[i] != SPELL_EFFECT_DISPEL)
                        continue;
                    a.dispelMask |= Spells::GetDispellMask(DispelType(spell->EffectMiscValue[i]));
                    if (spell->EffectImplicitTargetA[i] == TARGET_UNIT_FRIEND)
                        a.hostileOk = false;
                }
                if (!a.dispelMask)
                    continue;
                a.maxRange = sSpellRangeStore.LookupEntry(spell->rangeIndex) ? CombatBotBaseAI::SpellMaxRange(me, spell) : 30.0f;
                a.ready = !f.controlled && !f.silenced && me->IsSpellReady(spell) &&
                          me->GetPower(Powers(spell->powerType)) >= Spell::CalculatePowerCost(spell, me) &&
                          spell->GetErrorAtShapeshiftedCast(me->GetShapeshiftForm()) == SPELL_CAST_OK;
                f.dispels.push_back(a);
            }
        }

        bool HasReagents(Player const* me, SpellEntry const* spell)
        {
            for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
                if (spell->Reagent[i] > 0 && !me->HasItemCount(spell->Reagent[i], spell->ReagentCount[i]))
                    return false;
            return true;
        }

        void FillControls(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            Player* me = ai->me;
            for (SpellEntry const* spell : ai->Kit().Controls())
            {
                if (!spell)
                    continue;
                std::shared_ptr<SpellProfile const> profile = sProfileStore.GetSpell(spell->Id);
                if (!profile || !profile->controls)
                    continue;
                ControlAbility a;
                a.spellId = spell->Id;
                a.creatureTypeMask = spell->TargetCreatureType;
                a.mechanicMask = SpellTags::StopMechanicMask(spell);
                a.school = uint8(spell->School);
                a.maxRange = AbilityRange(me, spell, 5.0f);
                // Area around the caster (Psychic Scream, Howl of Terror; Intimidating Shout
                // fears everyone around, its target only cowers): a radius on a control effect.
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (SpellTags::IsDisableEffect(spell, i) && spell->EffectRadiusIndex[i])
                        if (SpellRadiusEntry const* radius = sSpellRadiusStore.LookupEntry(spell->EffectRadiusIndex[i]))
                            a.radius = std::max(a.radius, radius->Radius);
                a.castMs = profile->castMs;
                a.durationSec = profile->durationMs / 1000.0f;
                a.cooldownMs = spell->GetRecoveryTime();
                if (Powers(spell->powerType) == POWER_MANA)
                    a.manaCost = uint32(Spell::CalculatePowerCost(spell, me));
                a.breaksOnDamage = profile->breaksOnDamage;
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (SpellTags::IsDisableEffect(spell, i) && spell->EffectApplyAuraName[i] == SPELL_AURA_MOD_FEAR)
                        a.fear = true;
                a.needsStealth = (spell->Stances & (1u << (FORM_STEALTH - 1))) != 0;
                a.peacefulOnly = spell->HasAttribute(SPELL_ATTR_EX_ONLY_PEACEFUL_TARGETS);
                a.oneTarget = spell->HasSingleTargetAura();
                if (a.oneTarget)
                {
                    for (auto const& held : me->GetSingleCastSpellTargets())
                        if (Spells::IsSingleTargetSpells(held.first, spell))
                            a.holding = held.second;
                    // The one it is on its way to replaces it once it lands.
                    if (Spell const* landing = LandingControl(me))
                        if (Spells::IsSingleTargetSpells(landing->m_spellInfo, spell))
                            a.holding = landing->m_targets.getUnitTargetGuid();
                }
                Powers const powerType = Powers(spell->powerType);
                bool formOk = spell->GetErrorAtShapeshiftedCast(me->GetShapeshiftForm()) == SPELL_CAST_OK;
                // Sap: it goes into stealth on its way (ControlAbility::ready).
                if (!formOk && a.needsStealth && !me->IsInCombat())
                    if (SpellEntry const* stealth = ai->Kit().Stealth())
                        formOk = ai->IsReadyWithStance(stealth);
                a.ready = !f.controlled && me->IsSpellReady(spell) &&
                          (powerType == POWER_HEALTH || me->GetPower(powerType) >= Spell::CalculatePowerCost(spell, me)) &&
                          formOk &&
                          HasReagents(me, spell) &&
                          !(f.silenced && spell->PreventionType == SPELL_PREVENTION_TYPE_SILENCE);
                f.controls.push_back(a);
            }
        }

        // Flee interception (todo 13): ClassKit::FleeStoppers with what the spell data says.
        // Stun or root: seconds it holds (a root, or a stun that damage breaks, at most
        // FLEE_ROOT_HOLD_SEC: the party's hits break it); slow: the speed it takes off.
        float const FLEE_ROOT_HOLD_SEC = 2.0f;
        void FillFleeStops(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            Player* me = ai->me;
            for (SpellEntry const* spell : ai->Kit().FleeStoppers())
            {
                if (!spell)
                    continue;
                FleeStopAbility a;
                a.spellId = spell->Id;
                bool root = false, stun = false;
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                {
                    if (spell->Effect[i] != SPELL_EFFECT_APPLY_AURA)
                        continue;
                    switch (spell->EffectApplyAuraName[i])
                    {
                        case SPELL_AURA_MOD_ROOT: root = true; break;
                        case SPELL_AURA_MOD_STUN: stun = true; break;
                        case SPELL_AURA_MOD_DECREASE_SPEED:
                            a.slowPct = std::max(a.slowPct, std::min(1.0f, -float(spell->CalculateSimpleValue(SpellEffectIndex(i))) / 100.0f));
                            break;
                        case SPELL_AURA_MOD_INCREASE_SPEED:
                            if (spell->rangeIndex == SPELL_RANGE_IDX_SELF_ONLY)
                                a.speedPct = std::max(a.speedPct, float(spell->CalculateSimpleValue(SpellEffectIndex(i))) / 100.0f);
                            break;
                        default: break;
                    }
                    if (spell->EffectRadiusIndex[i])
                        if (SpellRadiusEntry const* radius = sSpellRadiusStore.LookupEntry(spell->EffectRadiusIndex[i]))
                            a.radius = std::max(a.radius, radius->Radius);
                }
                float const sec = std::max(0, spell->GetDuration()) / 1000.0f;
                if (stun && !spell->HasAuraInterruptFlag(AURA_INTERRUPT_DAMAGE_CANCELS))
                    a.holdSec = sec;
                else if (root || stun)
                    a.holdSec = std::min(sec, FLEE_ROOT_HOLD_SEC);
                if (a.speedPct > 0.0f)
                    a.speedSec = sec;
                if (a.holdSec <= 0.0f && a.slowPct <= 0.0f && a.speedPct <= 0.0f)
                    continue;
                if (spell->rangeIndex != SPELL_RANGE_IDX_SELF_ONLY)
                    a.radius = 0.0f;
                a.maxRange = AbilityRange(me, spell, 5.0f);
                a.castMs = spell->GetCastTime(me);
                a.cooldownMs = spell->GetRecoveryTime();
                Powers const powerType = Powers(spell->powerType);
                if (powerType == POWER_MANA)
                    a.manaCost = uint32(Spell::CalculatePowerCost(spell, me));
                a.needsCombo = spell->NeedsComboPoints();
                a.ready = !f.controlled && me->IsSpellReady(spell) &&
                          (powerType == POWER_HEALTH || me->GetPower(powerType) >= Spell::CalculatePowerCost(spell, me)) &&
                          spell->GetErrorAtShapeshiftedCast(me->GetShapeshiftForm()) == SPELL_CAST_OK &&
                          !(f.silenced && spell->PreventionType == SPELL_PREVENTION_TYPE_SILENCE);
                f.fleeStops.push_back(a);
            }
        }

        // Team rescue (Perception/Values/RescueValue.h): a tank bot's taunts, one entry per spell.
        void FillTaunts(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            if (ai->GetRole() != ROLE_TANK)
                return;
            for (SpellEntry const* spell : ai->m_spellListTaunt)
            {
                if (!spell || std::any_of(f.taunts.begin(), f.taunts.end(),
                        [spell](TauntAbility const& t) { return t.spellId == spell->Id; }))
                    continue;
                TauntAbility t;
                t.spellId = spell->Id;
                t.maxRange = AbilityRange(ai->me, spell, 5.0f);
                // Taunt 355: RecoveryTime 0, CategoryRecoveryTime 10 s.
                t.cooldownMs = spell->GetRecoveryTime();
                t.holdSec = RescueValue::TemporaryTauntSec(spell);
                t.stanceSwap = ai->StanceSwapFor(spell) != nullptr;
                t.gcd = spell->StartRecoveryCategory > 0;
                t.ready = f.CanAct() && ai->IsReadyWithStance(spell);
                f.taunts.push_back(t);
            }
        }

        // ClassKit::AttackPowerDebuffs of a tank bot: the SPELL_AURA_MOD_ATTACK_POWER amount
        // below 0, its duration and radius from the spell data.
        void FillMitigations(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            if (ai->GetRole() != ROLE_TANK)
                return;
            Player* me = ai->me;
            for (SpellEntry const* spell : ai->Kit().AttackPowerDebuffs())
            {
                if (!spell)
                    continue;
                MitigationAbility a;
                a.spellId = spell->Id;
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (spell->EffectApplyAuraName[i] == SPELL_AURA_MOD_ATTACK_POWER)
                        a.reduction = std::max(a.reduction, -float(spell->CalculateSimpleValue(SpellEffectIndex(i))));
                a.durationSec = std::max(0, spell->GetDuration()) / 1000.0f;
                a.radius = SpellTags::AreaRadius(spell);
                if (a.reduction <= 0.0f || a.durationSec <= 0.0f || a.radius <= 0.0f)
                    continue;
                if (Powers(spell->powerType) == POWER_RAGE)
                    a.rageCost = uint32(Spell::CalculatePowerCost(spell, me));
                a.stanceSwap = ai->StanceSwapFor(spell) != nullptr;
                a.ready = f.CanAct() && ai->IsReadyWithStance(spell) &&
                          !(f.silenced && spell->PreventionType == SPELL_PREVENTION_TYPE_SILENCE);
                f.mitigations.push_back(a);
            }
        }

        // ClassKit::ThreatDrops of a non-tank bot. Fade: SPELL_AURA_MOD_TOTAL_THREAT with a
        // negative amount on every threat list for the aura's duration. Vanish: a sanctuary
        // effect (Spell::EffectSanctuary removes the rogue from every threat list); Feign
        // Death: its aura (Unit::SetFeignDeath deletes every hostile reference). Feint, Cower:
        // SPELL_EFFECT_THREAT with a negative amount on the target (Spell::EffectThreat ->
        // AddThreat: the caster's threat modifiers apply, ThreatCalcHelper::CalcThreat).
        // `spell` is on cooldown and its reset (ClassKit::CooldownReset) is ready: the reset's
        // facts; otherwise none (spellId 0). Only its cooldown keeps `spell`: its power is there.
        // The caster able to act and not silenced for the reset is the caller's.
        CooldownResetFacts ResetFor(TeamBotAI const* ai, PartyMemberFacts const& f, SpellEntry const* spell)
        {
            CooldownResetFacts r;
            Player* me = ai->me;
            SpellEntry const* reset = ai->Kit().CooldownReset(spell);
            if (!reset || me->IsSpellReady(spell) || !ai->IsReadyWithStance(reset) ||
                (f.silenced && reset->PreventionType == SPELL_PREVENTION_TYPE_SILENCE))
                return r;
            Powers const powerType = Powers(spell->powerType);
            if (powerType != POWER_HEALTH && me->GetPower(powerType) < uint32(Spell::CalculatePowerCost(spell, me)))
                return r;
            r.spellId = reset->Id;
            r.cooldownMs = reset->GetRecoveryTime();
            r.gcd = reset->StartRecoveryTime > 0;
            return r;
        }

        void FillThreatDrops(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            if (ai->GetRole() == ROLE_TANK)
                return;
            Player* me = ai->me;
            for (SpellEntry const* spell : ai->Kit().ThreatDrops())
            {
                if (!spell)
                    continue;
                ThreatDropAbility a;
                a.spellId = spell->Id;
                a.wipe = spell->HasEffect(SPELL_EFFECT_SANCTUARY) || spell->HasAura(SPELL_AURA_FEIGN_DEATH);
                if (!a.wipe)
                {
                    for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                    {
                        float const amount = -float(spell->CalculateSimpleValue(SpellEffectIndex(i)));
                        if (spell->Effect[i] == SPELL_EFFECT_THREAT && amount > 0.0f)
                        {
                            a.onTarget = true;
                            a.drop = me->ApplyTotalThreatModifier(amount, spell->GetSpellSchoolMask());
                            a.maxRange = AbilityRange(me, spell, 5.0f);
                            break;
                        }
                        if (spell->Effect[i] == SPELL_EFFECT_APPLY_AURA &&
                            spell->EffectApplyAuraName[i] == SPELL_AURA_MOD_TOTAL_THREAT && amount > 0.0f)
                        {
                            a.drop = amount;
                            a.durationSec = std::max(0, spell->GetDuration()) / 1000.0f;
                            break;
                        }
                    }
                    if (a.drop <= 0.0f)
                        continue;   // no threat drop in its data
                }
                a.cooldownMs = spell->GetRecoveryTime();     // Feint, Cower 10 s; Fade 30 s; Feign Death 30 s
                if (Powers(spell->powerType) == POWER_MANA)
                    a.manaCost = uint32(Spell::CalculatePowerCost(spell, me));
                // Facts only (not CanTryToCastSpell: the brain's constraints are the brain's).
                // The form (cat Cower) is checked by IsReadyWithStance.
                a.ready = f.CanAct() && !f.silenced && ai->IsReadyWithStance(spell);
                if (!a.ready && f.CanAct() && !f.silenced)
                {
                    a.reset = ResetFor(ai, f, spell);
                    a.ready = a.reset.spellId != 0;
                }
                f.threatDrops.push_back(a);
            }
        }

        // Team rescue: ClassKit::Survival with the numbers from the spell data.
        void FillSurvival(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            Player* me = ai->me;
            std::vector<SurvivalSpell> spells = ai->Kit().Survival();
            std::vector<SurvivalSpell> const racial = RaceKit::Survival(*ai);
            spells.insert(spells.end(), racial.begin(), racial.end());
            for (SurvivalSpell const& s : spells)
            {
                SpellEntry const* spell = s.spell;
                if (!spell)
                    continue;
                SurvivalAbility a;
                a.spellId = spell->Id;
                a.effect = SurvivalAbility::Effect(uint8(s.effect));
                a.durationSec = s.durationSec > 0.0f ? s.durationSec : std::max(0, spell->GetDuration()) / 1000.0f;
                switch (s.effect)
                {
                    case SurvivalEffect::Absorb:
                        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                            if (spell->EffectApplyAuraName[i] == SPELL_AURA_SCHOOL_ABSORB)
                                a.amount = float(std::max(0, spell->CalculateSimpleValue(SpellEffectIndex(i))));
                        break;
                    case SurvivalEffect::Reduce:
                        a.amount = 1.0f;
                        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                            if (spell->EffectApplyAuraName[i] == SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN)
                                a.amount = std::max(0.0f, 1.0f + spell->CalculateSimpleValue(SpellEffectIndex(i)) / 100.0f);
                        break;
                    case SurvivalEffect::ReducePhysical:
                        a.amount = s.fraction;
                        break;
                    case SurvivalEffect::Heal:
                        // Lay on Hands: the caster's maximum health (SPELL_EFFECT_HEAL_MAX_HEALTH);
                        // a plain heal (Desperate Prayer): its amount with the caster's bonuses.
                        a.amount = spell->HasEffect(SPELL_EFFECT_HEAL) ?
                            HealSpells::Estimate(me, me, spell).direct : float(me->GetMaxHealth());
                        break;
                    case SurvivalEffect::ExtraHealth:
                        a.amount = s.fraction * float(me->GetMaxHealth());
                        break;
                    default:
                        break;
                }
                a.maxRange = s.selfOnly ? 0.0f : std::max(CombatBotBaseAI::SpellMaxRange(me, spell), 5.0f);
                a.cooldownMs = spell->GetRecoveryTime();
                if (s.allMana)
                    a.manaCost = me->GetPower(POWER_MANA);
                else if (Powers(spell->powerType) == POWER_MANA)
                    a.manaCost = uint32(Spell::CalculatePowerCost(spell, me));
                a.mechanic = spell->Mechanic;
                a.selfOnly = s.selfOnly;
                a.notOnTank = s.notOnTank;
                a.casterStops = s.casterStops;
                a.targetStops = s.targetStops;
                a.gcd = spell->StartRecoveryTime > 0;
                // Divine Shield, Ice Block: castable while stunned or feared, they lift it
                // (Spell::CheckCasterAuras; TeamBotAI::CastRescueWhileControlled casts them).
                bool const liftsControl = spell->HasAttribute(SPELL_ATTR_EX_IMMUNITY_PURGES_EFFECT);
                bool const castable = !(f.silenced && spell->PreventionType == SPELL_PREVENTION_TYPE_SILENCE) &&
                                      (!s.needsShield || ai->IsWearingShield(me));
                a.ready = castable && (f.CanAct() || (f.alive && liftsControl)) && ai->IsReadyWithStance(spell);
                // A reset (Cold Snap, Preparation) is cast first: not while controlled.
                if (!a.ready && castable && f.CanAct())
                {
                    a.reset = ResetFor(ai, f, spell);
                    a.ready = a.reset.spellId != 0;
                }
                a.renewWhileHit = s.renewWhileHit;
                f.survival.push_back(a);
            }
        }

        // Healthstones and healing potions in the bags (ItemKit::Use::Heal): rescue options on
        // itself, like a self heal spell.
        void FillItemSurvival(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            Player* me = ai->me;
            for (ItemKit::Consumable const& c : ItemKit::Held(*ai, ItemKit::Use::Heal))
            {
                SurvivalAbility a;
                a.spellId = c.spell->Id;
                a.itemId = c.proto->ItemId;
                a.itemCategory = c.category;
                a.effect = SurvivalAbility::Effect::Heal;
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (c.spell->Effect[i] == SPELL_EFFECT_HEAL)
                        a.amount += ItemKit::AverageValue(c.spell, i);
                if (a.amount <= 0.0f)
                    continue;
                a.cooldownMs = c.cooldownMs;
                a.selfOnly = true;
                a.gcd = c.spell->StartRecoveryTime > 0;
                a.ready = f.CanAct() && me->IsSpellReady(c.spell, c.proto) && me->CanUseItem(c.proto) == EQUIP_ERR_OK;
                f.survival.push_back(a);
            }
        }

        // A bot's pet (the off-tank, user 2026-11-15): its own survival spell (Shell Shield) and
        // threat drop (Cower), read from its learned spells (PetAbilities), the same facts as a
        // bot's. The owner's RescueBehavior has the pet cast them (BotActionType::PetCast).
        void FillPetAbilities(Pet* pet, PartyMemberFacts& f)
        {
            auto ready = [pet, &f](SpellEntry const* spell)
            {
                Powers const power = Powers(spell->powerType);
                return f.CanAct() && pet->IsSpellReady(spell) &&
                       !(f.silenced && spell->PreventionType == SPELL_PREVENTION_TYPE_SILENCE) &&
                       (power == POWER_HEALTH || pet->GetPower(power) >= Spell::CalculatePowerCost(spell, pet));
            };

            std::vector<SpellEntry const*> spells;
            PetAbilities::Learned(pet, PetAbilities::Kind::Survival, spells);
            for (SpellEntry const* spell : spells)
            {
                SurvivalAbility a;
                a.spellId = spell->Id;
                a.effect = SurvivalAbility::Effect::Reduce;
                a.amount = 1.0f;
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (spell->EffectApplyAuraName[i] == SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN)
                        a.amount = std::max(0.0f, 1.0f + spell->CalculateSimpleValue(SpellEffectIndex(i)) / 100.0f);
                a.durationSec = std::max(0, spell->GetDuration()) / 1000.0f;
                a.cooldownMs = spell->GetRecoveryTime();
                a.mechanic = spell->Mechanic;
                a.selfOnly = true;
                a.gcd = spell->StartRecoveryTime > 0;
                a.ready = ready(spell);
                f.survival.push_back(a);
            }

            spells.clear();
            PetAbilities::Learned(pet, PetAbilities::Kind::ThreatDrop, spells);
            for (SpellEntry const* spell : spells)
            {
                ThreatDropAbility a;
                a.spellId = spell->Id;
                a.onTarget = true;
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (spell->Effect[i] == SPELL_EFFECT_THREAT)
                        a.drop = pet->ApplyTotalThreatModifier(-float(spell->CalculateSimpleValue(SpellEffectIndex(i))),
                                                               spell->GetSpellSchoolMask());
                if (a.drop <= 0.0f)
                    continue;
                a.maxRange = AbilityRange(pet, spell, 5.0f);
                a.cooldownMs = spell->GetRecoveryTime();
                a.ready = ready(spell);
                f.threatDrops.push_back(a);
            }
        }

        // Mana potions, mana gems and runes in the bags (ItemKit::Use::Mana).
        void FillManaItems(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            Player* me = ai->me;
            if (me->GetMaxPower(POWER_MANA) == 0)
                return;
            for (ItemKit::Consumable const& c : ItemKit::Held(*ai, ItemKit::Use::Mana))
            {
                ManaItemAbility a;
                a.itemId = c.proto->ItemId;
                a.spellId = c.spell->Id;
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                {
                    if (c.spell->Effect[i] == SPELL_EFFECT_ENERGIZE && c.spell->EffectMiscValue[i] == POWER_MANA)
                        a.mana += ItemKit::AverageValue(c.spell, i);
                    else if (c.spell->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE)
                        a.healthCost += ItemKit::AverageValue(c.spell, i);
                }
                if (a.mana <= 0.0f)
                    continue;
                a.cooldownMs = c.cooldownMs;
                a.category = c.category;
                a.ready = f.CanAct() && me->IsSpellReady(c.spell, c.proto) && me->CanUseItem(c.proto) == EQUIP_ERR_OK;
                f.manaItems.push_back(a);
            }
        }

        // ClassKit::CombatResurrection (Rebirth): its reagent is checked here, the server only
        // refuses the cast (Spell::CheckItems).
        void FillCombatRes(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            SpellEntry const* spell = ai->Kit().CombatResurrection();
            if (!spell)
                return;
            Player* me = ai->me;
            CombatResAbility& a = f.combatRes;
            a.spellId = spell->Id;
            a.maxRange = std::max(CombatBotBaseAI::SpellMaxRange(me, spell), 5.0f);
            a.castMs = spell->GetCastTime(me);
            a.cooldownMs = spell->GetRecoveryTime();
            if (Powers(spell->powerType) == POWER_MANA)
                a.manaCost = uint32(Spell::CalculatePowerCost(spell, me));
            bool reagents = true;
            for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
                if (spell->Reagent[i] > 0 && !me->HasItemCount(uint32(spell->Reagent[i]), spell->ReagentCount[i]))
                    reagents = false;
            a.ready = reagents && f.CanAct() && ai->IsReadyWithStance(spell) &&
                      !(f.silenced && spell->PreventionType == SPELL_PREVENTION_TYPE_SILENCE);
        }

        // PartyMemberFacts::selfRes and resurrects, every member (wipe protection).
        void FillRevive(Player const* member, TeamBotAI const* ai, PartyMemberFacts& f)
        {
            uint32 riseSpell = 0;
            if (member->IsAlive())
                f.selfRes = member->SelectResurrectionSpellId() != 0;
            else
            {
#if SUPPORTED_CLIENT_BUILD >= CLIENT_BUILD_1_6_1
                riseSpell = member->GetUInt32Value(PLAYER_SELF_RES_SPELL);
                f.selfRes = riseSpell != 0;
#else
                riseSpell = member->GetResurrectionSpellId();
                f.selfRes = member->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_CAN_SELF_RESURRECT);
#endif
            }
            f.resurrects = TeamBotAI::CanResurrectOthers(member);
            // As Spell::EffectSelfResurrect.
            if (SpellEntry const* spell = f.selfRes ? sSpellMgr.GetSpellEntry(riseSpell) : nullptr)
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                {
                    if (spell->Effect[i] != SPELL_EFFECT_SELF_RESURRECT)
                        continue;
                    int32 const amount = spell->CalculateSimpleValue(SpellEffectIndex(i));
                    if (amount < 0)
                    {
                        f.riseHealth = uint32(-amount);
                        f.riseMana = uint32(std::max(0, spell->EffectMiscValue[i]));
                    }
                    else
                    {
                        f.riseHealth = uint32(amount / 100.0f * member->GetMaxHealth());
                        f.riseMana = uint32(amount / 100.0f * member->GetMaxPower(POWER_MANA));
                    }
                    f.riseHealth = std::min(f.riseHealth, member->GetMaxHealth());
                    break;
                }
        }

        // PartyMemberFacts::soulstoneItem: a warlock bot's Soulstone in its bags, its use ready
        // (the same item as WarlockRotation::Soulstone: what its best Create Soulstone makes).
        void FillSoulstone(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            SpellEntry const* create = ai->m_spells.warlock.pCreateSoulstone;
            if (ai->me->GetClass() != CLASS_WARLOCK || !create)
                return;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            {
                if (create->Effect[i] != SPELL_EFFECT_CREATE_ITEM)
                    continue;
                uint32 const stone = uint32(create->EffectItemType[i]);
                ItemPrototype const* proto = sObjectMgr.GetItemPrototype(stone);
                SpellEntry const* use = proto ? sSpellMgr.GetSpellEntry(proto->Spells[0].SpellId) : nullptr;
                if (use && ai->me->HasItemCount(stone, 1) && ai->me->IsSpellReady(use, proto))
                    f.soulstoneItem = stone;
                return;
            }
        }

        // Dead members whose body (not released) or corpse (released) is on `map`.
        void FillCorpses(std::vector<Player*> const& members, Map* map, WorldModel& world)
        {
            for (Player* member : members)
            {
                if (!member || member->IsAlive())
                    continue;
                CorpseFacts c;
                c.guid = member->GetObjectGuid();
                c.released = member->GetDeathState() == DEAD;
                c.resRequested = member->IsRessurectRequested();
                if (!c.released)
                {
                    if (!member->IsInWorld() || member->GetMap() != map)
                        continue;
                    c.pos = PositionOf(member);
                }
                else
                {
                    Corpse* corpse = sObjectAccessor.GetCorpseForPlayerGUID(c.guid);
                    if (!corpse || corpse->GetType() == CORPSE_BONES || map->GetCorpse(corpse->GetObjectGuid()) != corpse)
                        continue;
                    c.pos = PositionOf(corpse);
                }
                if (PartyMemberFacts const* f = world.FindMember(c.guid))
                {
                    c.role = f->role;
                    c.isBot = f->isBot;
                }
                else if (TeamBotAI const* ai = member->AI() ? dynamic_cast<TeamBotAI const*>(member->AI()) : nullptr)
                {
                    c.role = uint8(ai->m_role);
                    c.isBot = true;
                }
                else
                {
                    uint8 const chosen = sCommanderMgr.GetPlayerRole(c.guid);
                    c.role = chosen != ROLE_INVALID ? chosen : uint8(RoleCapabilities::Assess(member).Best());
                }
                world.corpses.push_back(c);
            }
        }

        // Cooldowns for a party member: ClassKit::AllyBoosts.
        void FillAllyBoosts(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            Player* me = ai->me;
            for (AllyBoostSpell const& s : ai->Kit().AllyBoosts())
            {
                SpellEntry const* spell = s.spell;
                if (!spell)
                    continue;
                AllyBoostAbility a;
                a.spellId = spell->Id;
                a.kind = uint8(s.kind);
                a.maxRange = std::max(CombatBotBaseAI::SpellMaxRange(me, spell), 5.0f);
                a.radius = s.radius;
                a.manaPerTarget = s.manaPerTarget;
                a.durationSec = std::max(0, spell->GetDuration()) / 1000.0f;
                a.cooldownMs = spell->GetRecoveryTime();
                if (Powers(spell->powerType) == POWER_MANA)
                    a.manaCost = uint32(Spell::CalculatePowerCost(spell, me));
                a.gcd = spell->StartRecoveryTime > 0;
                a.ready = f.CanAct() && !(f.silenced && spell->PreventionType == SPELL_PREVENTION_TYPE_SILENCE) &&
                          ai->IsReadyWithStance(spell);
                f.allyBoosts.push_back(a);
            }
        }

        // Paladin blessings and party auras: ClassKit::Blessings / PartyAuras.
        void FillPaladinBuffs(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            std::vector<SpellEntry const*> const blessings = ai->Kit().Blessings();
            for (SpellEntry const* spell : blessings)
            {
                BlessingKind const kind = PaladinBuffs::BlessingOf(spell);
                if (kind != BlessingKind::None && !PaladinBuffs::IsGreaterBlessing(spell))
                    f.blessingSpells.push_back({ spell->Id, uint8(kind) });
            }
            // A Greater Blessing goes with its plain one (none learned without it).
            for (SpellEntry const* spell : blessings)
            {
                if (!PaladinBuffs::IsGreaterBlessing(spell))
                    continue;
                uint8 const kind = uint8(PaladinBuffs::BlessingOf(spell));
                for (PaladinBuffAbility& a : f.blessingSpells)
                    if (a.kind == kind)
                        a.greaterSpellId = spell->Id;
            }
            for (SpellEntry const* spell : ai->Kit().PartyAuras())
            {
                PaladinAuraKind const kind = PaladinBuffs::AuraOf(spell);
                if (kind != PaladinAuraKind::None)
                    f.auraSpells.push_back({ spell->Id, uint8(kind) });
            }
            for (SpellEntry const* spell : ai->Kit().JudgementSeals())
            {
                JudgementKind const kind = PaladinBuffs::SealOf(spell);
                if (kind != JudgementKind::None)
                    f.judgementSeals.push_back({ spell->Id, uint8(kind) });
            }
        }

        // Damage curses: ClassKit::Curses.
        void FillCurses(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            for (SpellEntry const* spell : ai->Kit().Curses())
            {
                CurseKind const kind = WarlockCurses::KindOf(spell);
                if (kind != CurseKind::None)
                    f.curseSpells.push_back({ spell->Id, uint8(kind) });
            }
        }

        // Party totems: ClassKit::Totems.
        void FillTotems(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            for (SpellEntry const* spell : ai->Kit().Totems())
            {
                TotemKind const kind = ShamanTotems::KindOf(spell);
                if (kind != TotemKind::None)
                    f.totemSpells.push_back({ spell->Id, uint8(kind) });
            }
        }

        // Damage cooldowns: ClassKit::Bursts and the racial ones (RaceKit::Bursts).
        void FillBursts(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            std::vector<BurstSpell> spells = ai->Kit().Bursts();
            std::vector<BurstSpell> const racial = RaceKit::Bursts(*ai);
            spells.insert(spells.end(), racial.begin(), racial.end());
            for (BurstSpell const& s : spells)
            {
                SpellEntry const* spell = s.spell;
                if (!spell || (s.then == nullptr && s.extraSec > 0.0f))
                    continue;   // a pairing whose cast is not learned
                BurstAbility a;
                a.spellId = spell->Id;
                a.thenSpellId = s.then ? s.then->Id : 0;
                a.damageMult = s.damageMult;
                a.extraSec = s.extraSec;
                float const own = std::max(0, spell->GetDuration()) / 1000.0f;
                a.durationSec = s.durationSec > 0.0f ? s.durationSec : (own > 0.0f ? own : 10.0f);
                a.damageTakenUp = s.damageTakenUp;
                a.cooldownMs = spell->GetRecoveryTime();
                a.gcd = spell->StartRecoveryTime > 0;
                a.minEnemiesNear = s.minEnemiesNear;
                a.minAttackersOnSelf = s.minAttackersOnSelf;
                a.minManaPct = s.minManaPct;
                a.notTank = s.notTank;
                a.healTakenCut = s.healTakenCut;
                a.needsPet = s.needsPet;
                a.needsCombo = s.needsCombo;
                a.thenNotOnTarget = s.thenNotOnTarget;
                a.ready = f.CanAct() && !(f.silenced && spell->PreventionType == SPELL_PREVENTION_TYPE_SILENCE) &&
                          ai->IsReadyWithStance(spell);
                f.bursts.push_back(a);
            }
        }

        // PartyMemberFacts::areaDpsPerTarget / areaRadius / fillerDps, on the bot's victim
        // (the pull's control valuation, ControlValue::OfPull).
        void FillAreaPotential(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            Player* me = ai->me;
            CombatBotRoles const role = ai->GetRole();
            if (role != ROLE_RANGE_DPS && role != ROLE_MELEE_DPS)
                return;
            // Out of combat (a pull being planned): valued on the bot itself (spell power, no
            // resistances of a target).
            Unit* victim = me->GetVictim() ? me->GetVictim() : static_cast<Unit*>(me);
            std::vector<AreaDamageSpell> const spells = ai->Kit().AreaDamage();
            SpellEntry const* filler = ai->Kit().Filler();
            if (spells.empty() || !filler)
                return;
            f.fillerDps = DamageSpells::Estimate(me, victim, filler) / std::max(0.1f, DamageSpells::BusySec(me, filler));
            for (AreaDamageSpell const& a : spells)
            {
                if (!a.spell || !me->IsSpellReady(a.spell))
                    continue;
                // A mage out of mana gives up no Blizzard when a pack member is sheeped or
                // shackled (ControlValue::OfPull's aoe; strat-20261108-141921: charged at 30 mana).
                if (DamageSpells::ManaCost(me, a.spell) > float(me->GetPower(POWER_MANA)))
                    continue;
                float const dps = DamageSpells::Estimate(me, victim, a.spell) / std::max(0.1f, DamageSpells::BusySec(me, a.spell));
                if (dps <= f.areaDpsPerTarget)
                    continue;
                f.areaDpsPerTarget = dps;
                float radius = 0.0f;
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (SpellRadiusEntry const* r = sSpellRadiusStore.LookupEntry(a.spell->EffectRadiusIndex[i]))
                        radius = std::max(radius, r->Radius);
                f.areaRadius = radius;
            }
        }

        // A channel's ticks (amplitude of its periodic effect), anything else: once after its busy time.
        void TicksOf(Unit const* caster, SpellEntry const* spell, AreaSpellFacts& a)
        {
            a.ticks = 1;
            a.periodSec = DamageSpells::BusySec(caster, spell);
            a.channel = spell->IsChanneledSpell();
            if (!a.channel)
                return;
            int32 const duration = spell->GetDuration();
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                if (spell->EffectAmplitude[i] == 0 || duration <= 0)
                    continue;
                a.ticks = std::max<uint32>(1, uint32(duration) / spell->EffectAmplitude[i]);
                a.periodSec = spell->EffectAmplitude[i] / 1000.0f;
                break;
            }
        }

        // Area phase facts (PartyMemberFacts::areaSpells, areaChannel, boltTarget, selfShield,
        // tapManaPerHealth): what Perception/Values/AreaValue.h needs without the live units.
        void FillAreaPhase(TeamBotAI const* ai, PartyMemberFacts& f)
        {
            Player* me = ai->me;
            CombatBotRoles const role = ai->GetRole();
            if (role == ROLE_RANGE_DPS || role == ROLE_MELEE_DPS)
            {
                Unit* victim = me->GetVictim() ? me->GetVictim() : static_cast<Unit*>(me);
                for (AreaDamageSpell const& k : ai->Kit().AreaDamage())
                {
                    SpellEntry const* spell = k.spell;
                    if (!spell)
                        continue;
                    AreaSpellFacts a;
                    a.spellId = spell->Id;
                    a.onTarget = k.onTarget;
                    a.slowPct = k.slowPct;
                    TicksOf(me, spell, a);
                    a.perTick = DamageSpells::Estimate(me, victim, spell) / float(a.ticks);
                    a.tickThreat = me->ApplyTotalThreatModifier(a.perTick * sSpellMgr.GetSpellThreatMultiplier(spell),
                                                                spell->GetSpellSchoolMask());
                    a.mana = DamageSpells::ManaCost(me, spell);
                    a.radius = SpellTags::AreaRadius(spell);
                    if (k.onTarget)
                        a.range = CombatBotBaseAI::SpellMaxRange(me, spell);    // Arctic Reach: Blizzard 36 yd
                    a.ready = f.CanAct() && !f.silenced && me->IsSpellReady(spell) &&
                              a.mana <= float(me->GetPower(POWER_MANA));
                    f.areaSpells.push_back(a);
                }

                // Its own area channel running now.
                if (Spell const* channel = me->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
                {
                    if (channel->getState() == SPELL_STATE_CASTING && channel->m_spellInfo)
                    {
                        uint32 const first = sSpellMgr.GetFirstSpellInChain(channel->m_spellInfo->Id);
                        for (AreaSpellFacts const& a : f.areaSpells)
                        {
                            if (sSpellMgr.GetFirstSpellInChain(a.spellId) != first || !a.channel)
                                continue;
                            f.areaChannel.spellId = channel->m_spellInfo->Id;
                            f.areaChannel.centre = f.pos;
                            float x = 0.0f, y = 0.0f, z = 0.0f;
                            channel->m_targets.getDestination(x, y, z);
                            if (x != 0.0f || y != 0.0f)
                                f.areaChannel.centre = Position3{ x, y, z };
                            uint32 const periodMs = std::max<uint32>(1, uint32(a.periodSec * 1000.0f));
                            f.areaChannel.ticksLeft = (uint32(std::max(0, int32(channel->GetCastedTime()))) + periodMs - 1) / periodMs;
                            break;
                        }
                    }
                }

                // Its bolt in the air (cast done, not landed: the generic slot in
                // SPELL_STATE_DELAYED; a channel started after it leaves it there). Not on the
                // threat list yet: a Frostbolt landing after the decision cancelled a Blizzard
                // after its first tick (strat-20261107-194655).
                if (Spell const* bolt = me->GetCurrentSpell(CURRENT_GENERIC_SPELL))
                {
                    if (bolt->getState() == SPELL_STATE_DELAYED && bolt->m_spellInfo)
                    {
                        if (Unit const* target = bolt->m_targets.getUnitTarget())
                        {
                            f.boltTarget = target->GetObjectGuid();
                            float const damage = DamageSpells::Estimate(me, target, bolt->m_spellInfo);
                            f.boltThreat = me->ApplyTotalThreatModifier(damage * sSpellMgr.GetSpellThreatMultiplier(bolt->m_spellInfo),
                                                                        bolt->m_spellInfo->GetSpellSchoolMask());
                        }
                    }
                }

                if (SpellEntry const* shield = ai->Kit().SelfShield())
                {
                    SelfShieldFacts& s = f.selfShield;
                    s.spellId = shield->Id;
                    for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                        if (shield->EffectApplyAuraName[i] == SPELL_AURA_SCHOOL_ABSORB)
                            s.absorb = float(std::max(0, shield->CalculateSimpleValue(SpellEffectIndex(i))));
                    if (Powers(shield->powerType) == POWER_MANA)
                        s.manaCost = uint32(Spell::CalculatePowerCost(shield, me));
                    s.up = me->HasAura(shield->Id);
                    // Facts only: cooldown, mana, form, able to act (not the brain's constraints:
                    // e.g. the threat opening's hold until 2026-11-13, strat-20261112-204141).
                    s.ready = f.CanAct() && !f.silenced && ai->IsReadyWithStance(shield);
                    int32 const left = me->GetSpellCooldownLeft(shield);
                    s.cooldownSec = left < 0 ? -1.0f : float(left) / 1000.0f;
                }
            }

            // Life Tap: health -> mana (EffectBasePoints on SPELL_EFFECT_DUMMY, the script
            // gives that mana for that health; talents not counted).
            if (SpellEntry const* tap = ai->Kit().HealthToMana())
            {
                float const amount = float(std::max(0, tap->CalculateSimpleValue(EFFECT_INDEX_0)));
                if (amount > 0.0f)
                {
                    f.tapManaPerHealth = 1.0f;
                    f.tapManaPerCast = amount;
                }
            }
        }

        // White damage per second of one hand from the unit's damage fields (attack power included).
        float WeaponDps(Unit const* unit, WeaponAttackType type)
        {
            uint32 const attackMs = unit->GetAttackTime(type);
            if (!attackMs)
                return 0.0f;
            uint16 minField = UNIT_FIELD_MINDAMAGE;
            uint16 maxField = UNIT_FIELD_MAXDAMAGE;
            if (type == OFF_ATTACK)
            {
                minField = UNIT_FIELD_MINOFFHANDDAMAGE;
                maxField = UNIT_FIELD_MAXOFFHANDDAMAGE;
            }
            else if (type == RANGED_ATTACK)
            {
                minField = UNIT_FIELD_MINRANGEDDAMAGE;
                maxField = UNIT_FIELD_MAXRANGEDDAMAGE;
            }
            float const hit = (unit->GetFloatValue(minField) + unit->GetFloatValue(maxField)) / 2.0f;
            return hit / (attackMs / 1000.0f);
        }

        // PartyMemberFacts::soloDps: what this member deals per second on its own (see there).
        // `ai` null = a player: weapons only (its spells are not known).
        float SoloDps(Player* member, TeamBotAI const* ai)
        {
            float melee = WeaponDps(member, BASE_ATTACK);
            if (member->HaveOffhandWeapon())
                melee += WeaponDps(member, OFF_ATTACK) * 0.5f;      // off-hand penalty
            if (member->GetClass() == CLASS_HUNTER)
                melee = std::max(melee, WeaponDps(member, RANGED_ATTACK));
            float best = melee * Value::MELEE_ABILITY_FACTOR;
            if (ai)
                if (SpellEntry const* filler = ai->Kit().Filler())
                    best = std::max(best, DamageSpells::Estimate(member, member, filler) /
                                          std::max(0.1f, DamageSpells::BusySec(member, filler)) * Value::CASTER_UPTIME);
            return best;
        }

        // ClassKit::Filler's mana per second while chain casting it (PartyMemberFacts::fillerManaPerSec).
        float FillerManaPerSec(TeamBotAI const* ai)
        {
            SpellEntry const* filler = ai->Kit().Filler();
            if (!filler || Powers(filler->powerType) != POWER_MANA)
                return 0.0f;
            float const cost = float(Spell::CalculatePowerCost(filler, ai->me));
            float const castSec = std::max(filler->GetCastTime(ai->me) / 1000.0f, 1.5f);
            return cost / castSec;
        }

        // Seconds until `caster` puts `spellId` back after a dispel, 0 = it does not (dead, not
        // hitting the member, or its profile does not reapply the spell).
        float ReapplySec(Creature const* caster, Unit const* member, uint32 spellId)
        {
            if (!caster || !caster->IsAlive() ||
                (caster->GetVictim() != member && !caster->CanReachWithMeleeAutoAttack(member)))
                return 0.0f;
            std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(caster->GetEntry());
            CreatureSpellRef const* ref = profile ? profile->FindSpell(spellId) : nullptr;
            if (!ref || !ref->reapplies)
                return 0.0f;
            float const fallback = 4.0f;    // on-hit chance or repeat delay unknown
            if (ref->source == SpellSource::OnHit)
            {
                float const attackSec = caster->GetAttackTime(BASE_ATTACK) / 1000.0f;
                if (attackSec <= 0.0f)
                    return fallback;
                float const chance = (ref->procChance > 0 && ref->procChance <= 100) ? ref->procChance / 100.0f : 1.0f;
                return attackSec / chance;
            }
            if (ref->repeatMaxMs)
                return (ref->repeatMinMs + ref->repeatMaxMs) / 2000.0f;
            return fallback;
        }

        // Paladin blessings on `member`, and the party aura a paladin member keeps up itself.
        void FillPaladinBuffsOn(Player const* member, PartyMemberFacts& f)
        {
            if (!member->IsAlive())
                return;
            ObjectGuid const self = member->GetObjectGuid();
            bool const paladin = member->GetClass() == CLASS_PALADIN;
            for (auto const& itr : member->GetSpellAuraHolderMap())
            {
                SpellAuraHolder const* holder = itr.second;
                SpellEntry const* spell = holder->GetSpellProto();
                if (spell->SpellFamilyName != SPELLFAMILY_PALADIN || !holder->IsPositive())
                    continue;
                BlessingKind const blessing = PaladinBuffs::BlessingOf(spell);
                if (blessing != BlessingKind::None)
                {
                    ObjectGuid const caster = holder->GetCasterGuid();
                    if (std::none_of(f.blessingsOn.begin(), f.blessingsOn.end(), [&](BlessingOnFacts const& b)
                        { return b.kind == uint8(blessing) && b.caster == caster; }))
                        f.blessingsOn.push_back({ uint8(blessing), caster });
                    continue;
                }
                if (paladin && holder->GetCasterGuid() == self)
                {
                    PaladinAuraKind const aura = PaladinBuffs::AuraOf(spell);
                    if (aura != PaladinAuraKind::None)
                        f.ownAura = uint8(aura);
                }
            }
        }

        // Harmful dispellable auras creatures put on `member` (players: pets are not dispelled).
        void FillDebuffs(Player const* member, PartyMemberFacts& f)
        {
            if (!member->IsAlive())
                return;
            Map* map = member->GetMap();
            for (auto const& itr : member->GetSpellAuraHolderMap())
            {
                SpellAuraHolder const* holder = itr.second;
                SpellEntry const* spell = holder->GetSpellProto();
                if (holder->IsPositive() || !spell->Dispel || !holder->GetCasterGuid().IsCreature())
                    continue;
                if (std::any_of(f.debuffs.begin(), f.debuffs.end(), [spell](DebuffFacts const& d) { return d.spellId == spell->Id; }))
                    continue;   // one entry per spell (an aura holder per effect group)
                DebuffFacts d;
                d.spellId = spell->Id;
                d.dispelType = uint8(spell->Dispel);
                d.caster = holder->GetCasterGuid();
                Creature const* caster = map->GetCreature(d.caster);
                d.casterLevel = caster ? caster->GetLevel() : member->GetLevel();
                d.remainingMs = uint32(std::max(0, holder->GetAuraDuration()));
                d.stacks = std::max<uint32>(1, holder->GetStackAmount());
                d.reapplySec = ReapplySec(caster, member, spell->Id);
                f.debuffs.push_back(d);
            }
        }

        // Poison, disease, bleed on `member` from any caster (Stoneform: StoneformBehavior).
        void FillAfflictions(Player const* member, PartyMemberFacts& f)
        {
            if (!member->IsAlive())
                return;
            for (auto const& itr : member->GetSpellAuraHolderMap())
            {
                SpellAuraHolder const* holder = itr.second;
                if (holder->IsPositive())
                    continue;
                SpellEntry const* spell = holder->GetSpellProto();
                f.poisoned |= spell->Dispel == DISPEL_POISON;
                f.diseased |= spell->Dispel == DISPEL_DISEASE;
                for (uint32 i = 0; i < MAX_EFFECT_INDEX && !f.bleeding; ++i)
                    if (holder->GetAuraByEffectIndex(SpellEffectIndex(i)))
                        f.bleeding = spell->GetEffectMechanic(SpellEffectIndex(i)) == MECHANIC_BLEED;
            }
        }
    }

    void RefreshCombat(TeamBotAI* bot, WorldModel& world, CoveragePlan const& coverage)
    {
        Player* me = bot->me;
        Map* map = me->GetMap();
        // The team (Commander/Team.h): a WoW group, or a population unit (a field bot alone too).
        std::vector<Player*> const members = Team::Members(me);
        Group* group = me->GetGroup();      // raid marks only

        world.party.clear();
        world.corpses.clear();
        world.enemies.clear();
        world.targeted.clear();
        world.leader = Team::LeaderOf(me);
        world.humanInTeam = std::any_of(members.begin(), members.end(), [](Player* p)
            { return p && !(p->AI() && dynamic_cast<TeamBotAI const*>(p->AI())); });
        world.seeksFights = std::all_of(members.begin(), members.end(), [](Player* p)
        {
            TeamBotAI const* ai = p && p->AI() ? dynamic_cast<TeamBotAI const*>(p->AI()) : nullptr;
            return !ai || ai->SeeksFights();
        });
        world.mapKind = map->IsRaid() ? MapKind::Raid : map->IsDungeon() ? MapKind::Dungeon : MapKind::World;
        if (members.empty())
        {
            world.allies.clear();
            world.castStartLog.clear();
            return;
        }

        // Party members and pets on this map.
        std::vector<Unit*> partyUnits;
        std::vector<Player*> sightCheckers;     // bots with a ranged interrupt that can act
        std::vector<Player*> tauntCheckers;     // tank bots with a taunt that can act
        for (Player* member : members)
        {
            if (!member || !member->IsInWorld() || member->GetMap() != map)
                continue;

            PartyMemberFacts f;
            FillUnitFacts(member, f);
            FillPendingHit(member, f);
            FillPendingHeal(member, f);
            f.cap = RoleCapabilities::Assess(member);
            FillDebuffs(member, f);
            FillAfflictions(member, f);
            FillPaladinBuffsOn(member, f);
            FillRevive(member, member->AI() ? dynamic_cast<TeamBotAI const*>(member->AI()) : nullptr, f);
            if (member->AI())
            {
                if (TeamBotAI const* ai = dynamic_cast<TeamBotAI const*>(member->AI()))
                {
                    f.isBot = true;
                    if (member->IsAlive())
                        FillSoulstone(ai, f);
                    f.role = uint8(ai->m_role);
                    f.roleLocked = ai->IsRoleLocked();
                    f.functions = BotFunctions(f, coverage);
                    f.drinkManaPerSec = ai->DrinkManaPerSec();
                    f.foodHealthPerSec = ai->FoodHealthPerSec();
                    f.drinking = ai->IsDrinking();
                    f.eating = ai->IsEating();
                    f.canShootWand = ai->CanShootWand();
                    f.healerDamageSet = ai->m_healerDamageTier;
                    if (member->IsAlive())
                    {
                        FillInterrupts(ai, f);
                        FillCastFailures(ai, f);
                        FillDispels(ai, f);
                        FillControls(ai, f);
                        FillFleeStops(ai, f);
                        FillTaunts(ai, f);
                        FillMitigations(ai, f);
                        FillThreatDrops(ai, f);
                        FillSurvival(ai, f);
                        FillItemSurvival(ai, f);
                        FillManaItems(ai, f);
                        FillBursts(ai, f);
                        FillAllyBoosts(ai, f);
                        FillCombatRes(ai, f);
                        FillPaladinBuffs(ai, f);
                        FillCurses(ai, f);
                        FillTotems(ai, f);
                        f.fillerManaPerSec = FillerManaPerSec(ai);
                        if (SpellEntry const* filler = ai->Kit().Filler())
                        {
                            if (!filler->IsChanneledSpell() && filler->GetCastTime(member) > 0)
                            {
                                f.fillerCastMs = uint32(filler->GetCastTime(member));
                                f.fillerSpeed = filler->speed;
                            }
                        }
                        FillAreaPotential(ai, f);
                        FillAreaPhase(ai, f);
                        if (f.DealsDamage())
                            f.soloDps = SoloDps(member, ai);
                    }
                    f.threatMod = member->ApplyTotalThreatModifier(1.0f, SPELL_SCHOOL_MASK_NORMAL);
                    if (member->IsAlive() && f.HealsTeam())
                    {
                        f.healRates = HealCapacity::Rates(ai);
                        f.healThreat = HealThreat(member, f.healRates);
                    }
                    else if (member->IsAlive() && f.cap.heal >= RoleCapabilities::MIN_HEAL)
                        f.spareHealRates = HealCapacity::Rates(ai);
                    else if (!member->IsAlive() && f.selfRes && f.HealsTeam())
                        f.spareHealRates = HealCapacity::Rates(ai);     // its heals once it rises
                    for (RaidTargetIcon mark : ai->m_marksToFocus)
                    {
                        ObjectGuid const marked = group ? group->GetTargetWithIcon(mark) : ObjectGuid();
                        if (!marked.IsUnit())
                            continue;
                        Unit const* unit = map->GetUnit(marked);
                        if (unit && unit->IsAlive())
                        {
                            f.focusMarkEnemy = marked;
                            break;
                        }
                    }
                    if (f.CanAct() && HasRangedInterrupt(f))
                        sightCheckers.push_back(member);
                    if (f.CanAct() && !f.taunts.empty())
                        tauntCheckers.push_back(member);
                }
            }
            if (!f.isBot && member->IsAlive())
                f.soloDps = SoloDps(member, nullptr);
            if (!f.isBot && member->IsAlive() && f.cap.heal >= RoleCapabilities::MIN_HEAL)
                f.spareHealRates = HealCapacity::PlayerRates(member);
            world.party.push_back(f);
            partyUnits.push_back(member);

            if (Pet* pet = member->GetPet())
            {
                if (pet->IsInWorld() && pet->GetMap() == map)
                {
                    PartyMemberFacts pf;
                    FillUnitFacts(pet, pf);
                    FillPendingHit(pet, pf);
                    pf.isPet = true;
                    pf.ownerGuid = member->GetObjectGuid();
                    pf.tauntsItself = PetTaunt::HasAutocast(pet);
                    // Only a bot's pet: a player's pet is his to command.
                    if (pet->IsControlled() && !pet->IsCharmed() && member->AI() &&
                        dynamic_cast<TeamBotAI const*>(member->AI()))
                    {
                        std::vector<SpellEntry const*> threatSpells;
                        PetTaunt::Spells(pet, threatSpells);
                        pf.canOffTank = !threatSpells.empty();
                        if (pet->IsAlive())
                            FillPetAbilities(pet, pf);
                    }
                    world.party.push_back(pf);
                    partyUnits.push_back(pet);
                }
            }
        }

        FillPlayerRoles(world);
        FillCorpses(members, map, world);

        // Every creature that has one of us on its threat list.
        std::vector<Creature*> enemies;
        for (Unit* unit : partyUnits)
            CollectAttackers(unit, map, enemies, std::numeric_limits<size_t>::max(), nullptr);
        size_t const partyEnemyCount = enemies.size();

        // Allies. Candidates: the last grid scan (RefreshPacks, 1 s), the allies of the previous
        // refresh, and non-party creatures on our enemies' threat lists (an ally that just joined
        // our fight is seen at once). All re-checked here, so the 1 s scan only delays allies
        // that fight something we are not fighting yet.
        Player* leader = bot->GetPartyLeader();
        Unit* reference = (leader && leader->IsInWorld() && leader->GetMap() == map) ? static_cast<Unit*>(leader) : me;
        std::vector<ObjectGuid> allyGuids = world.allyCandidates;
        for (AllyFacts const& a : world.allies)
            allyGuids.push_back(a.guid);
        for (Creature* enemy : enemies)
            for (HostileReference* ref : enemy->GetThreatManager().getThreatList())
                if (Unit* target = ref->getTarget())
                    if (target->IsCreature())
                        allyGuids.push_back(target->GetObjectGuid());
        world.allies.clear();

        std::vector<Creature*> allies;
        for (ObjectGuid const& guid : allyGuids)
        {
            Creature* c = map->GetCreature(guid);
            if (!c || std::find(allies.begin(), allies.end(), c) != allies.end())
                continue;
            if (IsAllyCandidate(c, reference, reference))
                allies.push_back(c);
        }
        std::sort(allies.begin(), allies.end(), [reference](Creature* a, Creature* b)
        {
            return reference->GetDistance(a) < reference->GetDistance(b);
        });
        if (allies.size() > MAX_ALLIES)
            allies.resize(MAX_ALLIES);

        // tankLike: compared with the party tank (tank-role bot), else the sturdiest member.
        uint32 tankMaxHealth = 0;
        uint32 sturdiest = 0;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet)
                continue;
            sturdiest = std::max(sturdiest, m.maxHealth);
            if (Tanks::IsTank(m))
                tankMaxHealth = std::max(tankMaxHealth, m.maxHealth);
        }
        if (!tankMaxHealth)
            tankMaxHealth = sturdiest;

        world.allies.reserve(allies.size());
        for (Creature* c : allies)
        {
            AllyFacts a;
            FillAlly(c, a);
            a.tankLike = a.elite && tankMaxHealth && float(a.maxHealth) >= ALLY_TANK_HEALTH_MULT * float(tankMaxHealth);
            world.allies.push_back(a);
        }

        // Creatures fighting an ally and none of us: same reverse lookup as above, on the allies.
        for (Creature* ally : allies)
            CollectAttackers(ally, map, enemies, partyEnemyCount + MAX_ALLY_ENEMIES, me);

        world.enemies.reserve(enemies.size());
        for (size_t i = 0; i < enemies.size(); ++i)
        {
            EnemyFacts e;
            FillEnemy(enemies[i], partyUnits, e);
            e.onAllyOnly = i >= partyEnemyCount;
            for (DamageDealt::Rate const& r : DamageDealt::Query(e.guid, WorldTimer::getMSTime()))
                e.dealtBy.push_back({ r.attacker, r.dps });
            // Line of sight for InterruptAssigner: only casts it may assign (interruptible, on
            // our lists), one check per (bot, caster) per refresh.
            if (e.castingSpellId && e.castInterruptible && !e.onAllyOnly)
                for (Player* checker : sightCheckers)
                    if (!checker->IsWithinLOSInMap(enemies[i]))
                        e.outOfSight.push_back(checker->GetObjectGuid());
            // And for team rescue (RescueValue::Taunt): a tank's taunt on an enemy hitting a
            // member who is not a tank.
            if (!e.onAllyOnly && !tauntCheckers.empty())
                if (PartyMemberFacts const* victim = world.FindMember(e.victim))
                    if (!Tanks::IsTank(*victim))
                        for (Player* checker : tauntCheckers)
                            if (e.InSightOf(checker->GetObjectGuid()) && !checker->IsWithinLOSInMap(enemies[i]))
                                e.outOfSight.push_back(checker->GetObjectGuid());
            world.enemies.push_back(std::move(e));
        }

        // Creatures one of us attacks that are not on our lists yet (WorldModel::targeted).
        for (Unit* unit : partyUnits)
        {
            Unit* victim = unit->GetVictim();
            Creature* c = victim ? victim->ToCreature() : nullptr;
            if (!c || !c->IsInWorld() || c->GetMap() != map || !c->IsAlive() || !me->IsValidAttackTarget(c))
                continue;
            if (std::find(enemies.begin(), enemies.end(), c) != enemies.end() || world.FindTargeted(c->GetObjectGuid()))
                continue;
            EnemyFacts e;
            FillEnemy(c, partyUnits, e);
            world.targeted.push_back(std::move(e));
        }

        UpdateCastStarts(world, WorldTimer::getMSTime());
    }

    namespace
    {
        void FillPatrolTrack(Creature* creature, PatrolFacts& pf)
        {
            float const horizon = std::min(PATROL_TRACK_MAX_SEC,
                                           std::max(PATROL_TRACK_MIN_SEC, PatrolTracker::LapSec(creature)));
            PatrolTracker::Track const track = PatrolTracker::Predict(creature, horizon, PATROL_TRACK_STEP_SEC);
            pf.trackStepSec = PATROL_TRACK_STEP_SEC;
            pf.track.reserve(track.points.size());
            for (PatrolTracker::TrackPoint const& p : track.points)
                pf.track.push_back(p.pos);
        }
    }

    void RefreshPacks(TeamBotAI* bot, WorldModel& world, Player* guide)
    {
        Player* me = bot->me;
        Player* leader = bot->GetPartyLeader();
        // Where the party is going: the guide it follows, else the leader; a team of bots
        // (Commander/Team.h) goes where its bot goes (it finds its own fights: HuntAssigner).
        Player* walker = guide ? guide
            : (leader && leader->IsInWorld() && leader->GetMap() == me->GetMap()) ? leader
            : !world.humanInTeam ? me : nullptr;
        WorldObject* center = walker ? static_cast<WorldObject*>(walker) : static_cast<WorldObject*>(me);

        // One scan for both: packs within PACK_SCAN_RADIUS, patrols within PATROL_SCAN_RADIUS.
        float const scanRadius = std::max(PACK_SCAN_RADIUS, PATROL_SCAN_RADIUS);
        std::list<Unit*> units;
        MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(center, me, scanRadius);
        MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, check);
        Cell::VisitAllObjects(center, searcher, scanRadius);

        std::vector<PackCreature> creatures;
        world.patrols.clear();
        // Aggro range against the main tank while fighting: the fight is where the tank stands.
        bool const fighting = world.HasPartyEnemy();
        Unit const* reachAgainst = me;
        if (fighting)
            if (PartyMemberFacts const* tank = Tanks::Main(world, true))
                if (Unit const* u = me->GetMap()->GetUnit(tank->guid))
                    reachAgainst = u;
        for (Unit* unit : units)
        {
            if (!unit->IsCreature() || unit->IsInCombat() || unit->GetOwnerGuid() ||
                unit->GetCreatureType() == CREATURE_TYPE_CRITTER || !me->IsValidAttackTarget(unit))
                continue;

            Creature* creature = static_cast<Creature*>(unit);
            // Temporary summons are event waves, bombs, wards: not packs to pull.
            if (creature->IsPet() || creature->IsTotem() || creature->IsTemporarySummon())
                continue;
            // Guards, civilians, faction NPCs and PvP-flagged creatures: attacking them flags
            // the party for PvP or starts a town fight. Never offer them as pulls.
            if (creature->IsGuard() || creature->IsCivilian() || creature->IsPvP() || creature->IsContestedGuard())
                continue;
            if (FactionTemplateEntry const* faction = creature->GetFactionTemplateEntry())
                if (faction->ourMask & (FACTION_MASK_ALLIANCE | FACTION_MASK_HORDE))
                    continue;
            bool const inPackRange = center->IsWithinDistInMap(creature, PACK_SCAN_RADIUS);
            bool const patrol = creature->GetDefaultMovementType() == WAYPOINT_MOTION_TYPE;
            if (!inPackRange && !patrol)
                continue;

            PackCreature c;
            c.guid = creature->GetObjectGuid();
            if (CreatureGroup const* cg = creature->GetCreatureGroup())
                c.dbGroupLeader = cg->GetOriginalLeaderGuid();
            c.entry = creature->GetEntry();
            c.level = creature->GetLevel();
            c.elite = creature->IsElite();
            c.aggressive = me->IsHostileTo(creature);
            c.stealthed = creature->HasAuraType(SPELL_AURA_MOD_STEALTH);
            c.patrol = patrol;
            c.tappedByOther = creature->HasLootRecipient() && (!leader || !creature->IsTappedBy(leader));
            c.noAggro = creature->HasExtraFlag(CREATURE_FLAG_EXTRA_NO_AGGRO);
            c.creatureTypeMask = creature->GetCreatureTypeMask();
            c.reach = creature->GetAttackDistance(reachAgainst);
            c.health = creature->GetHealth();
            c.maxHealth = creature->GetMaxHealth();
            c.controlSpellId = IdleControl(creature);
            if (std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(c.entry))
            {
                c.boss = profile->boss;
                c.meleeDps = CreatureProfiles::EstimateMeleeDps(*profile, c.level);
                if (profile->mainRangedSpellId)
                    c.rangedDps = RangedSpellDps(*profile, c.level, CreatureProfiles::EstimateMeleeHit(*profile, c.level));
            }
            c.pos = PositionOf(creature);
            if (c.patrol)
            {
                PatrolFacts pf;
                static_cast<PackCreature&>(pf) = c;
                FillPatrolTrack(creature, pf);
                world.patrols.push_back(std::move(pf));
            }
            if (inPackRange)
                creatures.push_back(c);
        }

        world.packs = PackResolver::Resolve(creatures, PositionOf(center));
        // Walks from the leader or guide (the clear's order): only out of combat, when nothing
        // is pulled from here anyway and the paths are not worth the time.
        if (!fighting && walker)
            PackRoutes::Fill(walker, world.packs);

        // Ally candidates for the next RefreshCombat: friendly creatures in combat around the
        // leader. Grid objects only (players are party members or strangers, never allies).
        Unit* reference = (leader && leader->IsInWorld() && leader->GetMap() == me->GetMap())
            ? static_cast<Unit*>(leader) : static_cast<Unit*>(me);
        std::list<Unit*> friends;
        MaNGOS::AnyFriendlyUnitInObjectRangeCheck friendCheck(reference, ALLY_RANGE);
        MaNGOS::UnitListSearcher<MaNGOS::AnyFriendlyUnitInObjectRangeCheck> friendSearcher(friends, friendCheck);
        Cell::VisitGridObjects(reference, friendSearcher, ALLY_RANGE);

        world.allyCandidates.clear();
        for (Unit* unit : friends)
            if (unit->IsCreature() && IsAllyCandidate(static_cast<Creature const*>(unit), reference, reference))
                world.allyCandidates.push_back(unit->GetObjectGuid());
    }
}
}
