#include "Companions/Affinity/TraitEffects.h"
#include "Companions/CompanionMgr.h"
#include "Shared/FoodAndDrink.h"
#include "Player.h"
#include "Group.h"
#include "Map.h"
#include "Weather.h"
#include "ItemPrototype.h"
#include "ObjectMgr.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include <algorithm>
#include <ctime>

namespace Companions
{
    namespace TraitEffects
    {
        using Traits::Trait;

        namespace
        {
            uint32 constexpr LOOK_MS = 2 * IN_MILLISECONDS;
            uint32 constexpr WEATHER_LOOK_MS = 10 * IN_MILLISECONDS;

            // Life (design doc §4).
            float constexpr DILIGENT_MAX = 0.08f;               // regen, after a fight this long or more:
            uint32 constexpr DILIGENT_FULL_MS = 90 * IN_MILLISECONDS;
            float constexpr DAY_NIGHT_REGEN = 1.10f;            // NightOwl 20:00-6:00, EarlyBird the rest
            float constexpr RAIN_REGEN = 1.15f;
            float constexpr WELL_FED_DURATION = 2.0f;
            float constexpr FRUGAL_REPAIR = 0.90f;
            float constexpr FRUGAL_JUNK = 1.05f;
            float constexpr THRIFTY_SUPPLIES = 0.90f;
            float constexpr CAREFUL_DURABILITY = 0.5f;
            float constexpr LIGHT_FALL = 0.5f;
            float constexpr SWIMMER_BREATH = 2.0f;
            float constexpr SWIMMER_SPEED = 1.15f;
            float constexpr QUICK_HANDS_CAST = 0.70f;
            float constexpr CRAFTY_CHANCE = 0.05f;
            float constexpr SAVER_CHANCE = 0.05f;
            float constexpr GATHERER_CHANCE = 0.05f;
            float constexpr SCROUNGER_MONEY = 1.03f;
            float constexpr LIKEABLE_REPUTATION = 1.05f;
            float constexpr PATHFINDER_TRAVEL = 0.75f;
            float constexpr SWIFT_TRAVEL = 0.5f;
            float constexpr QUIET_DETECTED = 0.80f;

            // Combat (design doc §5).
            float constexpr TOUGH_RESIST = 20.0f;               // every magic school
            float constexpr CONSTITUTION_RESIST = 15.0f;        // one school
            float constexpr STURDY_HEALTH_PCT = 2.0f;
            float constexpr REVIVE_AT = 0.35f;
            float constexpr POISON_DURATION = 0.85f;
            float constexpr ONE_CONTROL_DURATION = 0.85f;       // Brave, Hardhead, SureFoot
            float constexpr ALL_CONTROL_DURATION = 0.90f;       // Composed
            float constexpr STEADY_PUSHBACK = 0.80f;
            float constexpr FOCUS_LOCKOUT = 0.5f;
            uint32 constexpr ALERT_DETECT = 15;                 // stealth detection skill (3 levels)
            float constexpr LAST_STAND_BELOW = 0.20f;           // health after the hit
            float constexpr LAST_STAND_SHIELD = 0.10f;          // of max health
            uint32 constexpr LAST_STAND_SHIELD_MS = 10 * IN_MILLISECONDS;
            uint32 constexpr LAST_STAND_EVERY_MS = HOUR * IN_MILLISECONDS;
            uint32 constexpr BREATHER_MS = 6 * IN_MILLISECONDS;
            float constexpr BREATHER_REGEN = 4.0f;
            float constexpr DESPERATE_BELOW = 30.0f;            // health percent
            float constexpr DESPERATE_TAKEN = 0.95f;
            float constexpr BACK_TO_WALL_TAKEN = 0.92f;
            uint32 constexpr BACK_TO_WALL_MS = 15 * IN_MILLISECONDS;
            uint32 constexpr BACK_TO_WALL_EVERY_MS = MINUTE * IN_MILLISECONDS;
            float constexpr ROOTED_ARMOR_PCT = 5.0f;
            uint32 constexpr ROOTED_STILL_MS = 3 * IN_MILLISECONDS;
            float constexpr DODGY_AOE_TAKEN = 0.92f;
            float constexpr GUARD_BIG_HIT = 0.20f;              // of max health
            float constexpr GUARD_TAKEN = 0.70f;
            uint32 constexpr GUARD_EVERY_MS = 2 * MINUTE * IN_MILLISECONDS;
            float constexpr GRIT_BELOW = 50.0f;                 // health percent
            float constexpr GRIT_HEALING = 1.08f;

