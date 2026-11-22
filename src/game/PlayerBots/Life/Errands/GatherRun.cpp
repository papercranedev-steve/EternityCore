#include "Life/Errands/GatherRun.h"
#include "Shared/Gathering.h"
#include "Shared/Inventory.h"
#include "Shared/LootFacts.h"
#include "Shared/ObjectUse.h"
#include "Player.h"
#include "Creature.h"
#include "GameObject.h"
#include "Map.h"
#include "MotionMaster.h"
#include "SpellEntry.h"
#include "Timer.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <algorithm>
#include <cmath>
#include <list>
#include <vector>

namespace Life
{
namespace GatherRun
{
    namespace
    {
        namespace G = BotShared::Gathering;

        bool IsRealPlayer(Player const* p)
        {
            return p && !p->IsBot();
        }

        // The leader can gather it himself: it is his (user 2026-11-21).
        bool LeaderTakes(Player const* leader, Creature const* corpse)
        {
            return IsRealPlayer(leader) && G::CanSkin(leader, corpse);
        }

        bool LeaderTakes(Player const* leader, G::Node const& node)
        {
            return IsRealPlayer(leader) && leader->HasSkill(uint16(node.skill)) &&
                   leader->GetSkillValue(uint16(node.skill)) >= node.reqValue && G::HasTool(leader, node.skill);
        }

        bool WantsCorpse(Player* me, Creature const* corpse, Player const* leader)
        {
            return G::CanSkin(me, corpse) && !LeaderTakes(leader, corpse);
        }

        bool WantsNode(Player* me, GameObject* go, Player const* leader)
        {
            G::Node const node = G::NodeOf(go);
            if (!node.skill || !me->HasSkill(uint16(node.skill)) || !G::HasTool(me, node.skill) || LeaderTakes(leader, node))
                return false;
            return BotShared::ObjectUse::HowToUse(me, go).way == BotShared::ObjectUse::Way::Spell;
        }

        class SkinnableCheck
        {
        public:
            SkinnableCheck(Player* me, float range) : m_me(me), m_range(range) {}
            bool operator()(Creature* c)
            {
                return c->IsDead() && c->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE) && m_me->IsWithinDist(c, m_range, false);
            }

        private:
            Player* m_me;
            float m_range;
        };

        class NodeCheck
        {
        public:
            NodeCheck(Player* me, float range) : m_me(me), m_range(range) {}
            bool operator()(GameObject* go)
            {
                return m_me->IsWithinDist(go, m_range, false) && G::NodeOf(go).skill;
            }

        private:
            Player* m_me;
            float m_range;
        };

        // How far from the target's centre the bot stands to gather it.
        float ApproachYd(WorldObject const* target)
        {
            return target->ToGameObject() ? APPROACH_YD : SKIN_REACH_YD * 0.5f;
        }

        void ApproachPoint(Player const* me, WorldObject const* target, float reach, float& x, float& y, float& z)
        {
            float const angle = target->GetAngle(me);
            x = target->GetPositionX() + reach * std::cos(angle);
            y = target->GetPositionY() + reach * std::sin(angle);
            z = target->GetPositionZ();
        }

        bool InReach(Player* me, WorldObject* target)
        {
            if (GameObject* go = target->ToGameObject())
                return go->IsAtInteractDistance(me);
            return target->IsWithinDistInMap(me, SKIN_REACH_YD, true, SizeFactor::None);
        }

        // The nearest corpse or node it wants; those without a safe walk are marked done.
        WorldObject* Pick(Player* me, GatherRunState& state, WorldObject const* center, Player const* leader)
        {
            std::vector<WorldObject*> targets;
            if (me->HasSkill(SKILL_SKINNING))
            {
                std::list<Creature*> corpses;
                SkinnableCheck check(me, SEARCH_YD);
                MaNGOS::CreatureListSearcher<SkinnableCheck> searcher(corpses, check);
                Cell::VisitGridObjects(me, searcher, SEARCH_YD);
                for (Creature* c : corpses)
                    if (!Errands::IsDone(state.done, c->GetObjectGuid()) && center->IsWithinDist(c, ANCHOR_YD, false) && WantsCorpse(me, c, leader))
                        targets.push_back(c);
            }
            if (me->HasSkill(SKILL_HERBALISM) || me->HasSkill(SKILL_MINING))
            {
                std::list<GameObject*> nodes;
                NodeCheck check(me, SEARCH_YD);
                MaNGOS::GameObjectListSearcher<NodeCheck> searcher(nodes, check);
                Cell::VisitGridObjects(me, searcher, SEARCH_YD);
                for (GameObject* go : nodes)
                    if (!Errands::IsDone(state.done, go->GetObjectGuid()) && center->IsWithinDist(go, ANCHOR_YD, false) && WantsNode(me, go, leader))
                        targets.push_back(go);
            }
            std::sort(targets.begin(), targets.end(), [me](WorldObject* a, WorldObject* b)
            {
                return me->GetDistance(a) < me->GetDistance(b);
            });

            for (WorldObject* t : targets)
            {
                float x, y, z;
                ApproachPoint(me, t, ApproachYd(t), x, y, z);
                if (InReach(me, t) || Errands::WalkIsSafe(me, x, y, z, MAX_PATH_DETOUR))
                    return t;
                Errands::MarkDone(state.done, t->GetObjectGuid(), DONE_KEEP);
            }
            return nullptr;
        }

