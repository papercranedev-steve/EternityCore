#include "Life/Errands/LootRun.h"
#include "Shared/LootFacts.h"
#include "Shared/Inventory.h"
#include "Commander/Perception/Spatial/SafeRoute.h"
#include "Player.h"
#include "Creature.h"
#include "Group.h"
#include "LootMgr.h"
#include "Map.h"
#include "MotionMaster.h"
#include "WorldSession.h"
#include "Timer.h"
#include <algorithm>
#include <set>
#include <vector>

namespace Life
{
namespace LootRun
{
    namespace
    {
        bool GroupFights(Player* me)
        {
            Group* group = me->GetGroup();
            if (!group)
                return false;
            for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
                if (Player* member = itr->getSource())
                    if (member->IsAlive() && member->IsInCombat())
                        return true;
            return false;
        }

        bool IsDone(LootRunState const& state, ObjectGuid guid)
        {
            return std::find(state.done.begin(), state.done.end(), guid) != state.done.end();
        }

        void MarkDone(LootRunState& state, ObjectGuid guid)
        {
            state.done.push_back(guid);
            while (state.done.size() > DONE_KEEP)
                state.done.pop_front();
        }

        bool InLootReach(Player* me, Creature const* corpse)
        {
            // (the check of the loot handlers)
            return corpse->IsWithinDistInMap(me, me->GetMaxLootDistance(corpse), true, SizeFactor::None);
        }

        // No idle pack on the walk to the corpse (the scan covers the whole walk).
        bool WalkIsSafe(Player* me, Creature const* corpse)
        {
            Commander::Position3 to;
            to.x = corpse->GetPositionX();
            to.y = corpse->GetPositionY();
            to.z = corpse->GetPositionZ();
            Commander::Position3 centre;
            centre.x = (me->GetPositionX() + to.x) * 0.5f;
            centre.y = (me->GetPositionY() + to.y) * 0.5f;
            centre.z = (me->GetPositionZ() + to.z) * 0.5f;
            float const radius = me->GetDistance(corpse) * 0.5f + Commander::SafeRoute::SCAN_RADIUS;
            Commander::SafeRoute::IdleCreatures const idle(me, centre, radius);
            return Commander::SafeRoute::PathIsSafe(me, idle, to, MAX_PATH_DETOUR);
        }

        bool Wants(Player* me, Creature* corpse)
        {
            return me->IsAllowedToLoot(corpse) && BotShared::LootFacts::HasLootWorthTaking(me, corpse);
        }

        // The nearest corpse to loot. Corpses without a safe walk are marked done.
        Creature* Pick(Player* me, LootRunState& state, WorldObject const* anchor)
        {
            std::vector<Creature*> corpses = BotShared::LootFacts::LootableCorpsesNear(me, SEARCH_YD);
            corpses.erase(std::remove_if(corpses.begin(), corpses.end(), [&](Creature* c)
            {
                return IsDone(state, c->GetObjectGuid()) || !anchor->IsWithinDist(c, ANCHOR_YD, false) || !Wants(me, c);
            }), corpses.end());
            std::sort(corpses.begin(), corpses.end(), [me](Creature* a, Creature* b)
            {
                return me->GetDistance(a) < me->GetDistance(b);
            });

            for (Creature* c : corpses)
            {
                if (InLootReach(me, c) || WalkIsSafe(me, c))
                    return c;
                MarkDone(state, c->GetObjectGuid());
            }
            return nullptr;
        }

        // Opens the corpse and takes what the loot rules give the bot, as a client would.
        void LootCorpse(Player* me, Creature* corpse)
        {
            WorldSession* session = me->GetSession();
            ObjectGuid const guid = corpse->GetObjectGuid();

            std::set<ObjectGuid> before;
            BotShared::Inventory::CollectBagItemGuids(me, before);

            if (me->IsNonMeleeSpellCasted())
                me->InterruptNonMeleeSpells(false);
            // The first opening starts the group's rolls (Group::GroupLoot): what is left to
            // take is read after it.
            me->SendLoot(guid, LOOT_CORPSE);
            if (me->GetLootGuid() != guid)
                return;

            if (corpse->loot.gold)
                session->HandleLootMoneyOpcode(NullClientPacket(CMSG_LOOT_MONEY));
            for (uint8 slot : BotShared::LootFacts::SlotsToTake(me, corpse))
            {
                WorldPackets::Loot::AutoStoreLootItem packet;
                packet.lootSlot = slot;
                session->HandleAutostoreLootItemOpcode(packet);
            }
            session->DoLootRelease(guid);

            BotShared::Inventory::WearUpgrades(me, before);
        }
    }

    void Stop(Player* me, LootRunState& state)
    {
        if (state.corpse.IsEmpty())
            return;
        state.corpse.Clear();
        if (!me->IsStopped())
            me->StopMoving();
        me->GetMotionMaster()->Clear(false, true);
        me->GetMotionMaster()->MoveIdle();
    }

    bool Update(Player* me, LootRunState& state, Player const* anchor)
    {
        WorldObject const* center = anchor ? static_cast<WorldObject const*>(anchor) : me;
        if (!me->IsAlive() || me->IsInCombat() || me->IsMounted() || (anchor && anchor->IsMounted()) ||
            me->GetStandState() != UNIT_STAND_STATE_STAND || GroupFights(me))
        {
            Stop(me, state);
            return false;
        }

        uint32 const now = WorldTimer::getMSTime();
        Creature* corpse = nullptr;
        if (!state.corpse.IsEmpty())
        {
            corpse = me->GetMap()->GetCreature(state.corpse);
            if (!corpse || !Wants(me, corpse) || WorldTimer::getMSTimeDiff(state.startedMs, now) > GIVE_UP_MS)
            {
                MarkDone(state, state.corpse);
                Stop(me, state);
                return false;
            }
        }
        else
        {
            if (now < state.nextSearchMs)
                return false;
            corpse = Pick(me, state, center);
            if (!corpse)
            {
                state.nextSearchMs = now + SEARCH_MS;
                return false;
            }
            state.corpse = corpse->GetObjectGuid();
            state.startedMs = now;
            state.nextMoveMs = 0;
        }

        if (InLootReach(me, corpse))
        {
            if (!me->IsStopped())
                me->StopMoving();
            LootCorpse(me, corpse);
            MarkDone(state, corpse->GetObjectGuid());
            Stop(me, state);
            return true;
        }

        if (me->HasUnitState(UNIT_STATE_NO_FREE_MOVE))
            return true;

        if (now >= state.nextMoveMs)
        {
            state.nextMoveMs = now + REPATH_MS;
            me->GetMotionMaster()->MovePoint(0, corpse->GetPositionX(), corpse->GetPositionY(), corpse->GetPositionZ(),
                                             MOVE_PATHFINDING | MOVE_RUN_MODE);
        }
        return true;
    }
}
}