            uint32 constexpr MAGIC_SCHOOLS[] = { SPELL_SCHOOL_FIRE, SPELL_SCHOOL_NATURE, SPELL_SCHOOL_FROST, SPELL_SCHOOL_SHADOW, SPELL_SCHOOL_ARCANE };

            uint32 constexpr Bit(uint32 mechanic) { return 1u << (mechanic - 1); }
            uint32 constexpr CONTROL_MECHANICS = Bit(MECHANIC_CHARM) | Bit(MECHANIC_DISORIENTED) | Bit(MECHANIC_FEAR) |
                Bit(MECHANIC_ROOT) | Bit(MECHANIC_SILENCE) | Bit(MECHANIC_SLEEP) | Bit(MECHANIC_SNARE) | Bit(MECHANIC_STUN) |
                Bit(MECHANIC_FREEZE) | Bit(MECHANIC_KNOCKOUT) | Bit(MECHANIC_POLYMORPH) | Bit(MECHANIC_BANISH) |
                Bit(MECHANIC_SHACKLE) | Bit(MECHANIC_HORROR);

            // (0 = never set: due)
            bool Due(uint32 nowMs, uint32 atMs)
            {
                return !atMs || nowMs >= atMs;
            }

            void SetResistance(Player* me, uint32 school, float amount, bool apply)
            {
                me->HandleStatModifier(UnitMods(UNIT_MOD_RESISTANCE_START + school), TOTAL_VALUE, amount, apply);
            }

            // Stat changes of a combat trait on (or off) him: not saved, gone with the AI's character.
            void ApplyStats(Player* me, Trait trait, uint32 school, bool apply)
            {
                switch (trait)
                {
                    case Trait::Tough:
                        for (uint32 s : MAGIC_SCHOOLS)
                            SetResistance(me, s, TOUGH_RESIST, apply);
                        break;
                    case Trait::Constitution:
                        SetResistance(me, school, CONSTITUTION_RESIST, apply);
                        break;
                    case Trait::Sturdy:
                        me->HandleStatModifier(UNIT_MOD_HEALTH, TOTAL_PCT, STURDY_HEALTH_PCT, apply);
                        break;
                    default:
                        break;
                }
            }

            bool HasStats(Trait trait)
            {
                return trait == Trait::Tough || trait == Trait::Constitution || trait == Trait::Sturdy;
            }

            uint32 DeadMembers(Player* me)
            {
                Group* group = me->GetGroup();
                if (!group)
                    return 0;
                uint32 dead = 0;
                for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                    if (Player const* member = ref->getSource())
                        if (member != me && member->IsInMap(me) && !member->IsAlive())
                            ++dead;
                return dead;
            }

            bool Rainy(Player const* me)
            {
                Map* map = me->GetMap();
                WeatherSystem* system = map ? map->GetWeatherSystem() : nullptr;
                Weather* weather = system ? system->FindOrCreateWeather(me->GetZoneId()) : nullptr;
                return weather && (weather->GetType() == WEATHER_TYPE_RAIN || weather->GetType() == WEATHER_TYPE_SNOW);
            }

            bool IsNight()
            {
                time_t const now = time(nullptr);
                tm local;
#ifdef _WIN32
                localtime_s(&local, &now);
#else
                localtime_r(&now, &local);
#endif
                return local.tm_hour >= 20 || local.tm_hour < 6;
            }

            // A tradeskill recipe, or gathering (herbs, ore, skinning).
            bool IsCraftOrGather(SpellEntry const* spell)
            {
                if (!spell)
                    return false;
                if (spell->HasAttribute(SPELL_ATTR_IS_TRADESKILL) || spell->HasEffect(SPELL_EFFECT_SKINNING))
                    return true;
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (spell->Effect[i] == SPELL_EFFECT_OPEN_LOCK &&
                        (spell->EffectMiscValue[i] == LOCKTYPE_HERBALISM || spell->EffectMiscValue[i] == LOCKTYPE_MINING))
                        return true;
                return false;
            }

