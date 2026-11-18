#include "Commander/Behaviors/Combat/HarvestBehavior.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Perception/Forecast/FinishForecast.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "TeamBotAI.h"
#include "Creature.h"
#include "Map.h"
#include "Spell.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
#include "SpellAuraDefines.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
    namespace
    {
        // The item the spell gives when its target dies under it (0 = none).
        uint32 HarvestItem(SpellEntry const* spell)
        {
            if (!spell)
                return 0;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (spell->Effect[i] == SPELL_EFFECT_APPLY_AURA &&
                    spell->EffectApplyAuraName[i] == SPELL_AURA_CHANNEL_DEATH_ITEM && spell->EffectItemType[i])
                    return spell->EffectItemType[i];
            return 0;
        }

        // What one more item is worth to the bot now (header); 0 = it holds enough or has no room.
        float ItemWorth(TeamBotAI const* bot, WorldModel const& world, PartyMemberFacts const& self, uint32 itemId)
        {
            Player* me = bot->me;
            uint32 const held = me->GetItemCount(itemId);
            if (held >= HarvestBehavior::KEEP)
                return 0.0f;
            ItemPosCountVec dest;
            if (me->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, 1) != EQUIP_ERR_OK)
                return 0.0f;
            float const need = 1.0f - float(held) / float(HarvestBehavior::KEEP);
            return Value::LostAction(world, self, HarvestBehavior::SHARD_WORTH_SEC * need);
        }

        // The enemy would give the bot the item when it dies (Aura::HandleChannelDeathItem).
        bool GivesItem(Player const* me, Unit const* enemy)
        {
            if (!enemy || !enemy->IsAlive() || !me->IsHonorOrXPTarget(enemy))
                return false;
            if (Creature const* creature = enemy->ToCreature())
                if (!creature->IsTappedBy(me))
                    return false;
            // Its own Shadowburn on it already gives the one item this enemy has for it.
            return !enemy->HasAuraTypeByCaster(SPELL_AURA_CHANNEL_DEATH_ITEM, me->GetObjectGuid());
        }

        uint32 SinceRefreshMs(WorldModel const& world)
        {
            return WorldTimer::getMSTimeDiff(world.refreshedAtMs, WorldTimer::getMSTime());
        }

        // Seconds from now until `enemy` dies while the bot only channels on it, or < 0 when
        // not within SHARD_WORTH_SEC (+ `slackSec`).
        float DiesInSec(WorldModel const& world, EnemyFacts const& enemy, ObjectGuid self, float slackSec)
        {
            uint32 const since = SinceRefreshMs(world);
            uint32 const horizon = since + uint32((HarvestBehavior::SHARD_WORTH_SEC + slackSec) * 1000.0f);
            uint32 const deathMs = FinishForecast::DeathMsOwnHitOnly(world, enemy, self, horizon);
            if (!deathMs)
                return -1.0f;
            return deathMs > since ? float(deathMs - since) / 1000.0f : 0.0f;
        }

        // The bot's harvest channel running now (null = none), and its target.
        Spell const* RunningHarvest(TeamBotAI const* bot, SpellEntry const* harvest)
        {
            Spell const* spell = bot->me->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
            if (!spell || !spell->m_spellInfo || spell->getState() != SPELL_STATE_CASTING)
                return nullptr;
            if (sSpellMgr.GetFirstSpellInChain(spell->m_spellInfo->Id) != sSpellMgr.GetFirstSpellInChain(harvest->Id))
                return nullptr;
            return spell;
        }
    }

    bool HarvestBehavior::AppliesTo(TeamBotAI const* bot) const
    {
        return HarvestItem(bot->Kit().DeathHarvest()) != 0;
    }

    void HarvestBehavior::Propose(TeamBotAI const* bot, WorldModel const& world,
                                  TeamPlan const& /*plan*/, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        if (!world.InFight(me->IsInCombat()))
            return;
        SpellEntry const* harvest = bot->Kit().DeathHarvest();
        uint32 const itemId = HarvestItem(harvest);
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!itemId || !self || !self->CanAct() || self->silenced)
            return;
        float const worth = ItemWorth(bot, world, *self, itemId);
        if (worth <= 0.0f)
            return;

        EnemyFacts const* best = nullptr;
        float bestSec = 0.0f;
        for (EnemyFacts const& e : world.enemies)
        {
            float const sec = DiesInSec(world, e, self->guid, 0.0f);
            if (sec < 0.0f || (best && sec >= bestSec))
                continue;
            Unit* unit = me->GetMap()->GetUnit(e.guid);
            if (!GivesItem(me, unit) || !me->IsWithinLOSInMap(unit) || !bot->CanTryToCastSpell(unit, harvest))
                continue;
            best = &e;
            bestSec = sec;
        }
        if (!best)
            return;

        Proposal p;
        p.tier = ProposalTier::Routine;
        p.source = "harvest";
        p.reason = "an enemy about to die: channel on it for its item";
        p.action.type = BotActionType::Cast;
        p.action.spell = harvest;
        p.action.target = best->guid;
        p.SetValue(worth, Value::LostAction(world, *self, bestSec));
        out.push_back(p);
    }

    float HarvestBehavior::ContinueValue(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& /*plan*/,
                                         SpellEntry const* spell, char const*& reason) const
    {
        SpellEntry const* harvest = bot->Kit().DeathHarvest();
        if (!harvest || !spell || sSpellMgr.GetFirstSpellInChain(spell->Id) != sSpellMgr.GetFirstSpellInChain(harvest->Id))
            return -1.0f;
        Spell const* running = RunningHarvest(bot, harvest);
        PartyMemberFacts const* self = world.FindMember(bot->me->GetObjectGuid());
        EnemyFacts const* target = running ? world.FindEnemy(running->m_targets.getUnitTargetGuid()) : nullptr;
        if (!self || !target || DiesInSec(world, *target, self->guid, 0.0f) < 0.0f)
        {
            reason = "its target does not die soon";
            return 0.0f;
        }
        reason = "its target dies soon: the item";
        return ItemWorth(bot, world, *self, HarvestItem(harvest));
    }

    float HarvestBehavior::CastBreakBenefit(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& /*plan*/,
                                            uint32 castLeftMs, char const*& reason) const
    {
        SpellEntry const* harvest = bot->Kit().DeathHarvest();
        Spell const* running = harvest ? RunningHarvest(bot, harvest) : nullptr;
        if (!running)
            return 0.0f;
        PartyMemberFacts const* self = world.FindMember(bot->me->GetObjectGuid());
        EnemyFacts const* target = world.FindEnemy(running->m_targets.getUnitTargetGuid());
        if (!self)
            return 0.0f;
        // One second of slack: the forecast moves a little from one refresh to the next.
        if (target && DiesInSec(world, *target, self->guid, 1.0f) >= 0.0f)
            return 0.0f;
        reason = "the harvest target does not die soon: back to the filler";
        return Value::LostAction(world, *self, float(castLeftMs) / 1000.0f);
    }
}
