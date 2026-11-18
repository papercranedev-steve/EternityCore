#include "Life/Errands/Trip/ShoppingPlanner.h"
#include "Life/Errands/TownTrip.h"
#include "Life/Errands/ErrandCommon.h"
#include "Shared/AuctionMarket.h"
#include "Shared/ClassTraining.h"
#include "Shared/Inventory.h"
#include "Shared/Mailbox.h"
#include "Shared/NpcDirectory.h"
#include "Shared/Purchasing.h"
#include "Shared/Supplies.h"
#include "Shared/Vendor.h"
#include "Player.h"
#include "Creature.h"
#include "GameObject.h"
#include "Item.h"
#include "Map.h"
#include "Log.h"
#include <algorithm>
#include <map>
#include <set>

namespace Life
{
namespace Trip
{
    namespace
    {
        using BotShared::Supplies::Need;
        using BotShared::Supplies::Priority;
        using BotShared::Purchasing::Budget;
        using Needs = std::vector<Need>;

        Priority const TIERS[] = { Priority::Critical, Priority::Important, Priority::Optional };

        uint32 WeightOf(Priority p)
        {
            switch (p)
            {
                case Priority::Critical: return 100;
                case Priority::Important: return 10;
                default: return 1;
            }
        }

        uint32 TrainerReserve(Player* me)
        {
            return BotShared::ClassTraining::CostOfAvailable(me);
        }

        // Lowest durability share of the equipped items, in percent (100: nothing worn down).
        uint32 LowestDurabilityPct(Player const* me)
        {
            uint32 lowest = 100;
            for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
                if (Item* item = me->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                {
                    uint32 const maxDurability = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
                    if (maxDurability)
                        lowest = std::min(lowest, item->GetUInt32Value(ITEM_FIELD_DURABILITY) * 100 / maxDurability);
                }
            return lowest;
        }

        // Units planned since `before`, as needs of the tier's priority (merged per goods).
        void TakeDiff(std::map<BotShared::Goods::Spec, uint32> const& before, Budget const& after, Priority tier, Needs& into)
        {
            for (auto const& itr : after.planned)
            {
                auto was = before.find(itr.first);
                uint32 const old = was != before.end() ? was->second : 0;
                if (itr.second <= old)
                    continue;
                uint32 const units = itr.second - old;
                auto same = std::find_if(into.begin(), into.end(), [&](Need const& n) { return n.goods.kind == itr.first.kind && n.goods.id == itr.first.id; });
                if (same != into.end())
                {
                    same->target += units;
                    same->priority = std::min(same->priority, tier);
                    continue;
                }
                Need n;
                n.goods = itr.first;
                n.target = units;
                n.have = 0;
                n.priority = tier;
                into.push_back(n);
            }
        }

        // ---- Stop actions ----

        class AuctionAction final : public IStopAction
        {
        public:
            Needs needs;
            char const* Name() const override { return "auction"; }
            void Do(Player* me, WorldObject* target) override
            {
                Creature* auctioneer = target->ToCreature();
                if (!auctioneer)
                    return;
                Budget budget = Budget::Of(me, TrainerReserve(me));
                std::set<uint32> taken;
                std::vector<BotShared::AuctionMarket::Listing> const listings =
                    BotShared::AuctionMarket::PlanBuys(me, auctioneer, needs, budget, taken);
                uint32 const asked = BotShared::AuctionMarket::Buyout(me, auctioneer, listings);
                sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Life] %s at the auction house: %u listings for %u copper.",
                    me->GetName(), uint32(listings.size()), asked);
            }
        };

        class MailboxAction final : public IStopAction
        {
        public:
            char const* Name() const override { return "mailbox"; }
            void Do(Player* me, WorldObject* target) override
            {
                if (GameObject* mailbox = target->ToGameObject())
                    BotShared::Mailbox::TakeAll(me, mailbox);
            }
        };

