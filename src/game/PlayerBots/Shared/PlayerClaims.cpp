#include "Shared/PlayerClaims.h"
#include "Group.h"
#include "Map.h"
#include "Player.h"

namespace BotShared
{
    namespace PlayerClaims
    {
        bool OnPlayersSide(Player const* p)
        {
            if (!p)
                return false;
            if (!p->IsBot())
                return true;
            Group* group = const_cast<Player*>(p)->GetGroup();
            if (!group)
                return false;
            for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
                if (Player const* member = itr->getSource())
                    if (!member->IsBot())
                        return true;
            return false;
        }

        void Collect(Player const* me, WorldObject const* center, float radius, std::vector<Claim>& out)
        {
            out.clear();
            float const reach = radius + CLAIM_YD;
            Group const* myGroup = me->GetGroup();
            for (auto const& ref : me->GetMap()->GetPlayers())
            {
                Player* p = ref.getSource();
                if (!p || p == me || !p->IsInWorld() || !p->IsAlive())
                    continue;
                ObjectGuid const target = p->GetTargetGuid();
                if (!target.IsCreature() || !p->IsWithinDist(center, reach, false))
                    continue;
                if (myGroup && p->GetGroup() == myGroup)
                    continue;
                if (!OnPlayersSide(p))
                    continue;
                Claim c;
                c.creature = target;
                c.player = p->GetObjectGuid();
                c.x = p->GetPositionX();
                c.y = p->GetPositionY();
                c.z = p->GetPositionZ();
                out.push_back(c);
            }
        }

        ObjectGuid ClaimantOf(std::vector<Claim> const& claims, Unit const* creature)
        {
            for (Claim const& c : claims)
                if (c.creature == creature->GetObjectGuid() &&
                    creature->GetDistance(c.x, c.y, c.z, SizeFactor::None) <= CLAIM_YD)
                    return c.player;
            return ObjectGuid();
        }
    }
}