            bool IsTradeskill(SpellEntry const* spell)
            {
                return spell && spell->HasAttribute(SPELL_ATTR_IS_TRADESKILL);
            }

            bool IsSupply(ItemPrototype const* proto)
            {
                using BotShared::FoodAndDrink::ItemSpell;
                using BotShared::FoodAndDrink::Kind;
                return proto && (proto->Class == ITEM_CLASS_PROJECTILE || proto->Class == ITEM_CLASS_REAGENT ||
                                 ItemSpell(proto, Kind::Food) || ItemSpell(proto, Kind::Drink));
            }
        }

        void Update(Player* me, State& state)
        {
            if (!me)
                return;
            uint32 const now = WorldTimer::getMSTime();

            if (Due(now, state.nextLookMs))
            {
                state.nextLookMs = now + LOOK_MS;
                TraitPair const traits = sCompanionMgr.GetTraits(me->GetGUIDLow());
                state.life = traits.life;
                state.combat = traits.combat;

                Trait const wanted = HasStats(state.combat) ? state.combat : Trait::None;
                if (wanted != state.statsOn)
                {
                    ApplyStats(me, state.statsOn, state.statsSchool, false);
                    state.statsOn = wanted;
                    state.statsSchool = Traits::ConstitutionSchool(me->GetGUIDLow());
                    ApplyStats(me, state.statsOn, state.statsSchool, true);
                }
                bool const swimmer = state.life == Trait::Swimmer;
                if (swimmer != state.swimSpeedOn)
                {
                    state.swimSpeedOn = swimmer;
                    me->UpdateSpeed(MOVE_SWIM, false);
                }
                if (state.life == Trait::RainLover && Due(now, state.nextWeatherMs))
                {
                    state.nextWeatherMs = now + WEATHER_LOOK_MS;
                    state.rainy = Rainy(me);
                }
            }

            // Fights.
            bool const inCombat = me->IsInCombat();
            if (inCombat && !state.inCombat)
                state.combatSinceMs = now;
            else if (!inCombat && state.inCombat)
            {
                state.lastFightMs = now - state.combatSinceMs;
                state.leftCombatMs = now;
            }
            state.inCombat = inCombat;

            if (state.combat == Trait::BackToWall)
            {
                uint32 const dead = DeadMembers(me);
                if (dead > state.deadMembers && me->IsAlive() && Due(now, state.backToWallReadyMs))
                {
                    state.backToWallUntilMs = now + BACK_TO_WALL_MS;
                    state.backToWallReadyMs = now + BACK_TO_WALL_EVERY_MS;
                }
                state.deadMembers = dead;
            }

            bool const rooted = state.combat == Trait::Rooted;
            if (rooted)
            {
                float const dx = me->GetPositionX() - state.stillX;
                float const dy = me->GetPositionY() - state.stillY;
                if (dx * dx + dy * dy > 0.25f || !state.stillSinceMs)
                {
                    state.stillX = me->GetPositionX();
                    state.stillY = me->GetPositionY();
                    state.stillSinceMs = now;
                }
            }
            bool const armor = rooted && me->IsAlive() && now - state.stillSinceMs >= ROOTED_STILL_MS;
            if (armor != state.armorOn)
            {
                state.armorOn = armor;
                me->HandleStatModifier(UNIT_MOD_ARMOR, TOTAL_PCT, ROOTED_ARMOR_PCT, armor);
            }
        }

