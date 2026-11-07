#include "Commander/Perception/WorldModelBuilder.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/SpellTags.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/DamageIntake.h"
#include "Commander/Perception/PatrolTracker.h"
#include "Commander/Perception/CreatureProfile.h"
#include "Commander/Perception/HealCapacity.h"
#include "Commander/Perception/PetTaunt.h"
#include "Commander/Perception/RescueValue.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Classes/ClassKit.h"
#include "DamageSpells.h"
#include "RoleCapability.h"
#include "World.h"
#include "PartyBotAI.h"
#include "DBCStores.h"
#include "Creature.h"
#include "CreatureGroups.h"
#include "Group.h"
#include "Pet.h"
#include "TemporarySummon.h"
#include "Player.h"
#include "Commander/Perception/ProfileStore.h"
#include "SpellMgr.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "ThreatManager.h"
#include "HostileRefManager.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "Timer.h"
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
            f.manaPct = unit->GetPowerType() == POWER_MANA && unit->GetMaxPower(POWER_MANA)
                ? unit->GetPowerPercent(POWER_MANA) : -1.0f;
            f.mana = unit->GetPower(POWER_MANA);
            f.maxMana = unit->GetMaxPower(POWER_MANA);
            f.pos = PositionOf(unit);
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
            f.controlled = f.charmed || unit->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL);
            f.silenced = unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SILENCED);
            ShapeshiftForm const form = unit->GetShapeshiftForm();
            f.tankStance = form == FORM_DEFENSIVESTANCE || form == FORM_BEAR || form == FORM_DIREBEAR ||
                           (f.classId == CLASS_PALADIN && unit->HasAura(SPELL_RIGHTEOUS_FURY));

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
            }
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

        void AddInterrupt(PartyBotAI const* ai, SpellEntry const* spell, PartyMemberFacts& f)
        {
            if (!spell || !SpellTags::StopsCasts(spell))
                return;
            Player* me = ai->me;

            InterruptAbility a;
            a.spellId = spell->Id;
            SpellRangeEntry const* range = sSpellRangeStore.LookupEntry(spell->rangeIndex);
            a.maxRange = std::max(range ? range->maxRange : 0.0f, 5.0f);
            a.cooldownMs = spell->GetRecoveryTime();
            a.lockoutMs = LockoutMs(spell);
            a.mechanicMask = SpellTags::StopMechanicMask(spell);
            a.needsFront = spell->Attributes == 0x150010;     // same test as Spell::CheckCast (Gouge)
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
                        e.partyDotMs = std::max(e.partyDotMs, left);
                }
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
            FillCast(creature, e);
            e.crowdControlled = creature->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL);
            e.creatureTypeMask = creature->GetCreatureTypeMask();
            FillControlState(creature, partyUnits, e);
            e.ignoresAssistThreat = IgnoresAssistThreat(creature);
            e.meleeEnabled = !creature->AI() || creature->AI()->IsMeleeAttackEnabled();
            e.meleePhysical = creature->GetCreatureInfo()->damage_school == SPELL_SCHOOL_NORMAL;
            if (!e.meleeEnabled)
                e.meleeDps = 0.0f;     // walking bombs, casters that stand back: no melee to count
            if (std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(e.entry))
            {
                // Standing back with melee off: it hurts with its main ranged spell instead.
                if (profile->mainRangedSpellId && !e.meleeEnabled)
                    e.rangedDps = RangedSpellDps(*profile, e.level, e.meleeHit);
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

        void FillInterrupts(PartyBotAI const* ai, PartyMemberFacts& f)
        {
            for (SpellEntry const* spell : ai->Kit().Interrupts())
                AddInterrupt(ai, spell, f);
        }

        void FillDispels(PartyBotAI const* ai, PartyMemberFacts& f)
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
                SpellRangeEntry const* range = sSpellRangeStore.LookupEntry(spell->rangeIndex);
                a.maxRange = range ? range->maxRange : 30.0f;
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

        void FillControls(PartyBotAI const* ai, PartyMemberFacts& f)
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
                SpellRangeEntry const* range = sSpellRangeStore.LookupEntry(spell->rangeIndex);
                a.maxRange = std::max(range ? range->maxRange : 0.0f, 5.0f);
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
                Powers const powerType = Powers(spell->powerType);
                a.ready = !f.controlled && me->IsSpellReady(spell) &&
                          (powerType == POWER_HEALTH || me->GetPower(powerType) >= Spell::CalculatePowerCost(spell, me)) &&
                          spell->GetErrorAtShapeshiftedCast(me->GetShapeshiftForm()) == SPELL_CAST_OK &&
                          HasReagents(me, spell) &&
                          !(f.silenced && spell->PreventionType == SPELL_PREVENTION_TYPE_SILENCE);
                f.controls.push_back(a);
            }
        }

        // Team rescue (Perception/RescueValue.h): a tank bot's taunts, one entry per spell.
        void FillTaunts(PartyBotAI const* ai, PartyMemberFacts& f)
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
                SpellRangeEntry const* range = sSpellRangeStore.LookupEntry(spell->rangeIndex);
                t.maxRange = std::max(range ? range->maxRange : 0.0f, 5.0f);
                // Taunt 355: RecoveryTime 0, CategoryRecoveryTime 10 s.
                t.cooldownMs = spell->GetRecoveryTime();
                t.holdSec = RescueValue::TemporaryTauntSec(spell);
                t.stanceSwap = ai->StanceSwapFor(spell) != nullptr;
                t.ready = f.CanAct() && ai->IsReadyWithStance(spell);
                f.taunts.push_back(t);
            }
        }

        // Fade (ClassKit::ThreatDrop) of a non-tank bot: SPELL_AURA_MOD_TOTAL_THREAT with a
        // negative amount on every threat list for the aura's duration.
        void FillThreatDrop(PartyBotAI const* ai, PartyMemberFacts& f)
        {
            SpellEntry const* spell = ai->Kit().ThreatDrop();
            if (!spell || ai->GetRole() == ROLE_TANK)
                return;
            Player* me = ai->me;
            ThreatDropAbility& a = f.threatDrop;
            a.spellId = spell->Id;
            a.drop = -float(spell->CalculateSimpleValue(EFFECT_INDEX_0));
            a.durationSec = std::max(0, spell->GetDuration()) / 1000.0f;
            a.cooldownMs = spell->GetRecoveryTime();
            if (Powers(spell->powerType) == POWER_MANA)
                a.manaCost = uint32(Spell::CalculatePowerCost(spell, me));
            a.ready = f.CanAct() && !f.silenced && ai->CanTryToCastSpell(me, spell);
        }

        // PartyMemberFacts::areaDpsPerTarget / areaRadius / fillerDps, on the bot's victim.
        void FillAreaPotential(PartyBotAI const* ai, PartyMemberFacts& f)
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

        // ClassKit::Filler's mana per second while chain casting it (PartyMemberFacts::fillerManaPerSec).
        float FillerManaPerSec(PartyBotAI const* ai)
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
    }

    void RefreshCombat(PartyBotAI* bot, WorldModel& world)
    {
        Player* me = bot->me;
        Map* map = me->GetMap();
        Group* group = me->GetGroup();

        world.party.clear();
        world.enemies.clear();
        world.targeted.clear();
        world.leader = group ? group->GetLeaderGuid() : ObjectGuid();
        if (!group)
        {
            world.allies.clear();
            world.castStartLog.clear();
            return;
        }

        // Party members and pets on this map.
        std::vector<Unit*> partyUnits;
        std::vector<Player*> sightCheckers;     // bots with a ranged interrupt that can act
        std::vector<Player*> tauntCheckers;     // tank bots with a taunt that can act
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
        {
            Player* member = itr->getSource();
            if (!member || !member->IsInWorld() || member->GetMap() != map)
                continue;

            PartyMemberFacts f;
            FillUnitFacts(member, f);
            f.cap = RoleCapabilities::Assess(member);
            FillDebuffs(member, f);
            if (member->AI())
            {
                if (PartyBotAI const* ai = dynamic_cast<PartyBotAI const*>(member->AI()))
                {
                    f.isBot = true;
                    f.role = uint8(ai->m_role);
                    f.roleLocked = ai->IsRoleLocked();
                    f.drinkManaPerSec = ai->DrinkManaPerSec();
                    f.foodHealthPerSec = ai->FoodHealthPerSec();
                    f.drinking = ai->IsDrinking();
                    if (member->IsAlive())
                    {
                        FillInterrupts(ai, f);
                        FillDispels(ai, f);
                        FillControls(ai, f);
                        FillTaunts(ai, f);
                        FillThreatDrop(ai, f);
                        f.fillerManaPerSec = FillerManaPerSec(ai);
                        FillAreaPotential(ai, f);
                    }
                    if (member->IsAlive() && f.role == ROLE_HEALER)
                        f.healRates = HealCapacity::Rates(ai);
                    for (RaidTargetIcon mark : ai->m_marksToFocus)
                    {
                        ObjectGuid const marked = group->GetTargetWithIcon(mark);
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
                    pf.tauntsItself = PetTaunt::HasAutocast(pet);
                    world.party.push_back(pf);
                    partyUnits.push_back(pet);
                }
            }
        }

        FillPlayerRoles(world);

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
            PatrolTracker::Track const track = PatrolTracker::Predict(creature, PATROL_FIGHT_HORIZON_SEC, PATROL_FIGHT_STEP_SEC);
            pf.trackStepSec = PATROL_FIGHT_STEP_SEC;
            pf.track.reserve(track.points.size());
            for (PatrolTracker::TrackPoint const& p : track.points)
                pf.track.push_back(p.pos);
        }
    }

    void RefreshPacks(PartyBotAI* bot, WorldModel& world)
    {
        Player* me = bot->me;
        Player* leader = bot->GetPartyLeader();
        WorldObject* center = (leader && leader->IsInWorld() && leader->GetMap() == me->GetMap())
            ? static_cast<WorldObject*>(leader) : static_cast<WorldObject*>(me);

        // One scan for both: packs within PACK_SCAN_RADIUS, patrols within PATROL_SCAN_RADIUS.
        float const scanRadius = std::max(PACK_SCAN_RADIUS, PATROL_SCAN_RADIUS);
        std::list<Unit*> units;
        MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(center, me, scanRadius);
        MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, check);
        Cell::VisitAllObjects(center, searcher, scanRadius);

        std::vector<PackCreature> creatures;
        world.patrols.clear();
        // Patrol tracks only matter while we fight (PatrolAssigner); the pull planner predicts
        // its own. Aggro range against the main tank: the fight is where the tank stands.
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
            if (std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(c.entry))
            {
                c.boss = profile->boss;
                c.meleeDps = CreatureProfiles::EstimateMeleeDps(*profile, c.level);
            }
            c.pos = PositionOf(creature);
            if (c.patrol)
            {
                PatrolFacts pf;
                static_cast<PackCreature&>(pf) = c;
                if (fighting)
                    FillPatrolTrack(creature, pf);
                world.patrols.push_back(std::move(pf));
            }
            if (inPackRange)
                creatures.push_back(c);
        }

        world.packs = PackResolver::Resolve(creatures, PositionOf(center));

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
