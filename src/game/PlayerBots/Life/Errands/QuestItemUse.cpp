#include "Life/Errands/QuestItemUse.h"
#include "Life/Errands/ErrandCommon.h"
#include "Shared/ItemUse.h"
#include "Shared/BotChat.h"
#include "Shared/Phrases.h"
#include "Player.h"
#include "Creature.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "DBCStores.h"
#include "Timer.h"
#include "Log.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <algorithm>
#include <list>

namespace Life
{
namespace QuestItemUse
{
    namespace
    {
        // An item of an incomplete quest that is used on a creature, and the quest.
        struct Use
        {
            uint32 itemId = 0;
            uint32 questId = 0;
        };

        bool HasCreatureTarget(uint32 itemId)
        {
            ItemRequiredTargetMapBounds const bounds = sObjectMgr.GetItemRequiredTargetMapBounds(itemId);
            return bounds.first != bounds.second;
        }

        std::vector<Use> Uses(Player* me)
        {
            std::vector<Use> uses;
            for (auto const& entry : me->GetQuestStatusMap())
            {
                if (entry.second.m_status != QUEST_STATUS_INCOMPLETE)
                    continue;
                Quest const* quest = sObjectMgr.GetQuestTemplate(entry.first);
                if (!quest)
                    continue;
                std::vector<uint32> items;
                if (quest->GetSrcItemId())
                    items.push_back(quest->GetSrcItemId());
                for (uint32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
                    if (quest->ReqItemId[i])
                        items.push_back(quest->ReqItemId[i]);
                for (uint32 itemId : items)
                    if (HasCreatureTarget(itemId) && me->HasItemCount(itemId, 1))
                        uses.push_back({ itemId, entry.first });
            }
            return uses;
        }

        bool Fits(uint32 itemId, Unit* unit)
        {
            ItemRequiredTargetMapBounds const bounds = sObjectMgr.GetItemRequiredTargetMapBounds(itemId);
            for (auto itr = bounds.first; itr != bounds.second; ++itr)
                if (itr->second.IsFitToRequirements(unit))
                    return true;
            return false;
        }

        // Not one somebody else fights (a tapped kill); the bot's own fight is fine.
        bool Free(Player const* me, Creature const* c)
        {
            if (!c->IsInCombat())
                return true;
            Unit const* victim = c->GetVictim();
            return victim == me;
        }

        class TargetCheck
        {
        public:
            TargetCheck(Player* me, std::vector<Use> const& uses) : m_me(me), m_uses(uses) {}
            bool operator()(Creature* c)
            {
                if (!m_me->IsWithinDist(c, SEARCH_YD, false) || !Free(m_me, c))
                    return false;
                for (Use const& use : m_uses)
                    if (Fits(use.itemId, c))
                        return true;
                return false;
            }

        private:
            Player* m_me;
            std::vector<Use> const& m_uses;
        };

        float UseRange(Player const* me, uint32 itemId)
        {
            SpellEntry const* spell = BotShared::ItemUse::OnUseSpell(me, itemId);
            float const range = spell ? Spells::GetSpellMaxRange(sSpellRangeStore.LookupEntry(spell->rangeIndex)) : 0.0f;
            // a few yards inside it, at least melee
            return std::max(range - 4.0f, INTERACTION_DISTANCE);
        }

        void Clear(QuestItemUseState& state)
        {
            state.target.Clear();
            state.itemId = 0;
            state.questId = 0;
        }

        bool Channeling(Player const* me)
        {
            return me->GetCurrentSpell(CURRENT_CHANNELED_SPELL) != nullptr;
        }
    }

    void Stop(Player* me, QuestItemUseState& state)
    {
        if (state.target.IsEmpty())
            return;
        Clear(state);
        Errands::StopWalk(me);
    }