        void ChangeDamageTaken(Player* me, State& state, uint32& damage, SpellEntry const* spell)
        {
            if (!damage || !me->IsAlive() || state.combat == Trait::None)
                return;
            uint32 const now = WorldTimer::getMSTime();
            uint32 const maxHealth = me->GetMaxHealth();
            float rate = 1.0f;
            switch (state.combat)
            {
                case Trait::Desperate:
                    if (me->GetHealthPercent() < DESPERATE_BELOW)
                        rate = DESPERATE_TAKEN;
                    break;
                case Trait::BackToWall:
                    if (state.backToWallUntilMs && now < state.backToWallUntilMs)
                        rate = BACK_TO_WALL_TAKEN;
                    break;
                case Trait::Dodgy:
                    if (spell && spell->IsAreaOfEffectSpell())
                        rate = DODGY_AOE_TAKEN;
                    break;
                case Trait::Guard:
                    if (damage > maxHealth * GUARD_BIG_HIT && Due(now, state.guardReadyMs))
                    {
                        rate = GUARD_TAKEN;
                        state.guardReadyMs = now + GUARD_EVERY_MS;
                    }
                    break;
                default:
                    break;
            }
            if (rate != 1.0f)
                damage = uint32(damage * rate + 0.5f);

            if (state.combat == Trait::LastStand && damage)
            {
                if (state.shieldLeft && now >= state.shieldUntilMs)
                    state.shieldLeft = 0;
                if (!state.shieldLeft && Due(now, state.lastStandReadyMs) &&
                    me->GetHealth() < damage + uint32(maxHealth * LAST_STAND_BELOW))
                {
                    state.shieldLeft = std::max<uint32>(1, uint32(maxHealth * LAST_STAND_SHIELD));
                    state.shieldUntilMs = now + LAST_STAND_SHIELD_MS;
                    state.lastStandReadyMs = now + LAST_STAND_EVERY_MS;
                }
                uint32 const absorbed = std::min(state.shieldLeft, damage);
                state.shieldLeft -= absorbed;
                damage -= absorbed;
            }
        }

        void ChangeHealingTaken(Player const* me, State const& state, uint32& heal)
        {
            if (state.combat == Trait::Grit && me->GetHealthPercent() < GRIT_BELOW)
                heal = uint32(heal * GRIT_HEALING + 0.5f);
        }

        void ChangeAuraDuration(State const& state, SpellEntry const* spell, bool positive, int32& duration)
        {
            if (!spell || duration <= 0)
                return;
            float rate = 1.0f;
            if (positive)
            {
                if (state.life == Trait::Appetite && Spells::GetSpellSpecific(spell->Id) == SPELL_WELL_FED)
                    rate = WELL_FED_DURATION;
            }
            else
            {
                uint32 const mechanics = spell->GetAllSpellMechanicMask();
                switch (state.combat)
                {
                    case Trait::PoisonResist:
                        if (spell->Dispel == DISPEL_POISON || spell->Dispel == DISPEL_DISEASE)
                            rate = POISON_DURATION;
                        break;
                    case Trait::Brave:
                        if (mechanics & Bit(MECHANIC_FEAR))
                            rate = ONE_CONTROL_DURATION;
                        break;
                    case Trait::Hardhead:
                        if (mechanics & (Bit(MECHANIC_STUN) | Bit(MECHANIC_KNOCKOUT)))
                            rate = ONE_CONTROL_DURATION;
                        break;
                    case Trait::SureFoot:
                        if (mechanics & (Bit(MECHANIC_ROOT) | Bit(MECHANIC_SNARE)))
                            rate = ONE_CONTROL_DURATION;
                        break;
                    case Trait::Composed:
                        if (mechanics & CONTROL_MECHANICS)
                            rate = ALL_CONTROL_DURATION;
                        break;
                    default:
                        break;
                }
            }
            if (rate != 1.0f)
                duration = std::max<int32>(1, int32(duration * rate));
        }

        void ChangeResurrection(Player const* me, State const& state, uint32& health, uint32& mana)
        {
            if (state.combat != Trait::Revive)
                return;
            health = std::max(health, uint32(me->GetMaxHealth() * REVIVE_AT));
            if (me->GetMaxPower(POWER_MANA))
                mana = std::max(mana, uint32(me->GetMaxPower(POWER_MANA) * REVIVE_AT));
        }

        float RegenRate(State const& state)
        {
            uint32 const now = WorldTimer::getMSTime();
            float rate = 1.0f;
            switch (state.life)
            {
                case Trait::Diligent:
                    rate += DILIGENT_MAX * std::min(1.0f, float(state.lastFightMs) / DILIGENT_FULL_MS);
                    break;
                case Trait::NightOwl:
                    if (IsNight())
                        rate = DAY_NIGHT_REGEN;
                    break;
                case Trait::EarlyBird:
                    if (!IsNight())
                        rate = DAY_NIGHT_REGEN;
                    break;
                case Trait::RainLover:
                    if (state.rainy)
                        rate = RAIN_REGEN;
                    break;
                default:
                    break;
            }
            if (state.combat == Trait::Breather && state.leftCombatMs && !state.inCombat && now - state.leftCombatMs < BREATHER_MS)
                rate *= BREATHER_REGEN;
            return rate;
        }

