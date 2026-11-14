#include "Commander/Behaviors/Combat/CastWatch.h"
#include "Commander/Behaviors/Combat/ThreatBehavior.h"
#include "Commander/Perception/Forecast/ThreatView.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "PartyBotAI.h"
#include "HealSpells.h"
#include "Spell.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>
#include <vector>

namespace Commander
{
    namespace
    {
        // Single-target direct heal with a cast time. Resurrections, buffs, area heals
        // (Prayer of Healing), chained heals (Chain Heal) and channels are not.
        bool IsWatchedHeal(Spell const* spell, bool& periodicOut)
        {
            SpellEntry const* info = spell->m_spellInfo;
            if (spell->GetCastTime() <= 0 || spell->IsChanneled() || info->IsChanneledSpell() ||
                info->IsAreaOfEffectSpell())
                return false;

            bool directHeal = false;
            periodicOut = false;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                if (info->EffectChainTarget[i] > 1)
                    return false;
                if (info->Effect[i] == SPELL_EFFECT_HEAL)
                    directHeal = true;
                else if (info->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_HEAL)
                    periodicOut = true;
            }
            return directHeal;
        }

        float MissingHealth(Unit const* unit)
        {
            uint32 const health = unit->GetHealth();
            uint32 const maxHealth = unit->GetMaxHealth();
            return maxHealth > health ? float(maxHealth - health) : 0.0f;
        }

        // The heal makes an enemy in the snapshot turn on the healer.
        bool PullsAggro(Player* me, WorldModel const& world, Unit* target, SpellEntry const* spell, float effective)
        {
            ObjectGuid const self = me->GetObjectGuid();
            std::vector<ThreatView::EnemyThreatDelta> deltas;
            ThreatView::PredictHealThreat(me, target, effective, spell, deltas);
            for (ThreatView::EnemyThreatDelta const& d : deltas)
            {
                if (d.added <= 0.0f)
                    continue;
                EnemyFacts const* e = world.FindEnemy(d.enemy);
                if (e && ThreatView::Headroom(*e, self) - d.added < 0.0f)
                    return true;
            }
            return false;
        }
    }

namespace CastWatch
{
    bool Update(PartyBotAI* bot, CastWatchState& state)
    {
        Player* me = bot->me;
        Spell* spell = bot->GetRole() == ROLE_HEALER ? me->GetCurrentSpell(CURRENT_GENERIC_SPELL) : nullptr;
        if (!spell || spell->getState() != SPELL_STATE_PREPARING)
        {
            state.spell = nullptr;
            return false;
        }

        uint32 const now = WorldTimer::getMSTime();
        Unit* target = spell->m_targets.getUnitTarget();
        ObjectGuid const targetGuid = target ? target->GetObjectGuid() : ObjectGuid();

        // A new cast. Unit::Update (which ends casts) runs before UpdateAI every frame and the
        // previous frame saw no cast in between, so a reused Spell address is still a new cast.
        if (spell != state.spell || spell->m_spellInfo->Id != state.spellId || targetGuid != state.target)
        {
            state.spell = spell;
            state.spellId = spell->m_spellInfo->Id;
            state.target = targetGuid;
            state.eligible = target && IsWatchedHeal(spell, state.periodic);
            state.startMissing = target ? MissingHealth(target) : 0.0f;
            state.lastCheckMs = now;    // the arbiter just approved it: first look one interval later
            return false;
        }

        if (!state.eligible || WorldTimer::getMSTimeDiff(state.lastCheckMs, now) < CHECK_INTERVAL_MS)
            return false;
        state.lastCheckMs = now;

        if (!sCommanderMgr.IsEnabled() || !sCommanderMgr.IsModuleEnabled("threat"))
            return false;
        if (!target->IsAlive() || !target->IsInWorld() || target->GetMaxHealth() == 0)
            return false;

        // A target this low is healed whatever it costs (same rule as ThreatBehavior).
        float const ignorePct = ThreatBehavior::HEAL_IGNORE_THREAT_PCT;
        if (target->GetHealthPercent() < ignorePct)
            return false;

        SpellEntry const* info = spell->m_spellInfo;
        float const heal = HealSpells::Estimate(me, target, info).direct;
        if (heal <= 0.0f)
            return false;

        // Same map as the bot, so the commander belongs to this map thread.
        std::shared_ptr<PartyCommander> commander = sCommanderMgr.FindCommander(me);
        if (!commander)
            return false;
        WorldModel const& world = commander->GetWorld();

        uint32 const leftMs = spell->GetCastedTime();
        float const missing = MissingHealth(target);
        float const incoming = Value::IncomingOn(world, targetGuid);
        float const missingAtLanding = std::min(float(target->GetMaxHealth()),
                                                missing + incoming * float(leftMs) / 1000.0f);

        // 1. Someone else healed the target since the cast started.
        if (!state.periodic && leftMs > OVERHEAL_MIN_LEFT_MS &&
            missing < OVERHEAL_DROP_FRACTION * state.startMissing &&
            missingAtLanding < OVERHEAL_KEEP_FRACTION * heal)
        {
            me->InterruptSpell(CURRENT_GENERIC_SPELL, true);
            state.spell = nullptr;
            return true;
        }

        // 2. The heal would now pull an enemy off its holder.
        if (me->IsInCombat() &&
            !(state.aggroCancelTarget == targetGuid &&
              WorldTimer::getMSTimeDiff(state.aggroCancelMs, now) < AGGRO_CANCEL_COOLDOWN_MS))
        {
            float const effective = std::min(missingAtLanding, heal);
            if (PullsAggro(me, world, target, info, effective))
            {
                me->InterruptSpell(CURRENT_GENERIC_SPELL, true);
                state.spell = nullptr;
                state.aggroCancelTarget = targetGuid;
                state.aggroCancelMs = now;
                return true;
            }
        }
        return false;
    }
}
}