        class VendorAction final : public IStopAction
        {
        public:
            Needs needs;
            bool repair = false;
            char const* Name() const override { return "vendor"; }
            void Do(Player* me, WorldObject* target) override
            {
                Creature* vendor = target->ToCreature();
                if (!vendor)
                    return;
                uint32 const earned = BotShared::Vendor::SellJunk(me, vendor);
                uint32 repaired = 0;
                if (repair && vendor->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_REPAIR))
                {
                    uint32 const cost = BotShared::Vendor::RepairCost(me, vendor);
                    if (cost && me->GetMoney() >= cost)
                        repaired = BotShared::Vendor::RepairAll(me, vendor);
                }
                Budget budget = Budget::Of(me, TrainerReserve(me));
                uint32 const spent = BotShared::Vendor::Buy(me, vendor, BotShared::Vendor::PlanPurchases(me, vendor, needs, budget));
                sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Life] %s at vendor %s: sold for %u, repaired for %u, bought for %u copper.",
                    me->GetName(), vendor->GetName(), earned, repaired, spent);
            }
        };

        // ---- Planning ----

        struct VendorPlan
        {
            Creature* npc = nullptr;
            std::shared_ptr<VendorAction> action;
        };

        std::vector<Creature*> LiveNpcs(Player* me, Player const* leader, uint32 npcFlags)
        {
            std::vector<Creature*> out;
            Map* map = me->GetMap();
            for (BotShared::NpcDirectory::ServiceNpc const& n : BotShared::NpcDirectory::Near(
                     map->GetId(), leader->GetPositionX(), leader->GetPositionY(), TownTrip::TRIP_YD, npcFlags))
            {
                Creature* npc = map->GetCreature(BotShared::NpcDirectory::GuidOf(n));
                if (npc && npc->IsAlive() && !npc->IsInCombat() && !me->IsHostileTo(npc))
                    out.push_back(npc);
            }
            return out;
        }

        bool SafeTo(Player* me, WorldObject const* target)
        {
            return Errands::WalkIsSafe(me, target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(),
                                       TownTrip::MAX_PATH_DETOUR);
        }

        TripStop StopAt(WorldObject const* target, char const* placeKey, std::shared_ptr<IStopAction> action)
        {
            TripStop s;
            s.target = target->GetObjectGuid();
            s.x = target->GetPositionX();
            s.y = target->GetPositionY();
            s.z = target->GetPositionZ();
            s.placeKey = placeKey ? placeKey : "";
            s.action = std::move(action);
            return s;
        }

        char const* VendorPlaceKey(VendorAction const& a)
        {
            if (a.repair)
                return "place.vendor.repair";
            for (Need const& n : a.needs)
                switch (n.goods.kind)
                {
                    case BotShared::Goods::Kind::Ammo:
                        return "place.vendor.ammo";
                    case BotShared::Goods::Kind::Food:
                    case BotShared::Goods::Kind::Drink:
                    case BotShared::Goods::Kind::PetFood:
                        return "place.vendor.food";
                    case BotShared::Goods::Kind::Item:
                        return "place.vendor.reagent";
                }
            return "place.vendor";
        }

        Needs OfTier(Needs const& all, Priority tier)
        {
            Needs out;
            for (Need const& n : all)
                if (n.priority == tier)
                    out.push_back(n);
            return out;
        }

        // What `vendor` would cover of the tier's needs (and the repair) with what is left.
        uint32 Score(Player* me, Creature* vendor, Needs const& tierNeeds, Budget const& budget, bool repairPending, Priority tier)
        {
            Budget sim = budget;
            BotShared::Vendor::PlanPurchases(me, vendor, tierNeeds, sim);
            uint32 score = 0;
            for (Need const& n : tierNeeds)
                if (budget.StillMissing(n) > sim.StillMissing(n))
                    score += WeightOf(n.priority);
            if (repairPending && vendor->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_REPAIR))
                score += WeightOf(tier) * 2;
            return score;
        }

        // Plans the tier's needs (and the repair) at `vp`, into the real budget.
        void Commit(Player* me, VendorPlan& vp, Needs const& tierNeeds, Budget& budget, bool& repairPending, Priority tier)
        {
            auto const before = budget.planned;
            BotShared::Vendor::PlanPurchases(me, vp.npc, tierNeeds, budget);
            TakeDiff(before, budget, tier, vp.action->needs);
            if (repairPending && vp.npc->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_REPAIR))
            {
                uint32 const cost = BotShared::Vendor::RepairCost(me, vp.npc);
                if (cost <= budget.MoneyFor(tier))
                {
                    budget.money -= cost;
                    vp.action->repair = true;
                    repairPending = false;
                }
            }
        }
    }

    bool ShoppingPlanner::Worth(Player* me)
    {
        for (Need const& need : BotShared::Supplies::Missing(me))
            if (need.priority == Priority::Critical)
                return true;
        if (LowestDurabilityPct(me) <= TRIP_DURABILITY_PCT)
            return true;
        return BotShared::Inventory::FreeBagSlots(me) <= BotShared::Purchasing::KEEP_FREE_SLOTS && BotShared::Vendor::HasJunk(me);
    }

    void ShoppingPlanner::PlanStops(Player* me, Player const* leader, TripPlanStops& plan)
    {
        Needs const needs = BotShared::Supplies::Missing(me);
        uint32 const lowestPct = LowestDurabilityPct(me);
        bool const junk = BotShared::Vendor::HasJunk(me);
        Budget budget = Budget::Of(me, TrainerReserve(me));

        // The auction house: the auctioneer nearest the leader with a mailbox nearby.
        Creature* auctioneer = nullptr;
        GameObject* mailbox = nullptr;
        if (!needs.empty())
            for (Creature* c : LiveNpcs(me, leader, UNIT_NPC_FLAG_AUCTIONEER))
            {
                for (BotShared::Mailbox::Spawn const& s : BotShared::Mailbox::Near(me->GetMapId(), c->GetPositionX(), c->GetPositionY(), MAILBOX_NEAR_AUCTION_YD))
                    if ((mailbox = me->GetMap()->GetGameObject(BotShared::Mailbox::GuidOf(s))))
                        break;
                if (mailbox && SafeTo(me, c) && SafeTo(me, mailbox))
                {
                    auctioneer = c;
                    break;
                }
                mailbox = nullptr;
            }
        auto auction = std::make_shared<AuctionAction>();
        std::set<uint32> taken;

        std::vector<Creature*> const vendors = LiveNpcs(me, leader, UNIT_NPC_FLAG_VENDOR);
        std::set<Creature*> unsafe;
        std::vector<VendorPlan> chosen;

        for (Priority tier : TIERS)
        {
            Needs const tierNeeds = OfTier(needs, tier);
            // Repairs: critical when nearly broken, else with the important business.
            bool repairPending = (tier == Priority::Critical && lowestPct <= TRIP_DURABILITY_PCT) ||
                                 (tier == Priority::Important && lowestPct < 100);
            if (std::any_of(chosen.begin(), chosen.end(), [](VendorPlan const& v) { return v.action->repair; }))
                repairPending = false;     // planned already
            if (tierNeeds.empty() && !repairPending)
                continue;

            // 1. The auction house.
            if (auctioneer && !tierNeeds.empty())
            {
                auto const before = budget.planned;
                BotShared::AuctionMarket::PlanBuys(me, auctioneer, tierNeeds, budget, taken);
                TakeDiff(before, budget, tier, auction->needs);
            }

            // 2. Vendors on the route already.
            for (VendorPlan& vp : chosen)
                Commit(me, vp, tierNeeds, budget, repairPending, tier);

            // 3. New vendors, the one covering most first.
            while (chosen.size() < MAX_VENDORS)
            {
                Creature* best = nullptr;
                uint32 bestScore = 0;
                for (Creature* v : vendors)
                {
                    if (unsafe.count(v) || std::any_of(chosen.begin(), chosen.end(), [v](VendorPlan const& c) { return c.npc == v; }))
                        continue;
                    uint32 const score = Score(me, v, tierNeeds, budget, repairPending, tier);
                    if (score > bestScore)      // (nearest the leader first: ties keep the earlier)
                    {
                        best = v;
                        bestScore = score;
                    }
                }
                if (!best)
                    break;
                if (!SafeTo(me, best))
                {
                    unsafe.insert(best);
                    continue;
                }
                VendorPlan vp;
                vp.npc = best;
                vp.action = std::make_shared<VendorAction>();
                Commit(me, vp, tierNeeds, budget, repairPending, tier);
                chosen.push_back(vp);
            }
        }

        // Junk to sell and no vendor on the way: the nearest one.
        if (chosen.empty() && junk)
            for (Creature* v : vendors)
                if (SafeTo(me, v))
                {
                    VendorPlan vp;
                    vp.npc = v;
                    vp.action = std::make_shared<VendorAction>();
                    chosen.push_back(vp);
                    break;
                }

        // The stops.
        int auctionIndex = -1;
        if (auctioneer && !auction->needs.empty())
            auctionIndex = plan.Add(StopAt(auctioneer, "place.auction", auction));
        if (auctionIndex >= 0 || BotShared::Mailbox::HasMailToTake(me))
        {
            GameObject* box = auctionIndex >= 0 ? mailbox : nullptr;
            if (!box)
            {
                for (BotShared::Mailbox::Spawn const& s : BotShared::Mailbox::Near(me->GetMapId(), leader->GetPositionX(), leader->GetPositionY(), TownTrip::TRIP_YD))
                {
                    GameObject* candidate = me->GetMap()->GetGameObject(BotShared::Mailbox::GuidOf(s));
                    if (candidate && SafeTo(me, candidate))
                    {
                        box = candidate;
                        break;
                    }
                }
            }
            if (box)
            {
                TripStop stop = StopAt(box, nullptr, std::make_shared<MailboxAction>());
                stop.after = auctionIndex;
                plan.Add(stop);
            }
        }
        for (VendorPlan const& vp : chosen)
            plan.Add(StopAt(vp.npc, VendorPlaceKey(*vp.action), vp.action));
    }
}
}
