#include "Commander/Team.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace Commander
{
namespace Team
{
    namespace
    {
        std::mutex s_lock;
        std::unordered_map<uint32, std::vector<ObjectGuid>> s_units;   // unit id -> members, leader first
        std::unordered_map<ObjectGuid, uint32> s_unitOf;

        uint32 UnitOf(ObjectGuid guid)
        {
            std::lock_guard<std::mutex> guard(s_lock);
            auto itr = s_unitOf.find(guid);
            return itr != s_unitOf.end() ? itr->second : 0;
        }

        std::vector<ObjectGuid> UnitMembers(uint32 unitId)
        {
            std::lock_guard<std::mutex> guard(s_lock);
            auto itr = s_units.find(unitId);
            return itr != s_units.end() ? itr->second : std::vector<ObjectGuid>();
        }
    }

    void JoinUnit(ObjectGuid member, uint32 unitId)
    {
        if (member.IsEmpty() || !unitId)
            return;
        LeaveUnit(member);
        std::lock_guard<std::mutex> guard(s_lock);
        s_units[unitId].push_back(member);
        s_unitOf[member] = unitId;
    }

    void LeaveUnit(ObjectGuid member)
    {
        std::lock_guard<std::mutex> guard(s_lock);
        auto itr = s_unitOf.find(member);
        if (itr == s_unitOf.end())
            return;
        auto unit = s_units.find(itr->second);
        if (unit != s_units.end())
        {
            unit->second.erase(std::remove(unit->second.begin(), unit->second.end(), member), unit->second.end());
            if (unit->second.empty())
                s_units.erase(unit);
        }
        s_unitOf.erase(itr);
    }

    uint64 KeyOf(Player const* member)
    {
        if (!member)
            return 0;
        if (Group const* pGroup = member->GetGroup())
            return pGroup->GetId();
        if (uint32 const unit = UnitOf(member->GetObjectGuid()))
            return UNIT_KEY_BIT | unit;
        return 0;
    }

    bool HasTeam(Player const* member)
    {
        return KeyOf(member) != 0;
    }

    bool SameTeam(Player const* a, Player const* b)
    {
        uint64 const key = KeyOf(a);
        return key && key == KeyOf(b);
    }

    std::vector<Player*> Members(Player const* member)
    {
        std::vector<Player*> out;
        if (!member)
            return out;
        if (Group* pGroup = const_cast<Player*>(member)->GetGroup())
        {
            for (GroupReference* itr = pGroup->GetFirstMember(); itr; itr = itr->next())
                if (Player* p = itr->getSource())
                    out.push_back(p);
            return out;
        }
        if (uint32 const unit = UnitOf(member->GetObjectGuid()))
            for (ObjectGuid const& guid : UnitMembers(unit))
                if (Player* p = ObjectAccessor::FindPlayer(guid))
                    out.push_back(p);
        return out;
    }

    ObjectGuid LeaderOf(Player const* member)
    {
        if (!member)
            return ObjectGuid();
        if (Group const* pGroup = member->GetGroup())
            return pGroup->GetLeaderGuid();
        if (uint32 const unit = UnitOf(member->GetObjectGuid()))
        {
            std::vector<ObjectGuid> const members = UnitMembers(unit);
            if (!members.empty())
                return members.front();
        }
        return ObjectGuid();
    }

    bool IsShared(Player const* member)
    {
        if (!member)
            return false;
        if (Group const* pGroup = member->GetGroup())
            return pGroup->GetMembersCount() > 1;
        if (uint32 const unit = UnitOf(member->GetObjectGuid()))
            return UnitMembers(unit).size() > 1;
        return false;
    }
}
}
