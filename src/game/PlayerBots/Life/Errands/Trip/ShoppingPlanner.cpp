#include "Life/Errands/Trip/ShoppingPlanner.h"
#include "Life/Errands/TownTrip.h"
#include "Life/Errands/ErrandCommon.h"
#include "Shared/AuctionMarket.h"
#include "Shared/BagSorting.h"
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

        using BotShared::BagSorting::Fate;
        using BotShared::BagSorting::Rules;

        // What is sold where (Shared/BagSorting): vendors always take Vendor items, and Market
        // items too when the trip has no auction house to post them at.
        std::vector<Fate> VendorFates(bool withMarket)
        {
            return withMarket ? std::vector<Fate>{ Fate::Vendor, Fate::Market } : std::vector<Fate>{ Fate::Vendor };
        }

        class AuctionAction final : public IStopAction
        {
        public:
            Needs needs;
            Rules rules;
            bool sell = false;      // post the Market items too
            char const* Name() const override { return "auction"; }
            // (searching takes a while, every listing is compared and bought on its own)
            Browse BrowseTime() const override
            {
                return { 15 * IN_MILLISECONDS, 25 * IN_MILLISECONDS, 6 * IN_MILLISECONDS, 75 * IN_MILLISECONDS };
            }
            uint32 Do(Player* me, WorldObject* target) override
            {
                Creature* auctioneer = target->ToCreature();
                if (!auctioneer)
                    return 0;
                Budget budget = Budget::Of(me, TrainerReserve(me));
                std::set<uint32> taken;
                std::vector<BotShared::AuctionMarket::Listing> const listings =
                    BotShared::AuctionMarket::PlanBuys(me, auctioneer, needs, budget, taken);
                uint32 const asked = BotShared::AuctionMarket::Buyout(me, auctioneer, listings);
                uint32 const posted = sell ? BotShared::AuctionMarket::Post(me, auctioneer,
                    BotShared::BagSorting::WithFate(me, rules, { Fate::Market })) : 0;
                sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Life] %s at the auction house: %u listings for %u copper, %u items posted.",
                    me->GetName(), uint32(listings.size()), asked, posted);
                return uint32(listings.size()) + posted;
            }
        };

        class MailboxAction final : public IStopAction
        {
        public:
            char const* Name() const override { return "mailbox"; }
            Browse BrowseTime() const override
            {
                return { 2 * IN_MILLISECONDS, 4 * IN_MILLISECONDS, 1500, 20 * IN_MILLISECONDS };
            }
            uint32 Do(Player* me, WorldObject* target) override
            {
                GameObject* mailbox = target->ToGameObject();
                return mailbox ? BotShared::Mailbox::TakeAll(me, mailbox) : 0;
            }
        };

        class VendorAction final : public IStopAction
        {
        public:
            Needs needs;
            bool repair = false;
            Rules rules;
            bool sellMarket = false;    // no auction house stop on this trip
            char const* Name() const override { return "vendor"; }
            // (every junk stack, the repair and every item bought is a click or two)
            Browse BrowseTime() const override
            {
                return { 5 * IN_MILLISECONDS, 10 * IN_MILLISECONDS, 2 * IN_MILLISECONDS, 45 * IN_MILLISECONDS };
            }
            uint32 Do(Player* me, WorldObject* target) override
            {
                Creature* vendor = target->ToCreature();
                if (!vendor)
                    return 0;
                uint32 sold = 0;
                uint32 const earned = BotShared::Vendor::Sell(me, vendor,
                    BotShared::BagSorting::WithFate(me, rules, VendorFates(sellMarket)), &sold);
                uint32 repaired = 0;
                if (repair && vendor->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_REPAIR))
                {
                    uint32 const cost = BotShared::Vendor::RepairCost(me, vendor);
                    if (cost && me->GetMoney() >= cost)
                        repaired = BotShared::Vendor::RepairAll(me, vendor);
                }
                Budget budget = Budget::Of(me, TrainerReserve(me));
                std::vector<BotShared::Vendor::Purchase> const purchases =
                    BotShared::Vendor::PlanPurchases(me, vendor, needs, budget);
                uint32 const spent = BotShared::Vendor::Buy(me, vendor, purchases);
                sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Life] %s at vendor %s: sold for %u, repaired for %u, bought for %u copper.",
                    me->GetName(), vendor->GetName(), earned, repaired, spent);
                return sold + (repaired ? 1 : 0) + uint32(purchases.size());
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
        // (a need that wants a trip of its own: portal runes running low, Supplies::Need::tripBelow)
        for (Need const& need : BotShared::Supplies::Missing(me))
            if (need.priority == Priority::Critical || need.WantsTrip())
                return true;
        if (LowestDurabilityPct(me) <= TRIP_DURABILITY_PCT)
            return true;
        return BotShared::Inventory::FreeBagSlots(me) <= BotShared::Purchasing::KEEP_FREE_SLOTS &&
               BotShared::BagSorting::HasAny(me, Bags(), { Fate::Vendor, Fate::Market });
    }

    Rules ShoppingPlanner::Bags() const
    {
        return m_bags ? *m_bags : Rules();
    }

    void ShoppingPlanner::PlanStops(Player* me, Player const* leader, TripPlanStops& plan)
    {
        Needs const needs = BotShared::Supplies::Missing(me);
        uint32 const lowestPct = LowestDurabilityPct(me);
        Rules const bags = Bags();
        bool const toVendor = BotShared::BagSorting::HasAny(me, bags, { Fate::Vendor });
        bool const toMarket = BotShared::BagSorting::HasAny(me, bags, { Fate::Market });
        Budget budget = Budget::Of(me, TrainerReserve(me));

        // The auction house: the auctioneer nearest the leader with a mailbox nearby.
        Creature* auctioneer = nullptr;
        GameObject* mailbox = nullptr;
        if (!needs.empty() || toMarket)
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
        auction->rules = bags;
        auction->sell = auctioneer && toMarket;
        std::set<uint32> taken;
        auto newVendor = [&](Creature* npc)
        {
            VendorPlan vp;
            vp.npc = npc;
            vp.action = std::make_shared<VendorAction>();
            vp.action->rules = bags;
            vp.action->sellMarket = !auction->sell;
            return vp;
        };

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
                VendorPlan vp = newVendor(best);
                Commit(me, vp, tierNeeds, budget, repairPending, tier);
                chosen.push_back(vp);
            }
        }

        // Things for a vendor and no vendor on the way: the nearest one.
        if (chosen.empty() && (toVendor || (toMarket && !auction->sell)))
            for (Creature* v : vendors)
                if (SafeTo(me, v))
                {
                    chosen.push_back(newVendor(v));
                    break;
                }

        // The stops.
        int auctionIndex = -1;
        bool const buys = auctioneer && !auction->needs.empty();
        if (buys || auction->sell)
            auctionIndex = plan.Add(StopAt(auctioneer, "place.auction", auction));
        // (what it bought comes by mail at once; what it posted, once sold: a later trip)
        if (buys || BotShared::Mailbox::HasMailToTake(me))
        {
            GameObject* box = buys ? mailbox : nullptr;
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
                stop.after = buys ? auctionIndex : -1;
                plan.Add(stop);
            }
        }
        for (VendorPlan const& vp : chosen)
            plan.Add(StopAt(vp.npc, VendorPlaceKey(*vp.action), vp.action));
    }
}
}
