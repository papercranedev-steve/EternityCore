#include "Life/Errands/QuestItemUse.h"
#include "Life/Errands/ErrandCommon.h"
#include "Shared/ItemUse.h"
#include "Shared/BotChat.h"
#include "Shared/Phrases.h"
#include "Shared/QuestOverrides.h"
#include "Player.h"
#include "Creature.h"
#include "GameObject.h"
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
        // An item of an incomplete quest, how it is used, and the quest.
        struct Use
        {
            uint32 itemId = 0;
            uint32 questId = 0;
            QuestItemUseState::Mode mode = QuestItemUseState::Mode::OnCreature;
            SpellEntry const* spell = nullptr;
        };

        bool HasCreatureTarget(uint32 itemId)
        {
            ItemRequiredTargetMapBounds const bounds = sObjectMgr.GetItemRequiredTargetMapBounds(itemId);
            return bounds.first != bounds.second;
        }

        bool PicksScriptTarget(SpellEntry const* spell)
        {
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                for (uint32 target : { spell->EffectImplicitTargetA[i], spell->EffectImplicitTargetB[i] })
                    if (target == TARGET_UNIT_SCRIPT_NEAR_CASTER || target == TARGET_GAMEOBJECT_SCRIPT_NEAR_CASTER)
                        return true;
            return false;
        }

        // How the item is used, false: not one the bot knows how to use.
        bool ModeOf(uint32 itemId, SpellEntry const* spell, QuestItemUseState::Mode& mode)
        {
            if (HasCreatureTarget(itemId))
                mode = QuestItemUseState::Mode::OnCreature;
            else if (!spell)
                return false;
            else if (spell->RequiresSpellFocus)
                mode = QuestItemUseState::Mode::NearFocus;
            else if (PicksScriptTarget(spell))
            {
                SpellScriptTargetBounds const bounds = sSpellMgr.GetSpellScriptTargetBounds(spell->Id);
                if (bounds.first == bounds.second)
                    return false;
                mode = QuestItemUseState::Mode::NearScriptTarget;
            }
            else
                return false;
            return true;
        }

        std::vector<Use> Uses(Player* me)
        {
            std::vector<Use> uses;
            for (auto const& entry : me->GetQuestStatusMap())
            {
                if (entry.second.m_status != QUEST_STATUS_INCOMPLETE || BotShared::QuestOverrides::NoItemUse(entry.first))
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
                {
                    if (!me->HasItemCount(itemId, 1))
                        continue;
                    Use use;
                    use.itemId = itemId;
                    use.questId = entry.first;
                    use.spell = BotShared::ItemUse::OnUseSpell(me, itemId);
                    if (ModeOf(itemId, use.spell, use.mode))
                        uses.push_back(use);
                }
            }
            return uses;
        }

        bool FitsCreature(Use const& use, Creature* c)
        {
            if (use.mode == QuestItemUseState::Mode::OnCreature)
            {
                ItemRequiredTargetMapBounds const bounds = sObjectMgr.GetItemRequiredTargetMapBounds(use.itemId);
                for (auto itr = bounds.first; itr != bounds.second; ++itr)
                    if (itr->second.IsFitToRequirements(c))
                        return true;
                return false;
            }
            if (use.mode != QuestItemUseState::Mode::NearScriptTarget || !use.spell)
                return false;
            SpellScriptTargetBounds const bounds = sSpellMgr.GetSpellScriptTargetBounds(use.spell->Id);
            for (auto itr = bounds.first; itr != bounds.second; ++itr)
            {
                SpellTargetEntry const& t = itr->second;
                if (t.targetEntry != c->GetEntry())
                    continue;
                if (t.type == SPELL_TARGET_TYPE_CREATURE && c->IsAlive())
                    return true;
                if (t.type == SPELL_TARGET_TYPE_DEAD && c->IsCorpse())
                    return true;
            }
            return false;
        }

        bool FitsObject(Use const& use, GameObject* go)
        {
            if (!go->isSpawned() || !use.spell)
                return false;
            GameObjectInfo const* info = go->GetGOInfo();
            if (use.mode == QuestItemUseState::Mode::NearFocus)
                return info->type == GAMEOBJECT_TYPE_SPELL_FOCUS && info->spellFocus.focusId == use.spell->RequiresSpellFocus;
            if (use.mode != QuestItemUseState::Mode::NearScriptTarget)
                return false;
            SpellScriptTargetBounds const bounds = sSpellMgr.GetSpellScriptTargetBounds(use.spell->Id);
            for (auto itr = bounds.first; itr != bounds.second; ++itr)
                if (itr->second.type == SPELL_TARGET_TYPE_GAMEOBJECT && itr->second.targetEntry == go->GetEntry())
                    return true;
            return false;
        }

        bool Fits(Use const& use, WorldObject* target)
        {
            if (Creature* c = target->ToCreature())
                return FitsCreature(use, c);
            if (GameObject* go = target->ToGameObject())
                return FitsObject(use, go);
            return false;
        }

        // Not one somebody else fights (a tapped kill); the bot's own fight is fine.
        bool Free(Player const* me, WorldObject* target)
        {
            Creature const* c = target->ToCreature();
            if (!c || !c->IsInCombat())
                return true;
            return c->GetVictim() == me;
        }

        class CreatureCheck
        {
        public:
            CreatureCheck(Player* me, std::vector<Use> const& uses) : m_me(me), m_uses(uses) {}
            bool operator()(Creature* c)
            {
                if (!m_me->IsWithinDist(c, SEARCH_YD, false) || !Free(m_me, c))
                    return false;
                for (Use const& use : m_uses)
                    if (FitsCreature(use, c))
                        return true;
                return false;
            }

        private:
            Player* m_me;
            std::vector<Use> const& m_uses;
        };

        class ObjectCheck
        {
        public:
            ObjectCheck(Player* me, std::vector<Use> const& uses) : m_me(me), m_uses(uses) {}
            bool operator()(GameObject* go)
            {
                if (!m_me->IsWithinDist(go, SEARCH_YD, false))
                    return false;
                for (Use const& use : m_uses)
                    if (FitsObject(use, go))
                        return true;
                return false;
            }

        private:
            Player* m_me;
            std::vector<Use> const& m_uses;
        };

        // How close the bot goes: into the spell's range of a target it is used on or picks
        // itself, into the focus object's own distance.
        float UseRange(Use const& use, WorldObject const* target)
        {
            float range = 0.0f;
            if (use.mode == QuestItemUseState::Mode::NearFocus)
            {
                GameObject const* go = target->ToGameObject();
                range = go ? float(go->GetGOInfo()->spellFocus.dist) : 0.0f;
                return std::max(range - 1.5f, 1.5f);
            }
            if (use.spell)
                range = Spells::GetSpellMaxRange(sSpellRangeStore.LookupEntry(use.spell->rangeIndex));
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

        WorldObject* Find(Player* me, ObjectGuid const& guid)
        {
            if (guid.IsGameObject())
                return me->GetMap()->GetGameObject(guid);
            return me->GetMap()->GetCreature(guid);
        }

        // The nearest target of any use near the bot and the anchor, not done.
        WorldObject* Pick(Player* me, QuestItemUseState& state, WorldObject const* center, std::vector<Use> const& uses, Use& picked)
        {
            std::vector<WorldObject*> targets;
            {
                CreatureCheck check(me, uses);
                std::list<Creature*> creatures;
                MaNGOS::CreatureListSearcher<CreatureCheck> searcher(creatures, check);
                Cell::VisitGridObjects(me, searcher, SEARCH_YD);
                targets.insert(targets.end(), creatures.begin(), creatures.end());
            }
            {
                ObjectCheck check(me, uses);
                std::list<GameObject*> objects;
                MaNGOS::GameObjectListSearcher<ObjectCheck> searcher(objects, check);
                Cell::VisitGridObjects(me, searcher, SEARCH_YD);
                targets.insert(targets.end(), objects.begin(), objects.end());
            }
            targets.erase(std::remove_if(targets.begin(), targets.end(), [&](WorldObject* t)
            {
                return Errands::IsDone(state.done, t->GetObjectGuid()) || !center->IsWithinDist(t, ANCHOR_YD, false);
            }), targets.end());
            std::sort(targets.begin(), targets.end(), [me](WorldObject* a, WorldObject* b)
            {
                return me->GetDistance(a) < me->GetDistance(b);
            });
            for (WorldObject* t : targets)
                for (Use const& use : uses)
                    if (Fits(use, t))
                    {
                        picked = use;
                        return t;
                    }
            return nullptr;
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
        WorldObject* target = nullptr;
        Use use;
        if (!state.target.IsEmpty())
        {
            target = Find(me, state.target);
            use.itemId = state.itemId;
            use.questId = state.questId;
            use.mode = state.mode;
            use.spell = BotShared::ItemUse::OnUseSpell(me, state.itemId);
            if (!target || !Fits(use, target) || !Free(me, target) || !me->HasItemCount(state.itemId, 1) ||
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
            target = Pick(me, state, center, uses, use);
            if (!target)
                return false;
            state.target = target->GetObjectGuid();
            state.itemId = use.itemId;
            state.questId = use.questId;
            state.mode = use.mode;
            state.startedMs = now;
            state.nextMoveMs = 0;
        }

        if (me->IsWithinDistInMap(target, UseRange(use, target)) && me->IsWithinLOSInMap(target))
        {
            if (!me->IsStopped())
                me->StopMoving();
            me->SetFacingToObject(target);
            // On a creature: it is the target. Near one: the spell finds the focus or the
            // script target itself, the client sends the caster.
            SpellCastTargets targets;
            if (use.mode == QuestItemUseState::Mode::OnCreature)
                targets.setUnitTarget(target->ToCreature());
            else
                targets.setUnitTarget(me);
            // One use per target, gone through or not: one the item already worked on (a woken
            // Lazy Peon, a tower with its beacon) gives no second credit.
            Errands::MarkDone(state.done, target->GetObjectGuid(), DONE_KEEP);
            if (!BotShared::ItemUse::UseFromBags(me, state.itemId, targets))
                sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[QuestItemUse] %s: item %u at %s (%u) did not go through (stand state %u).",
                    me->GetName(), state.itemId, target->GetName(), target->GetEntry(), uint32(me->GetStandState()));
            else if (use.spell && use.spell->HasAttribute(SPELL_ATTR_EX2_SPECIAL_TAMING_FLAG))
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