    bool Update(Player* me, QuestItemUseState& state, Player const* anchor)
    {
        // A channel it started (the taming rod) keeps the bot where it is.
        if (Channeling(me))
            return true;

        WorldObject const* center = anchor ? static_cast<WorldObject const*>(anchor) : me;
        if (!Errands::MayRun(me, anchor))
        {
            Stop(me, state);
            return false;
        }

        uint32 const now = WorldTimer::getMSTime();
        Creature* target = nullptr;
        if (!state.target.IsEmpty())
        {
            target = me->GetMap()->GetCreature(state.target);
            if (!target || !Fits(state.itemId, target) || !Free(me, target) || !me->HasItemCount(state.itemId, 1) ||
                me->GetQuestStatus(state.questId) != QUEST_STATUS_INCOMPLETE ||
                WorldTimer::getMSTimeDiff(state.startedMs, now) > GIVE_UP_MS)
            {
                Errands::MarkDone(state.done, state.target, DONE_KEEP);
                Stop(me, state);
                return false;
            }
        }
        else
        {
            if (now < state.nextSearchMs)
                return false;
            state.nextSearchMs = now + SEARCH_MS;
            std::vector<Use> const uses = Uses(me);
            if (uses.empty())
                return false;

            TargetCheck check(me, uses);
            std::list<Creature*> creatures;
            MaNGOS::CreatureListSearcher<TargetCheck> searcher(creatures, check);
            Cell::VisitGridObjects(me, searcher, SEARCH_YD);
            std::vector<Creature*> targets(creatures.begin(), creatures.end());
            targets.erase(std::remove_if(targets.begin(), targets.end(), [&](Creature* c)
            {
                return Errands::IsDone(state.done, c->GetObjectGuid()) || !center->IsWithinDist(c, ANCHOR_YD, false);
            }), targets.end());
            std::sort(targets.begin(), targets.end(), [me](Creature* a, Creature* b)
            {
                return me->GetDistance(a) < me->GetDistance(b);
            });
            for (Creature* c : targets)
            {
                for (Use const& use : uses)
                    if (Fits(use.itemId, c))
                    {
                        target = c;
                        state.itemId = use.itemId;
                        state.questId = use.questId;
                        break;
                    }
                if (target)
                    break;
            }
            if (!target)
                return false;
            state.target = target->GetObjectGuid();
            state.startedMs = now;
            state.nextMoveMs = 0;
        }

        if (me->IsWithinDistInMap(target, UseRange(me, state.itemId)) && me->IsWithinLOSInMap(target))
        {
            if (!me->IsStopped())
                me->StopMoving();
            me->SetFacingToObject(target);
            SpellCastTargets targets;
            targets.setUnitTarget(target);
            SpellEntry const* spell = BotShared::ItemUse::OnUseSpell(me, state.itemId);
            if (!BotShared::ItemUse::UseFromBags(me, state.itemId, targets))
            {
                sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[QuestItemUse] %s: item %u on %s (%u) did not go through (stand state %u).",
                    me->GetName(), state.itemId, target->GetName(), target->GetEntry(), uint32(me->GetStandState()));
                Errands::MarkDone(state.done, target->GetObjectGuid(), DONE_KEEP);
            }
            else if (spell && spell->HasAttribute(SPELL_ATTR_EX2_SPECIAL_TAMING_FLAG))
                // (the party does not hit it meanwhile: doctrine HoldFire on a beast being tamed)
                BotShared::BotChat::SayToParty(me, BotShared::Phrases::Say(BotShared::I18n::OfParty(me), "questitem.tame", {}, {}));
            Clear(state);
            return true;
        }

        if (me->HasUnitState(UNIT_STATE_NO_FREE_MOVE))
            return true;

        if (now >= state.nextMoveMs)
        {
            state.nextMoveMs = now + REPATH_MS;
            me->GetMotionMaster()->MovePoint(0, target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(),
                                             MOVE_PATHFINDING | MOVE_RUN_MODE);
        }
        return true;
    }
}
}