        WorldObject* Find(Player* me, ObjectGuid guid)
        {
            if (guid.IsGameObject())
                return me->GetMap()->GetGameObject(guid);
            return me->GetMap()->GetCreature(guid);
        }

        bool StillWanted(Player* me, WorldObject* target, Player const* leader)
        {
            if (!target)
                return false;
            if (GameObject* go = target->ToGameObject())
                return WantsNode(me, go, leader);
            Creature* corpse = target->ToCreature();
            return corpse && WantsCorpse(me, corpse, leader);
        }

        // Starts the gathering cast. False: it could not.
        bool StartCast(Player* me, WorldObject* target)
        {
            if (GameObject* go = target->ToGameObject())
                return BotShared::ObjectUse::Start(me, go, BotShared::ObjectUse::HowToUse(me, go));
            Creature* corpse = target->ToCreature();
            SpellEntry const* skinning = G::SpellOf(me, SKILL_SKINNING);
            return corpse && skinning && me->CastSpell(corpse, skinning, false) == SPELL_CAST_OK;
        }

        void Finish(Player* me, GatherRunState& state)
        {
            Errands::MarkDone(state.done, state.target, DONE_KEEP);
            state.target.Clear();
            state.casting = false;
            Errands::StopWalk(me);
        }
    }

    void Stop(Player* me, GatherRunState& state)
    {
        if (state.target.IsEmpty())
            return;
        state.target.Clear();
        state.casting = false;
        Errands::StopWalk(me);
    }

    bool Update(Player* me, GatherRunState& state, Player const* anchor)
    {
        WorldObject const* center = anchor ? static_cast<WorldObject const*>(anchor) : me;
        // (a gathering cast keeps the bot standing still: MayRun stays true while it casts)
        if (!Errands::MayRun(me, anchor))
        {
            Stop(me, state);
            return false;
        }

        uint32 const now = WorldTimer::getMSTime();
        WorldObject* target = nullptr;
        if (!state.target.IsEmpty())
        {
            if (state.casting)
            {
                if (me->IsNonMeleeSpellCasted(false))
                    return true;
                // The cast is over: the loot window is open on the target when it worked.
                if (me->GetLootGuid() == state.target)
                    BotShared::LootFacts::TakeOpenLoot(me);
                Finish(me, state);
                return true;
            }
            target = Find(me, state.target);
            if (!StillWanted(me, target, anchor) || WorldTimer::getMSTimeDiff(state.startedMs, now) > GIVE_UP_MS)
            {
                Finish(me, state);
                return false;
            }
        }
        else
        {
            if (now < state.nextSearchMs)
                return false;
            if (!BotShared::Inventory::FreeBagSlots(me))
            {
                state.nextSearchMs = now + SEARCH_MS;
                return false;
            }
            target = Pick(me, state, center, anchor);
            if (!target)
            {
                state.nextSearchMs = now + SEARCH_MS;
                return false;
            }
            state.target = target->GetObjectGuid();
            state.startedMs = now;
            state.nextMoveMs = 0;
        }

        // Walks up to it like a player, not just into reach (nextMoveMs: it was sent towards it).
        if (InReach(me, target) && Errands::CloseEnoughToUse(me, target, ApproachYd(target), state.nextMoveMs != 0))
        {
            if (!me->IsStopped())
                me->StopMoving();
            me->SetFacingToObject(target);
            if (!StartCast(me, target))
            {
                Finish(me, state);
                return false;
            }
            state.casting = true;
            return true;
        }

        if (me->HasUnitState(UNIT_STATE_NO_FREE_MOVE))
            return true;

        if (now >= state.nextMoveMs)
        {
            state.nextMoveMs = now + REPATH_MS;
            float x, y, z;
            ApproachPoint(me, target, ApproachYd(target), x, y, z);
            me->GetMotionMaster()->MovePoint(0, x, y, z, MOVE_PATHFINDING | MOVE_RUN_MODE);
        }
        return true;
    }
}
}