        float CastTimeRate(State const& state, SpellEntry const* spell)
        {
            return state.life == Trait::QuickHands && IsCraftOrGather(spell) ? QUICK_HANDS_CAST : 1.0f;
        }

        float PushbackRate(State const& state)
        {
            return state.combat == Trait::Steady ? STEADY_PUSHBACK : 1.0f;
        }

        float LockoutRate(State const& state)
        {
            return state.combat == Trait::Focus ? FOCUS_LOCKOUT : 1.0f;
        }

        float FallDamageRate(State const& state)
        {
            return state.life == Trait::Light ? LIGHT_FALL : 1.0f;
        }

        float BreathRate(State const& state)
        {
            return state.life == Trait::Swimmer ? SWIMMER_BREATH : 1.0f;
        }

        float SwimSpeedRate(State const& state)
        {
            return state.life == Trait::Swimmer ? SWIMMER_SPEED : 1.0f;
        }

        float ReputationRate(State const& state)
        {
            return state.life == Trait::Likeable ? LIKEABLE_REPUTATION : 1.0f;
        }

        float RepairCostRate(State const& state)
        {
            return state.life == Trait::Frugal ? FRUGAL_REPAIR : 1.0f;
        }

        float SellPriceRate(State const& state, ItemPrototype const* proto)
        {
            // (only grey: the user did not want him to become the one who sells everything)
            return state.life == Trait::Frugal && proto && proto->Quality == ITEM_QUALITY_POOR ? FRUGAL_JUNK : 1.0f;
        }

        float BuyPriceRate(State const& state, ItemPrototype const* proto)
        {
            return state.life == Trait::Thrifty && IsSupply(proto) ? THRIFTY_SUPPLIES : 1.0f;
        }

        float LootMoneyRate(State const& state)
        {
            return state.life == Trait::Scrounger ? SCROUNGER_MONEY : 1.0f;
        }

        float DurabilityLossRate(State const& state)
        {
            return state.life == Trait::Careful ? CAREFUL_DURABILITY : 1.0f;
        }

        float DetectedRangeRate(State const& state)
        {
            return state.life == Trait::Quiet ? QUIET_DETECTED : 1.0f;
        }

        int32 StealthDetectBonus(State const& state)
        {
            return state.combat == Trait::Alert ? int32(ALERT_DETECT) : 0;
        }

        float CraftExtraChance(State const& state, SpellEntry const* spell)
        {
            return state.life == Trait::Crafty && IsTradeskill(spell) ? CRAFTY_CHANCE : 0.0f;
        }

        uint32 SavedReagent(State const& state, SpellEntry const* spell)
        {
            if (state.life != Trait::Saver || !IsTradeskill(spell) || !roll_chance_f(SAVER_CHANCE * 100.0f))
                return 0;
            // The cheapest of its reagents (by what a vendor pays for one).
            uint32 cheapest = 0;
            uint32 cheapestPrice = 0;
            for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
            {
                if (spell->Reagent[i] <= 0)
                    continue;
                ItemPrototype const* proto = sObjectMgr.GetItemPrototype(spell->Reagent[i]);
                uint32 const price = proto ? proto->SellPrice : 0;
                if (!cheapest || price < cheapestPrice)
                {
                    cheapest = spell->Reagent[i];
                    cheapestPrice = price;
                }
            }
            return cheapest;
        }

        float GatherExtraChance(State const& state)
        {
            return state.life == Trait::Gatherer ? GATHERER_CHANCE : 0.0f;
        }

        float TravelTimeRate(State const& state)
        {
            if (state.life == Trait::Swift)
                return SWIFT_TRAVEL;
            return state.life == Trait::Pathfinder ? PATHFINDER_TRAVEL : 1.0f;
        }

        bool SkipsSailing(State const& state)
        {
            return state.life == Trait::Swift;
        }
    }
}
