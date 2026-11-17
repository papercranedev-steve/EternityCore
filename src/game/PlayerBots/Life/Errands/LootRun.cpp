#include "Life/Errands/LootRun.h"
#include "Life/Errands/ErrandCommon.h"
#include "Shared/LootFacts.h"
#include "Shared/Inventory.h"
#include "Player.h"
#include "Creature.h"
#include "LootMgr.h"
#include "Map.h"
#include "MotionMaster.h"
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
        bool InLootReach(Player* me, Creature const* corpse)
        {
            // (the check of the loot handlers)
            return corpse->IsWithinDistInMap(me, me->GetMaxLootDistance(corpse), true, SizeFactor::None);
        }

        // Gray items (vendor money, Shared/Vendor::SellJunk) while the bags have room for them.
        bool TakesJunk(Player* me)
        {
            return BotShared::Inventory::FreeBagSlots(me) > JUNK_ROOM_SLOTS;
        }

        bool Wants(Player* me, Creature* corpse)
        {
            return me->IsAllowedToLoot(corpse) && BotShared::LootFacts::HasLootWorthTaking(me, corpse, TakesJunk(me));
        }

        // The nearest corpse to loot. Corpses without a safe walk are marked done.
        Creature* Pick(Player* me, LootRunState& state, WorldObject const* anchor)
        {
            std::vector<Creature*> corpses = BotShared::LootFacts::LootableCorpsesNear(me, SEARCH_YD);
            corpses.erase(std::remove_if(corpses.begin(), corpses.end(), [&](Creature* c)
            {
                return Errands::IsDone(state.done, c->GetObjectGuid()) || !anchor->IsWithinDist(c, ANCHOR_YD, false) || !Wants(me, c);
            }), corpses.end());
            std::sort(corpses.begin(), corpses.end(), [me](Creature* a, Creature* b)
            {
                return me->GetDistance(a) < me->GetDistance(b);
            });

            for (Creature* c : corpses)
            {
                if (InLootReach(me, c) || Errands::WalkIsSafe(me, c->GetPositionX(), c->GetPositionY(), c->GetPositionZ(), MAX_PATH_DETOUR))
                    return c;
                Errands::MarkDone(state.done, c->GetObjectGuid(), DONE_KEEP);
            }
            return nullptr;
        }

        // Opens the corpse and takes what the loot rules give the bot, as a client would.
        void LootCorpse(Player* me, Creature* corpse)
        {
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

            BotShared::LootFacts::TakeOpenLoot(me, TakesJunk(me));
            BotShared::Inventory::WearUpgrades(me, before);
        }
    }

    void Stop(Player* me, LootRunState& state)
    {
        if (state.corpse.IsEmpty())
            return;
        state.corpse.Clear();
        Errands::StopWalk(me);
    }

    bool Update(Player* me, LootRunState& state, Player const* anchor)
    {
        WorldObject const* center = anchor ? static_cast<WorldObject const*>(anchor) : me;
        if (!Errands::MayRun(me, anchor))
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
                Errands::MarkDone(state.done, state.corpse, DONE_KEEP);
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
            Errands::MarkDone(state.done, corpse->GetObjectGuid(), DONE_KEEP);
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
